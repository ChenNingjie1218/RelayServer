#include "client.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sstream>
#include <utility>

#include "message.h"

// 构造函数
Client::Client(int serverport, const char* str_server_ip)
    : serverport_(serverport) {
  //存服务器的ip
  if (str_server_ip) {
    auto len = strlen(str_server_ip) + 1;
    str_server_ip_ = new char[len];
    strncpy((char*)str_server_ip_, str_server_ip, len);
  } else {
    str_server_ip_ = NULL;
  }
}

// 析构函数
Client::~Client() {
  if (str_server_ip_) {
    delete[] str_server_ip_;
    str_server_ip_ = nullptr;
  }
}

void Client::Run() {
  // 创建客户端套接字
  int client_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (-1 == client_socket) {
    std::cerr << "无法创建套接字" << strerror(errno) << std::endl;
    exit(-1);
  }

  // 准备服务器地址
  sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  if (inet_pton(AF_INET, str_server_ip_, &server_address.sin_addr) != 1) {
    std::cerr << "inet_pton error" << std::endl;
    close(client_socket);
    exit(-1);
  }
  server_address.sin_port = htons(serverport_);

  //连接到服务器
  if (connect(client_socket, (sockaddr*)&server_address,
              sizeof(server_address)) == -1) {
    std::cerr << "连接失败：" << strerror(errno) << std::endl;
    close(client_socket);
    exit(-1);
  }
#ifdef DEBUG
  std::cerr << "连接服务器成功" << std::endl;
#endif
  ClientFunction(client_socket);
  //关闭socket
  close(client_socket);
}

// MyClient

MyClient::MyClient(int serverport, const char* str_server_ip, int id,
                   int message_size)
    : Client(serverport, str_server_ip), id_(id), message_size_(message_size) {}

