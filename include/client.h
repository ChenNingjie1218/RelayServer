#ifndef CLIENT_H_
#define CLIENT_H_
#include <arpa/inet.h>
#include <memory.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <iostream>

#include "buffer.h"
#include "message.h"
#include "param.h"
#include "test-message.h"
class Client {
 public:
  Client() = delete;
  Client(int serverport, const char* str_server_ip, int id);
  virtual ~Client();
  int Run();
  virtual ssize_t ReadData(int fd) = 0;
  virtual bool SendData(int fd) = 0;
  void SetConnected(int& fd);
  bool IsConnected() { return is_connected_; }
  int GetId() { return id_; }

 protected:
  int serverport_;
  const char* str_server_ip_;
  int id_;
  bool is_send_id_;    // 是否发送过id
  char* ptr_send_id_;  // 非阻塞发送id的起始指针
  bool is_connected_ = false;
};

class PressureClient : public Client {
 public:
  PressureClient(int serverport, const char* str_server_ip, int id,
                 int message_size, int max_test_time);
  ~PressureClient();
  ssize_t ReadData(int fd) override;
  bool SendData(int fd) override;

 private:
  int message_size_;
  TestMessage* test_message_;  // 测试报文
  int test_time_;              // 测试次数
  int max_test_time_;          // 最大测试次数
  std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

class EchoServerClient : public Client {
 public:
  EchoServerClient(int serverport, const char* str_server_ip, int id);
  ~EchoServerClient();
  ssize_t ReadData(int fd) override;
  bool SendData(int fd) override;

 private:
  Buffer* buffer_;     // 缓冲区
  bool has_dst_;       // 是否已经知道dst
  int rest_data_len_;  // 未分配缓冲区的剩余数据长度
};

#endif