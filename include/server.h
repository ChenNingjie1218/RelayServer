#ifndef SERVER_H_
#define SERVER_H_
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <map>
#include <mutex>

#include "client-connected.h"
#include "param.h"
class Server {
 public:
  Server() = delete;
  Server(int port, int length_of_queue_of_listen = 100,
         const char *str_bound_ip = NULL);
  virtual ~Server();
  void Run();

 protected:
  virtual void ServerFunction(int &connected_socket) = 0;
  int port_;                       // 端口号
  int length_of_queue_of_listen_;  // 监听队列最大长度
  char *str_bound_ip_;             // 绑定的ip
};

class RelayServer : public Server {
 public:
  RelayServer(int port, int length_of_queue_of_listen = 100,
              const char *str_bound_ip = NULL);
  ~RelayServer() = default;

 protected:
  // 转发功能
  void ServerFunction(int &listen_socket) override;

  std::mutex accept_mutex_;  // accept的锁
  ClientConnected
      *fd_to_client_[MAX_CLIENT_NUM];  // 散列表映射fd与所关联的客户端
  std::map<int, ClientConnected *> id_to_client_;
};
#endif