#ifndef MESSAGE_H_
#define MESSAGE_H_
#include <string>

struct Header {
  int src_id_;
  int dst_id_;
  int data_len_;
  int origin_id_;
};

struct Message {
  Header header;
  char data[11 * 1024];
};
#endif