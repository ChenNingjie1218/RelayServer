#include <sys/epoll.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
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
void CreateClient(int thread_id, int num_sessions, int (*epoll_fd)[THREAD_NUM],
                  Client** fd_to_client, const int message_size) {
  epoll_event event{};
  // 客户端与服务器连接 并将其加入到epoll 实例中
  int per_sessions = num_sessions / THREAD_NUM;
  if (!per_sessions) {
    per_sessions = 1;
  }
  int max_sessions = per_sessions * (thread_id + 1);
  if (max_sessions < num_sessions && thread_id == THREAD_NUM - 1) {
    max_sessions = num_sessions;
  }
  if (max_sessions > num_sessions) {
    // 已经够了
    return;
  }
  // std::cerr << "线程" << thread_id << "创建"
  //           << 2 * (max_sessions - thread_id * per_sessions) << "个连接"
  //           << std::endl;
  for (int i = thread_id * per_sessions; i < max_sessions; ++i) {
    // int test_time = 10 + 40.0 * rand() / RAND_MAX;
    int test_time = -1;
    // std::cerr << "测试次数:" << test_time << std::endl;
    // ---- 压力发生客户端 ----
    PressureClient* onePressureClient = new PressureClient(
        5000, "116.205.224.19", 2 * i, message_size, test_time);
    // new PressureClient(5000, "127.0.0.1", 2 * i, message_size, test_time);

    // 将新的连接套接字添加到 epoll 实例中
    int Pressure_fd = onePressureClient->Run();
    event.events =
        EPOLLIN | EPOLLOUT | EPOLLERR;  // 监听可读可写事件，水平触发模式
    event.data.fd = Pressure_fd;
    if (epoll_ctl(epoll_fd[0][thread_id], EPOLL_CTL_ADD, Pressure_fd, &event) ==
        -1) {
      std::cerr << "Failed to add client socket to epoll." << std::endl;
      exit(-1);
    }
    fd_to_client[Pressure_fd] = onePressureClient;

    // ---- 回射客户端 ----
    EchoServerClient* oneEchoServerClient =
        new EchoServerClient(5000, "116.205.224.19", 2 * i + 1);
    // new EchoServerClient(5000, "127.0.0.1", 2 * i + 1);
    // 将新的连接套接字添加到 epoll 实例中
    int EchoServer_fd = oneEchoServerClient->Run();
    // 这里回射端一开始也监听可写事件是因为要发送id
    event.events =
        EPOLLIN | EPOLLOUT | EPOLLERR;  // 监听可读可写事件，水平触发模式
    event.data.fd = EchoServer_fd;
    if (epoll_ctl(epoll_fd[1][thread_id], EPOLL_CTL_ADD, EchoServer_fd,
                  &event) == -1) {
      std::cerr << "Failed to add client socket to epoll." << std::endl;
      exit(-1);
    }
    fd_to_client[EchoServer_fd] = oneEchoServerClient;
  }
}
/*
 * @brief: 线程主函数
 * @param: type: 线程类型，0表示压力发生端线程，1表示回射端线程
 */
