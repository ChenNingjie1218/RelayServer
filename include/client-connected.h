#ifndef CLIENT_CONNECTED_H_
#define CLIENT_CONNECTED_H_
#include <map>

#include "buffer.h"

// 连接上的客户端
class ClientConnected {
 public:
  ClientConnected() = default;
  ClientConnected(int& fd);
  ~ClientConnected();
  // 设置id
  void SetId(int id) { id_ = id; }

  // 获取id
  int GetId() { return id_; }

  // 设置源客户端
  void SetSrcClient(ClientConnected* src_client) { src_client_ = src_client; }

  // 从该客户端接收数据
  ssize_t RecvData(std::map<int, ClientConnected*>& id_to_client);

  // 向该客户端发送数据
  void SendData();

  // 获取缓冲区
  Buffer* GetBuffer() { return buffer_; }

  // 重置目的客户端
  void ResetDstClient();

  // 获取还未接收的剩余数据长度
  int GetRestDataLen() { return rest_data_len_; }

  // 获取目的客户端的id
  int GetDstId() { return dst_id_; }

 private:
  int id_;                       // 该客户端的id
  int fd_;                       // 所用的fd
  int dst_id_;                   // 目的客户端的id
  ClientConnected* src_client_;  // 源客户端 用于从该客户端的缓冲区接收数据
  int rest_data_len_;  // 该客户端还未分配缓冲区的剩余数据长度
  Buffer* buffer_;     // 接收该客户端发送数据的数据缓冲区
};
#endif