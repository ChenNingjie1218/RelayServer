#ifndef TEST_MESSAGE_H_
#define TEST_MESSAGE_H_
#include <cstring>

#include "message.h"
class TestMessage {
 public:
  TestMessage() = default;
  TestMessage(int src_id, int dst_id, int test_id, int message_size);
  ~TestMessage();
  char* GetPtrSendStart() { return ptr_send_start_; }
  char* GetPtrRecvStart() { return ptr_recv_start_; }
  char* GetPtrSendEnd() { return ptr_send_end_; }
  char* GetPtrRecvEnd() { return ptr_recv_end_; }
  void MoveSendStart(ssize_t& size) { ptr_send_start_ += size; }
  void MoveRecvStart(ssize_t& size) { ptr_recv_start_ += size; }
  // 测试回射回来的数据是否正确
  bool CheckMessage() {
    return strncmp(origin_message_->data, back_message_->data,
                   origin_message_->header.data_len_) == 0;
  }

 private:
  Message* origin_message_;
  Message* back_message_;
  char* ptr_send_start_;
  char* ptr_send_end_;
  char* ptr_recv_start_;
  char* ptr_recv_end_;
};
#endif