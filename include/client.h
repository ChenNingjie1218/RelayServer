#ifndef CLIENT_H_
#define CLIENT_H_
#include <arpa/inet.h>
#include <memory.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <iostream>

#include "message.h"
#include "param.h"
class Client {
 public:
  Client() = delete;
  Client(int serverport, const char* str_server_ip);
  virtual ~Client();
  void Run();

 protected:
  virtual void ClientFunction(int connected_socket) = 0;
  int serverport_;
  const char* str_server_ip_;
};

class MyClient : public Client {
 public:
  MyClient(int serverport, const char* str_server_ip, int id, int message_size);
  ~MyClient() = default;

 protected:
  void ClientFunction(int connected_socket) override;
  // 担任压力产生器
  void PressureGenerator(int connected_socket);
  // 担任回射服务器
  void EchoServer(int connected_socket);

  int id_;
  int message_size_;
};
#endif