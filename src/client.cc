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
#include "param.h"
#include "test-message.h"
// 构造函数
Client::Client(int serverport, const char* str_server_ip, int id)
    : serverport_(serverport), id_(id) {
  //存服务器的ip
  if (str_server_ip) {
    auto len = strlen(str_server_ip) + 1;
    str_server_ip_ = new char[len];
    strncpy((char*)str_server_ip_, str_server_ip, len);
  } else {
    str_server_ip_ = NULL;
  }

  // 还未发送id
  is_send_id_ = false;
  ptr_send_id_ = reinterpret_cast<char*>(&id_);
}

// 析构函数
Client::~Client() {
  if (str_server_ip_ != nullptr) {
    delete[] str_server_ip_;
    str_server_ip_ = nullptr;
  }
}

int Client::Run() {
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

  // 将套接字设置为非阻塞模式
  int val = fcntl(client_socket, F_GETFL);
  if (fcntl(client_socket, F_SETFL, val | O_NONBLOCK) == -1) {
    std::cerr << "Failed to set client socket to non-blocking mode."
              << std::endl;
    exit(-1);
  }

  //连接到服务器
  int connectResult;
  errno = 0;
  if ((connectResult = connect(client_socket, (sockaddr*)&server_address,
                               sizeof(server_address))) <= 0) {
    if (connectResult == 0) {
#ifdef DEBUG
      std::cerr << "连接服务器成功" << std::endl;
#endif
      SetConnected(connectResult);
    } else if (errno != EINPROGRESS) {
      std::cerr << "连接失败：" << strerror(errno) << std::endl;
      close(client_socket);
      exit(-1);
    }
  }

  // 将连接套接字设置为非阻塞模式
  // int val = fcntl(client_socket, F_GETFL);
  // if (fcntl(client_socket, F_SETFL, val | O_NONBLOCK) == -1) {
  //   std::cerr << "Failed to set client socket to non-blocking mode."
  //             << std::endl;
  //   exit(-1);
  // }
  return client_socket;
}

void Client::SetConnected(int& fd) {
  is_connected_ = true;
  // 将连接套接字设置为非阻塞模式
  int val = fcntl(fd, F_GETFL);
  if (fcntl(fd, F_SETFL, val | O_NONBLOCK) == -1) {
    std::cerr << "Failed to set client socket to non-blocking mode."
              << std::endl;
    exit(-1);
  }
}

// PressureClient

PressureClient::PressureClient(int serverport, const char* str_server_ip,
                               int id, int message_size, int max_test_time)
    : Client(serverport, str_server_ip, id),
      message_size_(message_size),
      max_test_time_(max_test_time) {
  test_message_ = nullptr;
  test_time_ = 1;
}
PressureClient::~PressureClient() {
  if (test_message_ != nullptr) {
    delete test_message_;
    test_message_ = nullptr;
  }
}

ssize_t PressureClient::ReadData(int fd) {
  // 接收
  ssize_t nrecv = -1;
  char* ptr_recv_start_ = test_message_->GetPtrRecvStart();
  char* ptr_recv_end_ = test_message_->GetPtrRecvEnd();
  if (ptr_recv_end_ > ptr_recv_start_) {
    if ((nrecv = recv(fd, ptr_recv_start_, ptr_recv_end_ - ptr_recv_start_,
                      MSG_NOSIGNAL)) < 0) {
      if (errno != EWOULDBLOCK) {
        std::cerr << "压力发生器接收出错:" << strerror(errno) << std::endl;
      }
    } else if (nrecv == 0) {
      // 服务器断开连接
      std::cerr << "中继服务器与压力发生器断连" << std::endl;
    } else {
#ifdef DEBUG
      std::cerr << "压力发生器接收" << nrecv << std::endl;
#endif
      test_message_->MoveRecvStart(nrecv);
      if (test_message_->GetPtrRecvStart() == ptr_recv_end_) {
        if (test_message_->CheckMessage()) {
#ifdef DEBUG
          std::cerr << "压力发生器接收到正确的回射信息" << std::endl;
#endif
          // 计数
          extern int count;
          ++count;

          // 计时
          auto end = std::chrono::high_resolution_clock::now();
          extern std::chrono::duration<double, std::milli> duration;
          end = std::chrono::high_resolution_clock::now();
          duration += end - start_;

          //  重置测试信息
          if (test_message_ != nullptr) {
            delete test_message_;
            test_message_ = nullptr;
          }
          if (max_test_time_ != -1 && test_time_ >= max_test_time_) {
#ifdef DEBUG
            std::cerr << id_ << "到达最大测试次数" << std::endl;
#endif
            return 0;
          }
          test_message_ =
              new TestMessage(id_, id_ + 1, test_time_++, message_size_);
          start_ = std::chrono::high_resolution_clock::now();
        } else {
          std::cerr << "压力发生器接收到错误的回射信息" << std::endl;
          exit(-1);
        }
      }
    }
  }
  return nrecv;
}
bool PressureClient::SendData(int fd) {
  // 发送
  ssize_t nsend;
  char* ptr_send_start_;
  char* ptr_send_end_;
  if (is_send_id_) {
    // 已经发送完自己的id
    ptr_send_start_ = test_message_->GetPtrSendStart();
    ptr_send_end_ = test_message_->GetPtrSendEnd();
  } else {
    // 还没发送完自己的id
    ptr_send_start_ = ptr_send_id_;
    ptr_send_end_ = ptr_send_start_ + sizeof(id_);
  }

  if (ptr_send_end_ - ptr_send_start_ > 0) {
    if ((nsend = send(fd, ptr_send_start_, ptr_send_end_ - ptr_send_start_,
                      MSG_NOSIGNAL)) < 0) {
      if (errno != EWOULDBLOCK) {
        std::cerr << "压力发生器发送出错:" << strerror(errno) << std::endl;
      }
    } else {
#ifdef DEBUG
      std::cerr << "压力发生器发送" << nsend << std::endl;
#endif
      if (is_send_id_) {
        test_message_->MoveSendStart(nsend);
        return test_message_->GetPtrSendEnd() ==
               test_message_->GetPtrSendStart();
      } else {
        ptr_send_id_ += nsend;
        if (ptr_send_id_ == reinterpret_cast<char*>(&id_) + sizeof(id_)) {
          // 发送完id
          is_send_id_ = true;
          //  产生测试信息
          test_message_ =
              new TestMessage(id_, id_ + 1, test_time_++, message_size_);
          start_ = std::chrono::high_resolution_clock::now();
        }
      }
    }
  }
  return false;
}

