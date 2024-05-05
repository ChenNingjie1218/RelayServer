#include "test-message.h"

#include <sys/socket.h>

#include <cstring>
#include <iostream>
#include <sstream>

#include "message.h"
#include "param.h"
TestMessage::TestMessage(int src_id, int dst_id, int test_id, int message_size,
                         int socket)
    : socket_(socket) {
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
  delete origin_message_;
  delete back_message_;
}

/*
 * @ret 0: success
 *    -1: fail
 *      1: 连接中断
 */
int TestMessage::Test() {
  ssize_t nsend, nrecv;
  while (ptr_recv_start_ != ptr_recv_end_) {
    // 接收
    if ((nrecv = recv(socket_, ptr_recv_start_, ptr_recv_end_ - ptr_recv_start_,
                      MSG_NOSIGNAL)) < 0) {
      if (errno != EWOULDBLOCK) {
        std::cerr << "压力发生器接收出错, errno = " << errno << std::endl;
      }
    } else if (nrecv == 0) {
      // 服务器断开连接
      std::cerr << "中继服务器与压力发生器断连" << std::endl;
      return 1;
    } else {
      ptr_recv_start_ += nrecv;
    }

    // 发送
    if ((nsend = send(socket_, ptr_send_start_, ptr_send_end_ - ptr_send_start_,
                      MSG_NOSIGNAL)) < 0) {
      if (errno != EWOULDBLOCK) {
        std::cerr << "压力发生器发送出错, errno = " << errno << std::endl;
      }
    } else {
      ptr_send_start_ += nsend;
    }
  }
#ifdef DEBUG
  std::cerr << "回射信息为:" << back_message_->data << std::endl;
#endif
  if (strncmp(origin_message_->data, back_message_->data,
              origin_message_->header.data_len_) == 0) {
    return 0;
  } else {
    return -1;
  }
}