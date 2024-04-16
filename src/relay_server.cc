#include <chrono>
#include <csignal>
#include <iostream>

#include "server.h"
int count = 0;
std::chrono::system_clock::time_point start;
void signalHandler(int signal) {
  auto end = std::chrono::system_clock::now();
  std::chrono::duration<double> duration =
      std::chrono::duration_cast<std::chrono::duration<double>>(end - start);
  std::cout << count << " 平均每秒转发 " << count / duration.count()
            << " 个报文" << std::endl;
  exit(signal);
}

int main() {
  signal(SIGINT, signalHandler);
  RelayServer RelayServer(5000);
  start = std::chrono::system_clock::now();
  RelayServer.Run();
  return 0;
}