void MyClient::ClientFunction(int connected_socket) {
  // 发送自己的id

#ifdef DEBUG
  std::cerr << "发送自己的id:" << id_ << std::endl;
#endif
  send(connected_socket, reinterpret_cast<char*>(&id_), sizeof(id_), 0);

  // 创建 epoll 实例
  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    std::cerr << "Failed to create epoll instance." << std::endl;
    exit(-1);
  }

  int val = fcntl(connected_socket, F_GETFL, 0);
  if (fcntl(connected_socket, F_SETFL, val | O_NONBLOCK) == -1) {
    std::cerr << "Failed to set non-blocking mode." << std::endl;
    exit(-1);
  }

  // 添加连接的套接字到 epoll 实例中
  epoll_event event{};
  event.events = EPOLLIN | EPOLLOUT;  // 监听可读事件, 可写事件, 水平触发
  event.data.fd = connected_socket;

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connected_socket, &event) == -1) {
    std::cerr << "Failed to add connected_socket to epoll." << std::endl;
    exit(-1);
  }

  // 创建事件数组用于存储触发的事件
  epoll_event events[1024];

  // 要发送的内容
  Message message;
  message.header.dst_id_ = (id_ & 1) ? id_ - 1 : id_ + 1;
  message.header.src_id_ = id_;
  message.header.origin_id_ = id_;
  std::stringstream ss;
  ss << "测试数据: " << id_ << " --------------> " << message.header.dst_id_;
  std::string data = ss.str();
  strncpy(message.data, data.c_str(), data.length());
  message.header.data_len_ = data.length();

  // 初始化缓冲区指针
  char* ptr_send_message_start = reinterpret_cast<char*>(&message);
  char* ptr_send_message_end =
      ptr_send_message_start + sizeof(Header) + sizeof(char) * data.length();
  /*
   * 假设收到的跟发送的一样大
   */
  char* ptr_recv_message_start = ptr_send_message_end;

  if (id_ & 1) {
    // 有一个客户端作为回射服务器
    ptr_send_message_start = ptr_send_message_end;
    ptr_recv_message_start = reinterpret_cast<char*>(&message);
  }

  // 记录发送/接收了多少
  ssize_t nsend = 0, nrecv = 0;

  // 计时
  auto start = std::chrono::high_resolution_clock::now();
  auto end = std::chrono::high_resolution_clock::now();

  extern int count;
  extern std::chrono::duration<double, std::milli> duration;

  while (1) {
    // 等待事件触发
    int num_events = epoll_wait(epoll_fd, events, 1024, -1);
    if (num_events == -1) {
      std::cerr << "Failed to wait for events." << std::endl;
      exit(-1);
    }
#ifdef DEBUG
    // std::cerr << "events num:" << num_events << std::endl;
#endif
    for (int i = 0; i < num_events; ++i) {
      if ((events[i].events & EPOLLOUT) &&
          ptr_send_message_end - ptr_send_message_start > 0) {
        if ((nsend = send(connected_socket, ptr_send_message_start,
                          ptr_send_message_end - ptr_send_message_start,
                          MSG_NOSIGNAL)) < 0) {
          if (errno != EWOULDBLOCK) {
            std::cerr << "发送错误" << std::endl;
          }
        } else {
#ifdef DEBUG
// std::cerr << "发送了 " << nsend << " 字节" << std::endl;
#endif
          if (ptr_send_message_start == reinterpret_cast<char*>(&message)) {
            // 刚开始发
            start = std::chrono::high_resolution_clock::now();
          }
          ptr_send_message_start += nsend;
          if (ptr_send_message_start == ptr_send_message_end) {
#ifdef DEBUG
            std::cerr << id_ << " 发送完毕！" << std::endl;
#endif
            ptr_recv_message_start = reinterpret_cast<char*>(&message);
          }
        }
      }
      if (events[i].events & EPOLLIN) {
        // 可读事件
        if ((nrecv = recv(connected_socket, ptr_recv_message_start,
                          ptr_send_message_start - ptr_recv_message_start, 0)) <
            0) {
          if (errno != EWOULDBLOCK) {
            std::cerr << "接收错误" << std::endl;
          }
        } else if (nrecv == 0) {
          // 服务器断连了
          std::cerr << "服务器断连了" << std::endl;
          if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, connected_socket, nullptr) ==
              -1) {
            std::cerr << "Failed to remove connected_socket from epoll."
                      << std::endl;
            exit(-1);
          }
          close(epoll_fd);
          return;
        } else {
#ifdef DEBUG
// std::cerr << "接收了 " << nrecv << " 字节" << std::endl;
#endif
          ptr_recv_message_start += nrecv;

          if (ptr_recv_message_start == ptr_send_message_end) {
            if (message.header.origin_id_ == id_) {
              // 收到回射信息

#ifdef DEBUG
              std::cerr << "客户端 " << id_ << " 收到来自 "
                        << message.header.src_id_ << " 的回射消息："
                        << message.data << std::endl;
#endif
              // ptr_recv_message_start = reinterpret_cast<char*>(&message);
              end = std::chrono::high_resolution_clock::now();
              duration += end - start;
              ++count;
#ifdef DEBUG
              // std::cerr << "延时:" << duration.count() << std::endl;
#endif
              std::swap(message.header.src_id_, message.header.dst_id_);
#ifdef DEBUG
              std::cerr << "更改报头: dst = " << message.header.dst_id_
                        << std::endl;
#endif
              ptr_send_message_start = reinterpret_cast<char*>(&message);
            } else {
              // 回射消息

#ifdef DEBUG
              std::cerr << "客户端 " << id_ << " 收到来自 "
                        << message.header.src_id_ << " 的消息:" << message.data
                        << std::endl;
#endif
              std::swap(message.header.src_id_, message.header.dst_id_);
#ifdef DEBUG
              std::cerr << "更改报头: dst = " << message.header.dst_id_
                        << std::endl;
#endif
              ptr_send_message_start = reinterpret_cast<char*>(&message);
            }
          }
        }
      }
    }
  }
}