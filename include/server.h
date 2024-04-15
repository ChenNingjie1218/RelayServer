#ifndef SERVER_H_
#define SERVER_H_
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
class Server {
 public:
  Server() = delete;
  Server(int port, int length_of_queue_of_listen = 100,
         const char *str_bound_ip = NULL);
  virtual ~Server();
  void Run();

 protected:
  virtual bool ServerFunction(int &connected_socket) = 0;
  // 多线程epoll
  static void thread_main(int &listen_socket, Server *obj) {
    {
      // 创建 epoll 实例
      int epoll_fd = epoll_create1(0);
      if (epoll_fd == -1) {
        std::cerr << "Failed to create epoll instance." << std::endl;
        exit(-1);
      }

      // 添加监听套接字到 epoll 实例中
      epoll_event event{};
      event.events = EPOLLIN;  // 监听可读事件，默认水平触发
      event.data.fd = listen_socket;

      if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_socket, &event) == -1) {
        std::cerr << "Failed to add listen socket to epoll." << std::endl;
        exit(-1);
      }

      // 创建事件数组用于存储触发的事件
      epoll_event events[1024];

      // std::cout << "等待客户端连接..." << std::endl;

      while (1) {
        // 等待事件触发
        int num_events = epoll_wait(epoll_fd, events, 1024, -1);
        if (num_events == -1) {
          std::cerr << "Failed to wait for events." << std::endl;
          exit(-1);
        }

        for (int i = 0; i < num_events; ++i) {
          if (events[i].data.fd == listen_socket) {
            // 有新的连接请求

            // 接受客户端连接
            int client_socket;
            sockaddr_in clientAddress;
            socklen_t clientAddressLength = sizeof(clientAddress);
            obj->accept_mutex_.lock();
            client_socket =
                accept(listen_socket, (struct sockaddr *)&clientAddress,
                       &clientAddressLength);
            if (client_socket < 0) {
              std::cerr << "接受连接失败：" << strerror(errno) << std::endl;
              exit(-1);
            }
            obj->accept_mutex_.unlock();
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddress.sin_addr, ip_str,
                      INET_ADDRSTRLEN);
            std::cout << "有客户端连接成功：" << ip_str << ":"
                      << ntohs(clientAddress.sin_port) << std::endl;

            // 将新的连接套接字设置为非阻塞模式
            // if (fcntl(client_socket, F_SETFL, O_NONBLOCK) == -1) {
            //   std::cerr << "Failed to set client socket to non-blocking
            //   mode."
            //             << std::endl;
            //   exit(-1);
            // }
            // 将新的连接套接字添加到 epoll 实例中
            event.events = EPOLLIN;  // 监听可读事件，并设置为水平触发模式
            event.data.fd = client_socket;

            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &event) ==
                -1) {
              std::cerr << "Failed to add client socket to epoll." << std::endl;
              exit(-1);
            }
          } else {
            // 可读事件
            int client_socket = events[i].data.fd;

            // 刚连接上接收id
            if (obj->is_first_connected_[client_socket]) {
              obj->is_first_connected_[client_socket] = false;
              // 接收id
              int id;
              recv(client_socket, reinterpret_cast<char *>(&id), sizeof(id), 0);
              std::cout << "服务器建立新链接，id为：" << id << std::endl;
              if (obj->id_to_fd_[id] == -1) {
                // 记录新链接
                obj->id_to_fd_[id] = client_socket;
                obj->fd_to_id_[client_socket] = id;
              } else {
                std::cerr << "该id已建立链接" << std::endl;
              }
            } else {
              // 接收数据后提供的服务
              if (!obj->ServerFunction(client_socket)) {
                // 断开连接
                if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_socket,
                              nullptr) == -1) {
                  std::cerr << "Failed to remove client socket from epoll."
                            << std::endl;
                  exit(-1);
                }
                int id = obj->fd_to_id_[client_socket];
                obj->id_to_fd_[id] = -1;
                obj->is_first_connected_[client_socket] = false;
                close(client_socket);
                std::cout << "服务器与id为 " << id << " 的客户端断开连接"
                          << std::endl;
              }
            }
          }
        }
      }
      close(epoll_fd);
    }
  }

  int port_;                        // 端口号
  int length_of_queue_of_listen_;   // 监听队列最大长度
  char *str_bound_ip_;              // 绑定的ip
  int id_to_fd_[20000];             // 散列表映射id与fd
  int fd_to_id_[20010];             // 散列表映射fd与id
  bool is_first_connected_[20010];  // 刚连接上
  int thread_num_;                  // 线程数
  std::mutex accept_mutex_;         // accept的锁
};

class RelayServer : public Server {
 public:
  RelayServer(int port, int length_of_queue_of_listen = 100,
              const char *str_bound_ip = NULL);
  ~RelayServer() = default;

 protected:
  // 转发功能
  bool ServerFunction(int &connected_socket) override;
};
#endif