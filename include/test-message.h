#ifndef TEST_MESSAGE_H_
#define TEST_MESSAGE_H_
#include "message.h"
class TestMessage {
 public:
  TestMessage() = default;
  TestMessage(int src_id, int dst_id, int test_id, int message_size,
              int socket);
  ~TestMessage();

  /*
   * @ret 0: success
   *    -1: fail
   *      1: 连接中断
   */
  int Test();

 private:
  Message* origin_message_;
  Message* back_message_;
  char* ptr_send_start_;
  char* ptr_send_end_;
  char* ptr_recv_start_;
  char* ptr_recv_end_;
  int socket_;
};
#endif