#include "client-connected.h"

#include <sys/socket.h>

#include <cerrno>
#include <cstring>
#include <iostream>

#include "buffer.h"
#include "message.h"
#include "param.h"
// 记录转发数量
extern int relay_count;
ClientConnected::ClientConnected(int& fd)
    : id_(-1), fd_(fd), dst_id_(-1), src_client_(nullptr), rest_data_len_(0) {
  buffer_ = new Buffer();
}
ClientConnected::~ClientConnected() { delete buffer_; }

// 从该客户端接收数据
ssize_t ClientConnected::RecvData(
    std::map<int, ClientConnected*>& id_to_client) {
  ssize_t nrecv;
  if ((nrecv = recv(fd_, buffer_->GetRecvStart(),
                    buffer_->GetRecvEnd() - buffer_->GetRecvStart(),
                    MSG_NOSIGNAL))  // MSG_NOSIGNAL 防止SIGPIP终止应用程序
      < 0) {
    if (errno != EWOULDBLOCK) {
      std::cerr << "接收错误:" << strerror(errno) << std::endl;
    }
  } else if (nrecv > 0) {
#ifdef DEBUG
    std::cerr << "服务器接收到" << nrecv << "字节数据" << std::endl;
#endif
    buffer_->MoveRecvStart(nrecv);
    if (dst_id_ != -1) {
      // 读取的是数据
      buffer_->UpdateSendEnd();
    }
    if (buffer_->IsRecvFinish()) {
      if (id_ == -1) {
        // 获取id
        memcpy(&id_, buffer_->GetBuffer(), sizeof(int));
        buffer_->InitPtr();
#ifdef DEBUG
        std::cerr << "服务器建立新连接，id为：" << id_ << std::endl;
#endif
        //  检查是否有对端客户端
        for (auto connected_client_iter = id_to_client.begin();
             connected_client_iter != id_to_client.end();
             ++connected_client_iter) {
          if (connected_client_iter->second->GetDstId() == id_) {
            SetSrcClient(connected_client_iter->second);
            break;
          }
        }
        id_to_client[id_] = this;
      } else if (dst_id_ == -1) {
        // 读完报头
        Header header;
        memcpy(&header, buffer_->GetBuffer(), sizeof(Header));
        dst_id_ = header.dst_id_;
        rest_data_len_ = header.data_len_;
        // 可以发送数据了
        if (id_to_client.find(dst_id_) != id_to_client.end()) {
          id_to_client[dst_id_]->SetSrcClient(this);
        }
        // 开始转发
        buffer_->UpdateSendEnd();

        // 开始接收数据
        buffer_->UpdateRecvEnd(rest_data_len_);

#ifdef DEBUG
        std::cerr << "服务器收到来自" << header.src_id_ << "发给"
                  << header.dst_id_ << "的长为" << header.data_len_ << "信息"
                  << std::endl;
#endif
      } else if (rest_data_len_) {
        // 读数据至上限，还有剩余数据没读完

#ifdef DEBUG
        std::cerr << "读数据至上限，还有剩余数据没读完" << std::endl;
#endif
        buffer_->UpdateRecvEnd(rest_data_len_);
      }
    }
  }
  return nrecv;
}

// 向该客户端发送数据
void ClientConnected::SendData() {
  if (src_client_ != nullptr) {
    // 源端在线 从源端的缓冲区读取数据
    ssize_t nsend;
    Buffer* src_buffer = src_client_->GetBuffer();
    if (src_buffer->GetSendEnd() > src_buffer->GetSendStart()) {
      if ((nsend = send(fd_, src_buffer->GetSendStart(),
                        src_buffer->GetSendEnd() - src_buffer->GetSendStart(),
                        MSG_NOSIGNAL)) < 0) {
        if (errno != EWOULDBLOCK) {
          std::cerr << "发送出错:" << strerror(errno) << std::endl;
        }
      } else {
#ifdef DEBUG
        std::cerr << "服务器转发" << nsend << "字节" << std::endl;
#endif
        src_buffer->MoveSendStart(nsend);
        // 源端接收数据受该端发送数据所限制
        int& rest_data_len = src_client_->GetRestDataLen();
        if (rest_data_len) {
          src_buffer->UpdateRecvEnd(rest_data_len);
        }
        if (src_buffer->IsSendFinish()) {
          src_buffer->UpdateSendEnd();
          if (src_buffer->IsSendFinish() && src_buffer->IsRecvFinish()) {
            // 转发完成

#ifdef DEBUG
            std::cerr << src_client_->GetId() << " 的消息转发完了" << std::endl;
#endif

            src_client_->ResetDstClient();
            ++relay_count;
            src_client_ = nullptr;
          }
        }
      }
    }
  }
}

// 重置目的客户端
void ClientConnected::ResetDstClient() {
  dst_id_ = -1;
  buffer_->InitPtr();
}