#include <thread>
#include <vector>

#include "client.h"
/*
  第一个参数为对话数
  第二个参数为报文大小
*/
int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "参数错误" << std::endl;
    return -1;
  }

  int num_sessions = atoi(argv[1]);
  const int message_size = atoi(argv[2]);
  std::cout << "产生会话数为：" << num_sessions << ", 报文大小为："
            << message_size << std::endl;

  int num_thread = 2 * num_sessions;
  for (int i = 0; i < num_sessions; ++i) {
    int id = i;
    auto one_thread = new std::thread(
        [](int i) {
          MyClient client(5000, "127.0.0.1", i);
          client.Run();
        },
        id);
    one_thread->detach();
  }
  pause();
  return 0;
}