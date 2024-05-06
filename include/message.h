#ifndef MESSAGE_H_
#define MESSAGE_H_
#include <string>

#include "param.h"
struct Header {
  int src_id_;
  int dst_id_;
  int data_len_;
};

struct Message {
  Header header;
  char data[MAX_MESSAGE_LEN];
};
#endif