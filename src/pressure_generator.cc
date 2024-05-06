#include <sys/epoll.h>

#include <chrono>
#include <csignal>
#include <thread>
#include <vector>

#include "client.h"
#include "param.h"

// 记录延时
std::chrono::duration<double, std::milli> duration;
int count = 0;

void signalHandler(int signal) {
  std::cout << "平均延时：" << duration.count() / count << " ms" << std::endl;
  exit(signal);
}

/*
  第一个参数为对话数
  第二个参数为报文大小
*/
int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "参数错误" << std::endl;
    return -1;
  }
  signal(SIGINT, signalHandler);
  int num_sessions = atoi(argv[1]);
  const int message_size = atoi(argv[2]);
  std::cout << "产生会话数为：" << num_sessions << ", 报文大小为："
            << message_size << "B" << std::endl;
  if (message_size > MAX_MESSAGE_LEN) {
    std::cerr << "报文大小太大，暂只支持" << MAX_MESSAGE_LEN / 1024 << "KB"
              << std::endl;
    return -1;
  }

  // 创建 epoll 实例
  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    std::cerr << "Failed to create epoll instance." << std::endl;
    exit(-1);
  }

  epoll_event event{};

  Client* fd_to_client[MAX_CLIENT_NUM] = {nullptr};

  // 客户端与服务器连接 并将其加入到epoll 实例中
  for (int i = 0; i < num_sessions; ++i) {
    // ---- 压力发生客户端 ----
    PressureClient* onePressureClient =
        new PressureClient(5000, "127.0.0.1", 2 * i, message_size, -1);

    // 将新的连接套接字添加到 epoll 实例中
    int Pressure_fd = onePressureClient->Run();
    event.events = EPOLLIN | EPOLLOUT;  // 监听可读可写事件，水平触发模式
    event.data.fd = Pressure_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, Pressure_fd, &event) == -1) {
      std::cerr << "Failed to add client socket to epoll." << std::endl;
      exit(-1);
    }
    fd_to_client[Pressure_fd] = onePressureClient;

    // ---- 回射客户端 ----
    EchoServerClient* oneEchoServerClient =
        new EchoServerClient(5000, "127.0.0.1", 2 * i + 1);
    // 将新的连接套接字添加到 epoll 实例中
    int EchoServer_fd = oneEchoServerClient->Run();
    event.events = EPOLLIN | EPOLLOUT;  // 监听可读可写事件，水平触发模式
    event.data.fd = EchoServer_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, EchoServer_fd, &event) == -1) {
      std::cerr << "Failed to add client socket to epoll." << std::endl;
      exit(-1);
    }
    fd_to_client[EchoServer_fd] = oneEchoServerClient;
  }

  // 创建事件数组用于存储触发的事件
  epoll_event events[MAX_EVENTS];
  while (1) {
    // 等待事件触发
    int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    if (num_events == -1) {
      std::cerr << "Failed to wait for events." << std::endl;
      exit(-1);
    }
    for (int i = 0; i < num_events; ++i) {
      Client* client = fd_to_client[events[i].data.fd];
      if (client == nullptr) {
        continue;
      }
      int connected_socket = events[i].data.fd;

      if (events[i].events & EPOLLIN) {
        // 可读事件
        ssize_t nrecv = client->ReadData(connected_socket);
        if (nrecv == 0) {
          //断开连接
          if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, connected_socket, nullptr) ==
              -1) {
            std::cerr << "Failed to remove client socket from epoll."
                      << std::endl;
            exit(-1);
          }

          fd_to_client[connected_socket] = nullptr;
          close(connected_socket);
          continue;
        }
      }

      if (events[i].events & EPOLLOUT) {
        // 可写事件
        client->SendData(connected_socket);
      }
    }
  }

  return 0;
}