// EchoServerClient
EchoServerClient::EchoServerClient(int serverport, const char* str_server_ip,
                                   int id)
    : Client(serverport, str_server_ip, id) {
  buffer_ = new Buffer();
  // 将id置于缓冲区中发送给服务器
  memcpy(buffer_->GetBuffer(), &id_, sizeof(id));
  ssize_t nrecv = sizeof(id);
  buffer_->MoveRecvStart(nrecv);
  buffer_->UpdateSendEnd();
  rest_data_len_ = 0;
  has_dst_ = -1;
}

EchoServerClient::~EchoServerClient() {
  if (buffer_) {
    delete buffer_;
    buffer_ = nullptr;
  }
}
ssize_t EchoServerClient::ReadData(int fd) {
  ssize_t nrecv = -1;
  // 接收
  if (buffer_->GetRecvEnd() > buffer_->GetRecvStart()) {
    if ((nrecv = recv(fd, buffer_->GetRecvStart(),
                      buffer_->GetRecvEnd() - buffer_->GetRecvStart(),
                      MSG_NOSIGNAL)) < 0) {
      if (errno != EWOULDBLOCK) {
        std::cerr << "回射服务器接收错误" << strerror(errno) << std::endl;
      }
    } else {
      buffer_->MoveRecvStart(nrecv);
#ifdef DEBUG
      std::cerr << "回射服务器接收到 " << nrecv << " 字节" << std::endl;
#endif
      if (has_dst_) {
        // 读取的是数据
        buffer_->UpdateSendEnd();
      }
      if (buffer_->IsRecvFinish()) {
        if (!has_dst_) {
          // 接收完报头
          Header header;
          memcpy(&header, buffer_->GetBuffer(), sizeof(Header));
#ifdef DEBUG
          std::cerr << "回射服务器收到了来自 " << header.src_id_
                    << " 的消息，长度:" << header.data_len_ << std::endl;
#endif
          std::swap(header.dst_id_, header.src_id_);
          memcpy(buffer_->GetBuffer(), &header, sizeof(Header));
          rest_data_len_ = header.data_len_;
          has_dst_ = true;
          // 开始回射
          buffer_->UpdateSendEnd();
          // 开始接收数据
          buffer_->UpdateRecvEnd(rest_data_len_);
        } else if (rest_data_len_) {
          // 读数据至上限，还有剩余数据没读完

#ifdef DEBUG
          std::cerr << "回射服务器读数据至上限，还有剩余数据没读完"
                    << std::endl;
#endif
          buffer_->UpdateRecvEnd(rest_data_len_);
        }
      }
    }
  }
  return nrecv;
}
bool EchoServerClient::SendData(int fd) {
  ssize_t nsend;
  if (buffer_->GetSendEnd() > buffer_->GetSendStart()) {
    if ((nsend = send(fd, buffer_->GetSendStart(),
                      buffer_->GetSendEnd() - buffer_->GetSendStart(),
                      MSG_NOSIGNAL)) < 0) {
      if (errno != EWOULDBLOCK) {
        std::cerr << "回射服务器发送出错" << strerror(errno) << std::endl;
      }
    } else {
#ifdef DEBUG
      std::cerr << "回射服务器发送了 " << nsend << " 字节" << std::endl;
#endif
      buffer_->MoveSendStart(nsend);
      // 接收数据受发送数据所限制
      if (rest_data_len_) {
        buffer_->UpdateRecvEnd(rest_data_len_);
      }
      if (buffer_->IsSendFinish()) {
#ifdef DEBUG
        std::cerr << "回射服务器发送完了" << std::endl;
#endif
        buffer_->UpdateSendEnd();
        if (buffer_->IsSendFinish() && buffer_->IsRecvFinish()) {
          // 转发完成
#ifdef DEBUG
          std::cerr << id_ - 1 << "消息回射完了" << std::endl;
#endif
          has_dst_ = false;
          buffer_->InitPtr();
        }
      }
    }
    return buffer_->IsSendFinish();
  }
  return false;
}