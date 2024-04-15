#include "server.h"

#include <asm-generic/socket.h>

#include "message.h"
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

  // 初始化id到fd的映射
  memset(id_to_fd_, -1, sizeof(id_to_fd_));

  // 初始化第一次连接
  memset(is_first_connected_, 1, sizeof(is_first_connected_));

  // 线程数
  thread_num_ = 100;
}

// 析构函数
Server::~Server() {
  if (str_bound_ip_) {
    delete[] str_bound_ip_;
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
      std::cout << "inet_pton error" << std::endl;
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

  // 多线程epoll
  for (int i = 0; i < thread_num_; ++i) {
    auto one_thread =
        new std::thread(thread_main, std::ref(server_socket), this);
    one_thread->detach();
  }
  pause();
  close(server_socket);
}

// RelayServer
RelayServer::RelayServer(int port, int length_of_queue_of_listen,
                         const char *str_bound_ip)
    : Server(port, length_of_queue_of_listen, str_bound_ip) {}

bool RelayServer::ServerFunction(int &connected_socket) {
  const int buffer_size = 4 * 1024;
  char buf[buffer_size];
  memset(buf, 0, buffer_size);
  ssize_t read_size = recv(connected_socket, buf, buffer_size, 0);
  if (read_size > 0) {
    Header header;
    memcpy(&header, buf, sizeof(header));
    char data[1024];
    memset(data, 0, sizeof(data));
    memcpy(data, buf + sizeof(header), header.data_len_);
    std::cout << "服务器收到来自" << header.src_id_ << "发给" << header.dst_id_
              << "的长为" << header.data_len_ << "信息:" << data << std::endl;

    // 转发该消息
    if (id_to_fd_[header.dst_id_] != -1) {
      // dst存在
      send(id_to_fd_[header.dst_id_], buf, read_size, 0);
    } else {
      // dst不存在 回复对方不存在
      std::string back = "对方不存在";
      send(connected_socket, back.c_str(), back.length(), 0);
    }
    return true;
  } else {
    return false;
  }
}