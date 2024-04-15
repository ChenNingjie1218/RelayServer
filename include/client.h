#ifndef CLIENT_H_
#define CLIENT_H_
#include <arpa/inet.h>
#include <memory.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
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
  MyClient(int serverport, const char* str_server_ip, int id);
  ~MyClient() = default;

 protected:
  void ClientFunction(int connected_socket) override;
  int id_;
};
#endif