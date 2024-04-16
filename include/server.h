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

#include <chrono>
#include <mutex>
#include <ratio>

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
  void ServerFunction(int &connected_socket) override;
  static void thread_main(int &listen_socket, RelayServer *obj);
  int id_to_fd_[20010];             // 散列表映射id与fd
  int fd_to_id_[20010];             // 散列表映射fd与id
  int dst_id_[20010];               // 散列表目的id
  int src_id_[20010];               // 散列表源id
  bool is_first_connected_[20010];  // 刚连接上
  char *buf[20010];                 // 暂存数据缓冲区
  static ssize_t buffer_size;       // 暂存数据缓冲区的大小

  char *ptr_send_start[20010];  // 发送的起始指针
  char *ptr_recv_start[20010];  // 接收的起始指针 同时也是发送的末尾指针
  char *ptr_recv_end[20010];  // 接收的末尾指针

  static int thread_num_;    // 线程数
  std::mutex accept_mutex_;  // accept的锁
};
#endif