void ThreadMain(int type, int (*epoll_fd)[THREAD_NUM], int thread_id,
                int num_sessions, Client** fd_to_client, int& n_client) {
  // 创建事件数组用于存储触发的事件
  epoll_event events[MAX_EVENTS];
  while (1) {
    // 等待事件触发
    int num_events =
        epoll_wait(epoll_fd[type][thread_id], events, MAX_EVENTS, -1);
    if (num_events == -1) {
      std::cerr << "Failed to wait for events." << std::endl;
      exit(-1);
    }
    // std::cerr << "num_events: " << num_events << std::endl;
    for (int i = 0; i < num_events; ++i) {
      Client* client = fd_to_client[events[i].data.fd];
      if (client == nullptr) {
        continue;
      }
      int connected_socket = events[i].data.fd;

      if (events[i].events & EPOLLERR) {
        int error;
        socklen_t error_len = sizeof(int);
        getsockopt(connected_socket, SOL_SOCKET, SO_ERROR, &error, &error_len);
        std::cerr << "EPOLLERR error:" << error << " " << strerror(error)
                  << std::endl;
        if (error == ETIMEDOUT && !client->IsConnected()) {
          // connect超时
          // epoll中删除原来的fd
          if (epoll_ctl(epoll_fd[type][thread_id], EPOLL_CTL_DEL,
                        connected_socket, nullptr) == -1) {
            std::cerr << "Failed to remove client socket from epoll."
                      << std::endl;
            exit(-1);
          }
          // 取消原来的映射
          close(connected_socket);
          fd_to_client[connected_socket] = nullptr;
          std::cerr << client->GetId() << "连接超时，进行重连。" << std::endl;
          // 重连新fd
          connected_socket = client->Run();
          // 重新映射
          fd_to_client[connected_socket] = client;
          // 重新添入epoll
          epoll_event event{};
          event.events =
              EPOLLIN | EPOLLOUT | EPOLLERR;  // 监听可读可写事件，水平触发模式
          event.data.fd = connected_socket;
          if (epoll_ctl(epoll_fd[type][thread_id], EPOLL_CTL_ADD,
                        connected_socket, &event) == -1) {
            std::cerr << "Failed to add client socket to epoll." << std::endl;
            exit(-1);
          }
        } else {
          //断开连接
          if (epoll_ctl(epoll_fd[type][thread_id], EPOLL_CTL_DEL,
                        connected_socket, nullptr) == -1) {
            std::cerr << "Failed to remove client socket from epoll."
                      << std::endl;
            exit(-1);
          }
          if (client != nullptr) {
            delete client;
            client = nullptr;
          }
          --n_client;
          if (n_client == 0) {
            std::cerr << "所有客户端断连" << std::endl;
            exit(-1);
          }
          fd_to_client[connected_socket] = nullptr;
          close(connected_socket);
        }
        continue;
      }

      if (!client->IsConnected()) {
        client->SetConnected(connected_socket);
      }

      if (events[i].events & EPOLLIN) {
        // 可读事件
        ssize_t nrecv = client->ReadData(connected_socket);
        if (nrecv > 0 && type) {
          // 回射端接收了数据 添加可写监听
          epoll_event event{};
          event.events = EPOLLIN | EPOLLOUT;  // 监听可读事件,水平触发
          event.data.fd = connected_socket;
          if (epoll_ctl(epoll_fd[type][thread_id], EPOLL_CTL_MOD,
                        connected_socket, &event) == -1) {
            std::cerr << "Failed to change epoll mod." << strerror(errno)
                      << std::endl;
            exit(-1);
          }
        } else if (nrecv == 0) {
          //断开连接
          if (epoll_ctl(epoll_fd[type][thread_id], EPOLL_CTL_DEL,
                        connected_socket, nullptr) == -1) {
            std::cerr << "Failed to remove client socket from epoll."
                      << std::endl;
            exit(-1);
          }
          if (client != nullptr) {
            delete client;
            client = nullptr;
          }
          --n_client;
          if (n_client == 0) {
            std::cerr << "所有客户端断连" << std::endl;
            exit(-1);
          }
          fd_to_client[connected_socket] = nullptr;
          close(connected_socket);
          continue;
        }
      }

      if (events[i].events & EPOLLOUT) {
        // 可写事件
        if (client->SendData(connected_socket) &&
            type) {  // type写后面，为了让压力端发数据
          // 回射端写完了可写数据 关闭可写监听
          epoll_event event{};
          event.events = EPOLLIN;  // 监听可读事件,水平触发
          event.data.fd = connected_socket;
          if (epoll_ctl(epoll_fd[type][thread_id], EPOLL_CTL_MOD,
                        connected_socket, &event) == -1) {
            std::cerr << "Failed to change epoll mod." << std::endl;
            exit(-1);
          }
        }
      }
    }
  }
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
  // const int message_size = atoll(argv[2]);
  const int message_size = atoll(argv[2]) * 1024;
  std::cout << "产生会话数为：" << num_sessions << ", 报文大小为：";
  if (message_size >= 1024) {
    std::cout << atoi(argv[2]) << "KB" << std::endl;
  } else {
    std::cout << message_size << "B" << std::endl;
  }

  if (message_size > MAX_MESSAGE_LEN) {
    std::cerr << "报文大小太大，暂只支持" << MAX_MESSAGE_LEN / 1024 << "KB"
              << std::endl;
    return -1;
  }

  // 创建 epoll 实例
  int epoll_fd[2][THREAD_NUM];
  for (int j = 0; j < THREAD_NUM; ++j) {
    epoll_fd[0][j] = epoll_create1(0);  // 压力发生端
    epoll_fd[1][j] = epoll_create1(0);  // 回射服务端
    if (epoll_fd[0][j] == -1 || epoll_fd[1][j] == -1) {
      std::cerr << "Failed to create epoll instance." << std::endl;
      exit(-1);
    }
  }

  Client* fd_to_client[MAX_CLIENT_NUM] = {nullptr};
  std::vector<std::thread> v_thread;
  for (int j = 0; j < THREAD_NUM; ++j) {
    v_thread.push_back(std::thread(CreateClient, j, num_sessions, epoll_fd,
                                   fd_to_client, message_size));
  }
  for (auto& t : v_thread) {
    t.join();
  }
  std::cerr << "所有客户端调用connect()成功" << std::endl;
  int n_client = num_sessions;
  std::vector<std::thread> v_pressure_thread;
  std::vector<std::thread> v_echo_thread;
  for (int j = 0; j < THREAD_NUM; ++j) {
    v_pressure_thread.push_back(std::thread(ThreadMain, 0, epoll_fd, j,
                                            num_sessions, fd_to_client,
                                            std::ref(n_client)));
    v_echo_thread.push_back(std::thread(ThreadMain, 1, epoll_fd, j,
                                        num_sessions, fd_to_client,
                                        std::ref(n_client)));
  }
  for (auto& t : v_pressure_thread) {
    t.join();
  }
  for (auto& t : v_echo_thread) {
    t.join();
  }
  return 0;
}