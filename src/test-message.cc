#include "test-message.h"

#include <sys/socket.h>

#include <cstring>
#include <iostream>
#include <sstream>

#include "message.h"
#include "param.h"
TestMessage::TestMessage(int src_id, int dst_id, int test_id,
                         int message_size) {
  origin_message_ = new Message();
  origin_message_->header.src_id_ = src_id;
  origin_message_->header.dst_id_ = dst_id;
  std::stringstream ss;
  ss.width(message_size);
  ss.fill('x');
  ss << test_id;
  // ss << src_id << " 的第 " << test_id << " 次测试";
  origin_message_->header.data_len_ = ss.str().length();
  strncpy(origin_message_->data, ss.str().c_str(),
          origin_message_->header.data_len_);
#ifdef DEBUG
  std::cerr << "测试数据长度:" << origin_message_->header.data_len_
            << std::endl;
  std::cerr << "测试数据:" << origin_message_->data << std::endl;
#endif

  ptr_send_start_ = reinterpret_cast<char*>(origin_message_);
  ptr_send_end_ =
      ptr_send_start_ + sizeof(Header) + origin_message_->header.data_len_;

  back_message_ = new Message();
  ptr_recv_start_ = reinterpret_cast<char*>(back_message_);
  ptr_recv_end_ =
      ptr_recv_start_ + sizeof(Header) + origin_message_->header.data_len_;
}

TestMessage::~TestMessage() {
  if (origin_message_ != nullptr) {
    delete origin_message_;
    origin_message_ = nullptr;
  }
  if (back_message_ != nullptr) {
    delete back_message_;
    back_message_ = nullptr;
  }
}