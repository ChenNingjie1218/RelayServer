#include "client.h"

#include <fcntl.h>
#include <sys/epoll.h>

#include <sstream>

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
    std::cout << "inet_pton error" << std::endl;
    close(client_socket);
    exit(-1);
  }
  server_address.sin_port = htons(serverport_);

  //连接到服务器
  if (connect(client_socket, (sockaddr*)&server_address,
              sizeof(server_address)) == -1) {
    std::cout << "连接失败：" << strerror(errno) << std::endl;
    close(client_socket);
    exit(-1);
  }

  std::cout << "连接服务器成功" << std::endl;

  ClientFunction(client_socket);
  //关闭socket
  close(client_socket);
}

// MyClient

MyClient::MyClient(int serverport, const char* str_server_ip, int id)
    : Client(serverport, str_server_ip), id_(id) {}

void MyClient::ClientFunction(int connected_socket) {
  // 发送自己的id
  std::cout << "发送自己的id:" << id_ << std::endl;
  send(connected_socket, reinterpret_cast<char*>(&id_), sizeof(id_), 0);

  // 创建 epoll 实例
  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    std::cerr << "Failed to create epoll instance." << std::endl;
    exit(-1);
  }

  if (fcntl(connected_socket, F_SETFL, O_NONBLOCK) == -1) {
    std::cerr << "Failed to set non-blocking mode." << std::endl;
    exit(-1);
  }

  // 添加连接的套接字到 epoll 实例中
  epoll_event event{};
  event.events = EPOLLIN | EPOLLOUT;  // 监听可读事件, 可写事件
  event.data.fd = connected_socket;

  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connected_socket, &event) == -1) {
    std::cerr << "Failed to add connected_socket to epoll." << std::endl;
    exit(-1);
  }

  // 创建事件数组用于存储触发的事件
  epoll_event events[1024];

  while (1) {
    // 等待事件触发
    int num_events = epoll_wait(epoll_fd, events, 1024, -1);
    if (num_events == -1) {
      std::cerr << "Failed to wait for events." << std::endl;
      exit(-1);
    }
    std::cout << "events num:" << num_events << std::endl;
    for (int i = 0; i < num_events; ++i) {
      if (events[i].events & EPOLLOUT) {
        // 可写事件
        Message message;
        message.header.dst_id_ = (id_ & 1) ? id_ - 1 : id_ + 1;
        message.header.src_id_ = id_;
        std::stringstream ss;
        ss << "测试数据: " << id_ << "--------------" << message.header.dst_id_;
        std::string data = ss.str();
        strncpy(message.data, data.c_str(), data.length());
        message.header.data_len_ = data.length();
        send(connected_socket, reinterpret_cast<char*>(&message),
             sizeof(message), 0);
      }

      if (events[i].events & EPOLLIN) {
        // 可读事件
        const int buffer_size = 4 * 1024;
        char buf[buffer_size];
        bzero(buf, sizeof(buf));
        auto nread = recv(connected_socket, buf, buffer_size, 0);
        if (nread > 0) {
          // 读内容

          // 读报头
          Header header;
          memcpy(&header, buf, sizeof(header));
          char data[1024];
          bzero(data, sizeof(data));
          memcpy(data, buf + sizeof(header), header.data_len_);
          if (id_ == header.src_id_) {
            // 收到回射信息 丢弃
            std::cout << "客户端 " << id_ << "收到来自 " << header.src_id_
                      << " 的回射消息：" << data << std::endl;
          } else {
            // 回射消息
            std::cout << "客户端 " << id_ << " 收到来自 " << header.src_id_
                      << " 的信息:" << data << std::endl;
            // 更换报头
            Message back_message;
            back_message.header.src_id_ = id_;
            back_message.header.dst_id_ = header.src_id_;
            back_message.header.data_len_ = header.data_len_;
            memcpy(back_message.data, data, header.data_len_);
            send(connected_socket, reinterpret_cast<char*>(&back_message),
                 sizeof(back_message), 0);
          }
        } else {
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
        }
      }
    }
  }
}