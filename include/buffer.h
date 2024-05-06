#ifndef BUFFER_H
#define BUFFER_H
#include <unistd.h>

// 缓冲区类
class Buffer {
 public:
  Buffer();
  ~Buffer();

  // 获取ptr_recv_start
  char *GetRecvStart() { return ptr_recv_start_; }

  // 获取ptr_recv_end
  char *GetRecvEnd() { return ptr_recv_end_; }

  // 获取ptr_send_start
  char *GetSendStart() { return ptr_send_start_; }

  // 获取ptr_send_end
  char *GetSendEnd() { return ptr_send_end_; }

  // 获取buffer的首地址
  char *GetBuffer() { return buf_; }

  // 初始化指针
  void InitPtr(bool is_first = false);

  // 是否接受完指定数据
  bool IsRecvFinish();

  // 是否发送完指定数据
  bool IsSendFinish();

  // ptr_send_start指针后移
  void MoveSendStart(ssize_t &size) { ptr_send_start_ += size; }

  // 更新ptr_send_end
  void UpdateSendEnd();

  // ptr_recv_start指针后移
  void MoveRecvStart(ssize_t &size) { ptr_recv_start_ += size; }

  // 更新ptr_recv_end
  void UpdateRecvEnd(int &rest_size);

 private:
  char *buf_;                   // 暂存数据缓冲区
  static ssize_t buffer_size_;  // 暂存数据缓冲区的大小
  char *ptr_send_start_;        // 发送的起始指针
  char *ptr_send_end_;          // 发送的末尾指针
  char *ptr_recv_start_;        // 接收的起始指针
  char *ptr_recv_end_;          // 接收的末尾指针
};
#endif