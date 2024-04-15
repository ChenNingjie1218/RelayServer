#include "server.h"
int main() {
  RelayServer RelayServer(5000);
  RelayServer.Run();
  return 0;
}