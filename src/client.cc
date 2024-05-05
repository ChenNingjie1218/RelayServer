#include "client.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sstream>
#include <thread>
#include <utility>

#include "buffer.h"
#include "message.h"
#include "test-message.h"

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
  // 将连接套接字设置为非阻塞模式
  int val = fcntl(connected_socket, F_GETFL);
  if (fcntl(connected_socket, F_SETFL, val | O_NONBLOCK) == -1) {
    std::cerr << "Failed to set connected socket to non-blocking mode."
              << std::endl;
    exit(-1);
  }
  if (id_ & 1) {
    // id为奇数担任回射服务器的角色
    EchoServer(connected_socket);
  } else {
    // id为偶数担任压力产生器的角色
    PressureGenerator(connected_socket);
  }
}

// 担任压力发生器
void MyClient::PressureGenerator(int connected_socket) {
  // 计时
  auto start = std::chrono::high_resolution_clock::now();
  auto end = std::chrono::high_resolution_clock::now();
  extern std::chrono::duration<double, std::milli> duration;
  // 计数
  extern int count;

  // 测试次数
  // int test_count = 1;
  // for (int i = 1; i <= test_count; ++i) {
  //   TestMessage* test_message =
  //       new TestMessage(id_, id_ + 1, i, message_size_, connected_socket);
  //   start = std::chrono::high_resolution_clock::now();
  //   switch (test_message->Test()) {
  //     case 1:
  //       // 连接中断
  //       std::cerr << "测试失败：连接中断" << std::endl;
  //       break;
  //     case -1:
  //       // 回射信息不符
  //       std::cerr << "测试失败：回射信息不符" << std::endl;
  //       exit(-1);
  //       break;
  //     case 0:
  //       // 测试成功
  //       ++count;
  //       end = std::chrono::high_resolution_clock::now();
  //       duration += end - start;
  //       break;
  //   }
  //   delete test_message;
  // }

  // 无限测试
  while (1) {
    TestMessage* test_message =
        new TestMessage(id_, id_ + 1, 1, message_size_, connected_socket);
    start = std::chrono::high_resolution_clock::now();
    switch (test_message->Test()) {
      case 1:
        // 连接中断
        std::cerr << "测试失败：连接中断" << std::endl;
        break;
      case -1:
        // 回射信息不符
        std::cerr << "测试失败：回射信息不符" << std::endl;
        exit(-1);
        break;
      case 0:
        // 测试成功
        ++count;
        end = std::chrono::high_resolution_clock::now();
        duration += end - start;
        break;
    }
    delete test_message;
  }
}
// 担任回射服务器
void MyClient::EchoServer(int connected_socket) {
  Buffer* buffer = new Buffer();
  buffer->InitPtr();
  ssize_t nrecv, nsend;
  // 初始化报头，dst_id_为-1表示报头还没读到
  Header header;
  header.dst_id_ = -1;
  int rest_data_len = 0;  // 未读的报文长度
  while (1) {
    // 接收
    if ((nrecv = recv(connected_socket, buffer->GetRecvStart(),
                      buffer->GetRecvEnd() - buffer->GetRecvStart(),
                      MSG_NOSIGNAL)) < 0) {
      if (errno != EWOULDBLOCK) {
        std::cerr << "回射服务器接收错误" << strerror(errno) << std::endl;
      }
    } else if (nrecv == 0) {
      // 服务器断连了
      std::cerr << "中继服务器与回射服务器断连" << std::endl;
      return;
    } else {
      buffer->MoveRecvStart(nrecv);
      if (header.dst_id_ != -1) {
        // 读取的是数据
        buffer->UpdateSendEnd();
        rest_data_len -= nrecv;
      }
      if (buffer->IsRecvFinish()) {
        if (header.dst_id_ == -1) {
          // 接收完报头
          memcpy(&header, buffer->GetBuffer(), sizeof(Header));
#ifdef DEBUG
          std::cerr << "回射服务器收到了来自 " << header.src_id_
                    << " 的消息，长度:" << header.data_len_ << std::endl;
#endif
          std::swap(header.dst_id_, header.src_id_);
          memcpy(buffer->GetBuffer(), &header, sizeof(Header));
          rest_data_len = header.data_len_;
          // 开始接收数据
          buffer->UpdateRecvEnd(rest_data_len);
          // 开始回射
          buffer->UpdateSendEnd();
        } else if (rest_data_len) {
          // 读数据导致缓冲区满
          buffer->UpdateRecvEnd(rest_data_len);
        }
      }
    }

    // 发送
    if ((nsend = send(connected_socket, buffer->GetSendStart(),
                      buffer->GetSendEnd() - buffer->GetSendStart(),
                      MSG_NOSIGNAL)) < 0) {
      if (errno != EWOULDBLOCK) {
        std::cerr << "回射服务器发送出错" << strerror(errno) << std::endl;
      }
    } else {
      buffer->MoveSendStart(nsend);
      if (buffer->IsSendFinish()) {
        buffer->UpdateSendEnd();
        // 接收数据受发送数据所限制
        if (rest_data_len) {
          buffer->UpdateRecvEnd(rest_data_len);
        } else {
          // 转发完成
#ifdef DEBUG
          std::cerr << header.dst_id_ << " 的消息回射完了" << std::endl;
#endif
          header.dst_id_ = -1;
          buffer->InitPtr();
        }
      }
    }
  }
}
