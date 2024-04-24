#include "server.h"

#include <asm-generic/socket.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <iostream>
#include <thread>

#include "message.h"
ssize_t RelayServer::buffer_size = 11 * 1024;  // 暂存数据缓冲区的大小
int RelayServer::thread_num_ = 1;              // 线程数

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
    : Server(port, length_of_queue_of_listen, str_bound_ip) {
  // 初始化id到fd的映射
  memset(id_to_fd_, -1, sizeof(id_to_fd_));

  // 初始化fd到id的映射
  memset(fd_to_id_, -1, sizeof(id_to_fd_));

  // 初始化源id的映射
  memset(src_id_, -1, sizeof(src_id_));

  // 初始化第一次连接
  memset(is_first_connected_, 1, sizeof(is_first_connected_));
}

void RelayServer::ServerFunction(int &connected_socket) {
  for (int i = 0; i < thread_num_; ++i) {
    std::thread one_thread(thread_main, std::ref(connected_socket), this);
    one_thread.detach();
  }
  pause();
}

// 多线程epoll
void RelayServer::thread_main(int &listen_socket, RelayServer *obj) {
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
  epoll_event events[1024];

  // std::cerr << "等待客户端连接..." << std::endl;

  // 记录转发数量
  extern int count;
  while (1) {
    // 等待事件触发
    int num_events = epoll_wait(epoll_fd, events, 1024, -1);
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
        obj->accept_mutex_.lock();
        client_socket = accept(listen_socket, (struct sockaddr *)&clientAddress,
                               &clientAddressLength);
        if (client_socket < 0) {
          std::cerr << "接受连接失败：" << strerror(errno) << std::endl;
          exit(-1);
        }
        obj->accept_mutex_.unlock();
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddress.sin_addr, ip_str, INET_ADDRSTRLEN);
#ifdef DEBUG
        std::cerr << "有客户端连接成功：" << ip_str << ":"
                  << ntohs(clientAddress.sin_port) << std::endl;
#endif
        // 将新的连接套接字添加到 epoll 实例中
        event.events = EPOLLIN | EPOLLOUT;  // 监听可读可写事件，水平触发模式
        event.data.fd = client_socket;

        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &event) == -1) {
          std::cerr << "Failed to add client socket to epoll." << std::endl;
          exit(-1);
        }
      } else {
        if (events[i].events & EPOLLIN) {
          // 可读事件
          int client_socket = events[i].data.fd;

          // 刚连接上接收id
          if (obj->is_first_connected_[client_socket]) {
            obj->is_first_connected_[client_socket] = false;
            // 接收id
            int id;
            recv(client_socket, reinterpret_cast<char *>(&id), sizeof(id), 0);
#ifdef DEBUG
            std::cerr << "服务器建立新连接，id为：" << id << std::endl;
#endif
            if (obj->id_to_fd_[id] == -1) {
              // 记录新链接
              obj->id_to_fd_[id] = client_socket;
              obj->fd_to_id_[client_socket] = id;
              obj->dst_id_[id] = -1;
              // obj->src_id_[id] = -1;
              obj->buf[id] = new char[buffer_size];
              obj->ptr_recv_start[id] = obj->buf[id];
              obj->ptr_recv_end[id] = obj->buf[id] + sizeof(Header);
              obj->ptr_send_start[id] = obj->ptr_recv_end[id];
              // 将新的连接套接字设置为非阻塞模式
              int val = fcntl(client_socket, F_GETFL);
              if (fcntl(client_socket, F_SETFL, val | O_NONBLOCK) == -1) {
                std::cerr << "Failed to set client socket to non-blocking mode."
                          << std::endl;
                exit(-1);
              }
            } else {
              std::cerr << "该id已建立链接" << std::endl;
            }

          } else {
            int id = obj->fd_to_id_[client_socket];

            ssize_t nrecv;
            if ((nrecv = recv(client_socket, obj->ptr_recv_start[id],
                              obj->ptr_recv_end[id] - obj->ptr_recv_start[id],
                              MSG_NOSIGNAL)) < 0) {
              if (errno != EWOULDBLOCK) {
                std::cerr << "接收错误" << std::endl;
              }
            } else if (nrecv == 0) {
              // 断开连接
              if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket, nullptr) ==
                  -1) {
                std::cerr << "Failed to remove client socket from epoll."
                          << std::endl;
                exit(-1);
              }
              int id = obj->fd_to_id_[client_socket];
              if (id != -1) {
                obj->id_to_fd_[id] = -1;
                obj->is_first_connected_[client_socket] = true;
                obj->dst_id_[id] = -1;
                obj->ptr_send_start[id] = nullptr;
                obj->ptr_recv_start[id] = nullptr;
                obj->ptr_recv_end[id] = nullptr;
                delete obj->buf[id];
                obj->buf[id] = nullptr;
              }
              close(client_socket);
              std::cerr << "服务器与id为 " << id << " 的客户端断开连接"
                        << std::endl;
            } else {
#ifdef DEBUG
// std::cerr << "接收了 " << nrecv << " 字节" << std::endl;
#endif
              obj->ptr_recv_start[id] += nrecv;
              if (obj->ptr_recv_start[id] == obj->ptr_recv_end[id]) {
                if (obj->dst_id_[id] == -1) {
                  // 头部到达了
                  Header header;
                  memcpy(reinterpret_cast<char *>(&header), obj->buf[id],
                         sizeof(header));
                  obj->dst_id_[id] = header.dst_id_;
                  obj->src_id_[header.dst_id_] = id;
#ifdef DEBUG
                  std::cerr << "服务器收到来自" << header.src_id_ << "发给"
                            << header.dst_id_ << "的长为" << header.data_len_
                            << "信息" << std::endl;
#endif
                  // 设置尾部指针准备接收数据
                  obj->ptr_recv_end[id] += header.data_len_ * sizeof(char);
                  obj->ptr_send_start[id] = obj->ptr_recv_end[id];
                } else {
#ifdef DEBUG
                  std::cerr << id << " 的消息接收完了" << std::endl;
#endif
                  Message message;
                  memcpy(reinterpret_cast<char *>(&message), obj->buf[id],
                         obj->ptr_recv_start[id] - obj->buf[id]);
#ifdef DEBUG
                  std::cerr << message.data << std::endl;
#endif
                  // 可以开始转发了
                  obj->ptr_send_start[id] = obj->buf[id];
                  // event.events =
                  //     EPOLLIN | EPOLLOUT;  // 开始转发时才开始监听可写
                  // event.data.fd = obj->id_to_fd_[obj->dst_id_[id]]; //
                  // 不保证在线 epoll_ctl(epoll_fd, EPOLL_CTL_MOD,
                  // event.data.fd, &event);
                }
              }
            }
          }
        }
        if (events[i].events & EPOLLOUT) {
          // 可写事件
          int client_socket = events[i].data.fd;
          int id = obj->fd_to_id_[client_socket];
          if (id != -1 && obj->src_id_[id] != -1 &&
              obj->ptr_recv_start[obj->src_id_[id]] -
                      obj->ptr_send_start[obj->src_id_[id]] >
                  0) {
            int src_id = obj->src_id_[id];
#ifdef DEBUG
// std::cerr << "目的：" << id << " 源:" << src_id << std::endl;
#endif
            ssize_t nsend;
            if ((nsend = send(
                     client_socket, obj->ptr_send_start[src_id],
                     obj->ptr_recv_start[src_id] - obj->ptr_send_start[src_id],
                     MSG_NOSIGNAL)) < 0) {
              if (errno != EWOULDBLOCK) {
                std::cerr << "发送出错" << std::endl;
              }
            } else {
#ifdef DEBUG
// std::cerr << "发送 " << nsend << " 字节" << std::endl;
#endif
              obj->ptr_send_start[src_id] += nsend;
              if (obj->ptr_send_start[src_id] == obj->ptr_recv_end[src_id]) {
#ifdef DEBUG
                std::cerr << src_id << " 的消息转发完了" << std::endl;
                std::cerr << "重置缓冲区 " << src_id << std::endl;
#endif
                obj->ptr_recv_start[src_id] = obj->buf[src_id];
                obj->ptr_recv_end[src_id] = obj->buf[src_id] + sizeof(Header);
                obj->ptr_send_start[src_id] = obj->ptr_recv_end[src_id];
                obj->dst_id_[src_id] = -1;
                obj->src_id_[id] = -1;

                ++count;
                // event.events = EPOLLIN;  // 结束转发时 取消可写监听
                // event.data.fd = client_socket;
                // epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_socket, &event);
              }
            }
          }
        }
      }
    }
  }
  close(epoll_fd);
}