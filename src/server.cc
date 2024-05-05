#include "server.h"

#include <asm-generic/socket.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <iostream>

#include "client-connected.h"
#include "param.h"

// 构造函数
Server::Server(int port, int length_of_queue_of_listen,
               const char *str_bound_ip)
    : port_(port), length_of_queue_of_listen_(length_of_queue_of_listen) {
  // 存绑定的ip
  if (str_bound_ip != NULL) {
    auto len = strlen(str_bound_ip) + 1;
    str_bound_ip_ = new char[len];
    strncpy((char *)str_bound_ip_, str_bound_ip, len);
  } else {
    str_bound_ip_ = NULL;
  }
}

// 析构函数
Server::~Server() {
  if (str_bound_ip_) {
    delete[] str_bound_ip_;
    str_bound_ip_ = nullptr;
  }
}

void Server::Run() {
  // 创建服务器套接字
  int server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (-1 == server_socket) {
    std::cerr << "无法创建套接字" << strerror(errno) << std::endl;
    exit(-1);
  }

  // 设置监听套接字为非阻塞模式
  // if (fcntl(server_socket, F_SETFL, O_NONBLOCK) == -1) {
  //   std::cerr << "Failed to set non-blocking mode." << std::endl;
  //   exit(-1);
  // }

  // 准备服务器地址
  sockaddr_in server_address;
  memset(&server_address, 0, sizeof(sockaddr_in));
  server_address.sin_family = AF_INET;
  //静态绑定IP，如果没有，则为本地IP
  if (NULL == str_bound_ip_) {
    server_address.sin_addr.s_addr = INADDR_ANY;
  } else {
    //如果有，则绑定
    if (inet_pton(AF_INET, str_bound_ip_, &server_address.sin_addr) != 1) {
      std::cerr << "inet_pton error" << std::endl;
      close(server_socket);
      exit(-1);
    }
  }
  server_address.sin_port = htons(port_);
  int reuse = 1;
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse,
                 sizeof(reuse)) == -1) {
    std::cerr << "Failed to set SO_REUSEADDR option." << std::endl;
    exit(-1);
  }

  // 绑定套接字
  if (bind(server_socket, (sockaddr *)&server_address, sizeof(server_address)) <
      0) {
    std::cerr << "绑定失败:" << strerror(errno) << std::endl;
    close(server_socket);
    exit(-1);
  }

  // 监听连接
  if (listen(server_socket, length_of_queue_of_listen_) < 0) {
    std::cerr << "监听失败" << strerror(errno) << std::endl;
    exit(-1);
  }

  ServerFunction(server_socket);
  close(server_socket);
}

// RelayServer
RelayServer::RelayServer(int port, int length_of_queue_of_listen,
                         const char *str_bound_ip)
    : Server(port, length_of_queue_of_listen, str_bound_ip) {}

void RelayServer::ServerFunction(int &listen_socket) {
  // 创建 epoll 实例
  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    std::cerr << "Failed to create epoll instance." << std::endl;
    exit(-1);
  }

  // 添加监听套接字到 epoll 实例中
  epoll_event event{};
  event.events = EPOLLIN;  // 监听可读事件,水平触发
  event.data.fd = listen_socket;

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_socket, &event) == -1) {
    std::cerr << "Failed to add listen socket to epoll." << std::endl;
    exit(-1);
  }

  // 创建事件数组用于存储触发的事件
  epoll_event events[MAX_EVENTS];

  // std::cerr << "等待客户端连接..." << std::endl;

  while (1) {
    // 等待事件触发
    int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    if (num_events == -1) {
      std::cerr << "Failed to wait for events." << std::endl;
      exit(-1);
    }

    // std::cerr << "num_events:" << num_events << std::endl;

    for (int i = 0; i < num_events; ++i) {
      if (events[i].data.fd == listen_socket) {
        // 有新的连接请求

        // 接受客户端连接
        int client_socket;
        sockaddr_in clientAddress;
        socklen_t clientAddressLength = sizeof(clientAddress);
        accept_mutex_.lock();
        client_socket = accept(listen_socket, (struct sockaddr *)&clientAddress,
                               &clientAddressLength);
        if (client_socket < 0) {
          std::cerr << "接受连接失败：" << strerror(errno) << std::endl;
          exit(-1);
        }
        accept_mutex_.unlock();
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddress.sin_addr, ip_str, INET_ADDRSTRLEN);
#ifdef DEBUG
        std::cerr << "有客户端连接成功：" << ip_str << ":"
                  << ntohs(clientAddress.sin_port) << std::endl;
#endif
        if (client_socket > MAX_CLIENT_NUM) {
          std::cerr << "连接数超过最大限制" << std::endl;
          exit(-1);
        }
        fd_to_client_[client_socket] = new ClientConnected(client_socket);

        // 将新的连接套接字设置为非阻塞模式
        int val = fcntl(client_socket, F_GETFL);
        if (fcntl(client_socket, F_SETFL, val | O_NONBLOCK) == -1) {
          std::cerr << "Failed to set client socket to non-blocking mode."
                    << std::endl;
          exit(-1);
        }

        // 将新的连接套接字添加到 epoll 实例中
        event.events = EPOLLIN | EPOLLOUT;  // 监听可读可写事件，水平触发模式
        event.data.fd = client_socket;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &event) == -1) {
          std::cerr << "Failed to add client socket to epoll." << std::endl;
          exit(-1);
        }
      } else {
        int client_socket = events[i].data.fd;
        // 当前客户端对象指针
        ClientConnected *ptr_client = fd_to_client_[client_socket];
        if (ptr_client == nullptr) {
          std::cerr << "该客户端已经断连" << std::endl;
          exit(-1);
        }
        if (events[i].events & EPOLLIN) {
          // 可读事件

          ssize_t nrecv = ptr_client->RecvData(id_to_client_);
          if (nrecv == 0) {
            // 断开连接
            if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, nullptr) ==
                -1) {
              std::cerr << "Failed to remove client socket from epoll."
                        << std::endl;
              exit(-1);
            }

            fd_to_client_[client_socket] = nullptr;
            close(client_socket);
            if (ptr_client->GetId() != -1) {
              // 如果已经登记了id，更新id_to_client_映射

#ifdef DEBUG
              std::cerr << "服务器与id为 " << ptr_client->GetId()
                        << " 的客户端断开连接" << std::endl;
#endif
              // todo: 断线重连的话这个要保留
              if (ptr_client->GetDstId() != -1 &&
                  id_to_client_.find(ptr_client->GetDstId()) !=
                      id_to_client_.end()) {
                // 有目的客户端 需要目的客户端将源客户端置空
                // 防止再从该客户端读取数据
                id_to_client_[ptr_client->GetDstId()]->SetSrcClient(nullptr);
              }
              id_to_client_.erase(ptr_client->GetId());
            }
            delete ptr_client;
            continue;
          }
        }
        if (events[i].events & EPOLLOUT) {
          // 可写事件

          ptr_client->SendData();
        }
      }
    }
  }
  close(epoll_fd);
}
