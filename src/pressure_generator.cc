#include <csignal>
#include <thread>
#include <vector>

#include "client.h"

// 记录延时
std::chrono::duration<double, std::milli> duration;
int count;

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
  int num_thread = 2 * num_sessions;
  std::vector<std::thread*> v_thread;
  for (int i = 0; i < num_thread; ++i) {
    int id = i;
    auto one_thread = new std::thread(
        [&message_size](int i) {
          // MyClient client(5000, "116.205.224.19", i, message_size);
          MyClient client(5000, "127.0.0.1", i, message_size);
          client.Run();
        },
        id);
    v_thread.push_back(one_thread);
  }
  for (auto& t : v_thread) {
    t->join();
  }
  return 0;
}