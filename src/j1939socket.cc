#include "j1939socket.h"

#include <unistd.h>
#include <getopt.h>
#include <error.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "libj1939.h"

using namespace v8;

Nan::Persistent<v8::Function> J1939Socket::constructor;

NAN_MODULE_INIT(J1939Socket::Init) {
  v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("J1939Socket").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  Nan::SetPrototypeMethod(tpl, "open", open);

  constructor.Reset(Nan::GetFunction(tpl).ToLocalChecked());
  Nan::Set(target, Nan::New("J1939Socket").ToLocalChecked(), Nan::GetFunction(tpl).ToLocalChecked());
}

NAN_METHOD(J1939Socket::New) {
  if (info.IsConstructCall()) {
    v8::String::Utf8Value value(info[0]->ToString());
    J1939Socket *obj = new J1939Socket(std::string(*value));
    obj->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  } else {
    const int argc = 1; 
    v8::Local<v8::Value> argv[argc] = {info[0]};
    v8::Local<v8::Function> cons = Nan::New(constructor);
    info.GetReturnValue().Set(cons->NewInstance(argc, argv));
  }
}

namespace {
Local<String> Errno(const char* str) {
  using std::string;
  return
    (Nan::New<String>(string(str) + string(": ") + string(strerror(errno))))
    .ToLocalChecked();
}

}  // namespace

void J1939Socket::receiveMessagesCB(uv_poll_t* handle, int status, int events) {
  J1939Socket *obj = static_cast<J1939Socket*>(handle->data);
  obj->receiveMessages(status, events);
}


NAN_METHOD(J1939Socket::open) {
  J1939Socket* obj = Nan::ObjectWrap::Unwrap<J1939Socket>(info.This());

  obj->callback_.SetFunction(info[0].As<Function>());

  obj->socket_ = socket(PF_CAN, SOCK_DGRAM, CAN_J1939);
  if (obj->socket_ < 0) {
    Nan::ThrowError(Errno("socket(can, dgram, j1939)"));
    return;
  }

  int ival_1 = 1;
  int ret = setsockopt(obj->socket_, SOL_SOCKET, SO_TIMESTAMP, &ival_1, sizeof(ival_1));
  if (ret < 0) {
    Nan::ThrowError(Errno("setsockopt timestamp"));
    return;
  }

  int length = packetLength_;
  ret = setsockopt(obj->socket_, SOL_SOCKET, SO_RCVBUF, &length, sizeof(length));
  if (ret < 0) {
    Nan::ThrowError(Errno("setsockopt packet length"));
    return;
  }

  struct sockaddr_can addr;

  ret = libj1939_str2addr(obj->ifname_.c_str(), 0, &addr);
  if (ret < 0) {
    Nan::ThrowError("Can't find can interface");
    return;
  }

  /* bind(): to default, only ifindex is used. */
  struct sockaddr_can src;
  memset(&src, 0, sizeof(src));
  src.can_ifindex = addr.can_ifindex;
  src.can_family = AF_CAN;
  src.can_addr.j1939.name = J1939_NO_NAME;
  src.can_addr.j1939.addr = J1939_NO_ADDR;
  src.can_addr.j1939.pgn = J1939_NO_PGN;
  ret = bind(obj->socket_, (const sockaddr *) &src, sizeof(src));
  if (ret < 0) {
    Nan::ThrowError(Errno("bind()"));
    return;
  }

  uv_poll_init_socket(uv_default_loop(), &obj->socketHandle_, obj->socket_);
  uv_poll_start(&obj->socketHandle_, UV_READABLE, receiveMessagesCB);
}

void J1939Socket::receiveMessages(int status, int events) {
  Nan::HandleScope scope;

  if (status < 0) {
    // TODO report error
    return;
  }

  static char ctrlmsg[
    CMSG_SPACE(sizeof(struct timeval))
    + CMSG_SPACE(sizeof(uint8_t)) /* dest addr */
    + CMSG_SPACE(sizeof(uint64_t)) /* dest name */
    + CMSG_SPACE(sizeof(uint8_t)) /* priority */
  ];

  static struct iovec iov;
  struct sockaddr_can src;

  static struct msghdr msg;

  msg.msg_name = &src;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = &ctrlmsg;

  while (1) {
    char *buffer = static_cast<char*>(malloc(packetLength_));
    iov.iov_base = buffer;
    iov.iov_len = packetLength_;
    msg.msg_controllen = sizeof(ctrlmsg);
    msg.msg_flags = 0;
    msg.msg_namelen = sizeof(src);

    int ret = recvmsg(socket_, &msg, MSG_DONTWAIT);
    if (ret == EAGAIN || ret == EWOULDBLOCK) {
      free(buffer);
      break;
    }
    if (ret < 0) {
      // report error
      free(buffer);
      break;
    }
    int numBytesReceived = ret;

    Local<Date> timestamp;
    Local<Number> dstAddr;
    Local<Number> priority;
    Local<String> srcAddr = Nan::New<String>(libj1939_addr2str(&src)).ToLocalChecked();

    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
         cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
      switch (cmsg->cmsg_level) {
        case SOL_SOCKET:
          if (cmsg->cmsg_type == SCM_TIMESTAMP) {
            struct timeval tdut;
            memcpy(&tdut, CMSG_DATA(cmsg), sizeof(tdut));
            timestamp = Nan::New<Date>(tdut.tv_sec * 1000.0 + tdut.tv_usec / 1000.0)
              .ToLocalChecked();
          }
          break;
        case SOL_CAN_J1939:
          if (cmsg->cmsg_type == SCM_J1939_DEST_ADDR) {
            dstAddr = Nan::New<Number>(*CMSG_DATA(cmsg));
          } else if (cmsg->cmsg_type == SCM_J1939_DEST_NAME) {
            //uint64_t dst_name;
            //memcpy(&dst_name, CMSG_DATA(cmsg), cmsg->cmsg_len - CMSG_LEN(0));
          } else if (cmsg->cmsg_type == SCM_J1939_PRIO) {
            priority = Nan::New<Number>(*CMSG_DATA(cmsg));
          }
          break;
      }
    }

    Local<v8::Value> argv[] = {
      // buffer will be garbage collected
      Nan::NewBuffer(buffer, numBytesReceived).ToLocalChecked(),
      timestamp,
      srcAddr,
      priority,
      dstAddr
    };
    callback_.Call(sizeof(argv) / sizeof(argv[0]), argv);
  }
}

NODE_MODULE(j1939socket, J1939Socket::Init)
