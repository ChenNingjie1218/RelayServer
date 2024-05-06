#include "buffer.h"

#include <iostream>

#include "message.h"
#include "param.h"
ssize_t Buffer::buffer_size_ = 4 * 1024;  // 暂存数据缓冲区的大小

Buffer::Buffer() {
  if (sizeof(Header) > buffer_size_) {
    std::cerr << "Header 比 buffer_size 大，sizeof(Header):" << sizeof(Header)
              << std::endl;
    buffer_size_ = sizeof(Header);
  }
  buf_ = new char[buffer_size_];
  InitPtr(true);
}

Buffer::~Buffer() { delete[] buf_; }

// 初始化指针 等待读取Header的状态
void Buffer::InitPtr(bool is_first) {
  ptr_recv_start_ = buf_;
  if (is_first) {
    // 第一次准备接收id

    // #ifdef DEBUG
    //     std::cout << "第一次准备接收id" << std::endl;
    // #endif
    ptr_recv_end_ = buf_ + sizeof(int);
  } else {
    // 准备接收报头

    // #ifdef DEBUG
    //     std::cout << "准备接收报头" << std::endl;
    // #endif
    ptr_recv_end_ = buf_ + sizeof(Header);
  }
  ptr_send_start_ = buf_;
  ptr_send_end_ = buf_;
}

// 是否接受完指定数据
bool Buffer::IsRecvFinish() { return ptr_recv_start_ == ptr_recv_end_; }

// 是否发送完指定数据
bool Buffer::IsSendFinish() { return ptr_send_start_ == ptr_recv_end_; }

// 更新ptr_recv_end
void Buffer::UpdateRecvEnd(int& rest_size) {
  if (ptr_recv_start_ == buf_ + buffer_size_) {
    // 已经位于缓冲区尾部 将其置为首位置
    ptr_recv_end_ = buf_;
    ptr_recv_start_ = buf_;
  }

  if (ptr_recv_end_ <= ptr_send_start_ &&
      ptr_recv_end_ + rest_size > ptr_send_start_) {
    // 超过发送区 要等待发送完
    rest_size -= ptr_send_start_ - ptr_recv_end_;
    ptr_recv_end_ = ptr_send_start_;
  } else if (ptr_recv_end_ + rest_size > buf_ + buffer_size_) {
    // 超过缓冲区 只有可能是接收超长数据时
    rest_size -= buf_ + buffer_size_ - ptr_recv_end_;
    ptr_recv_end_ = buf_ + buffer_size_;
  } else {
    ptr_recv_end_ += rest_size;
    rest_size = 0;
  }
}

// 更新ptr_send_end
void Buffer::UpdateSendEnd() {
  if (ptr_send_end_ < ptr_recv_start_) {
    ptr_send_end_ = ptr_recv_start_;
  } else if (ptr_send_start_ == buf_ + buffer_size_) {
    // 两个指针均到达缓冲区末端
    ptr_send_end_ = ptr_recv_start_;
    ptr_send_start_ = buf_;
  }
}