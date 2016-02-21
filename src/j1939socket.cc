#include "j1939socket.h"

#include <unistd.h>
#include <getopt.h>
#include <error.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "libj1939.h"

#include <deque>

using namespace v8;

namespace {

class Packet {
 public:
  struct timeval time_;
  char *data_;
  int length_;
  int dstAddr_;
  uint64_t dstName_;
  int priority_;
  struct sockaddr_can src_;

  Packet(int size) {
    memset(this, 0, sizeof(Packet));
    data_ = (char *) malloc(size);
    length_ = size;
  }

  ~Packet() {
    if (data_) {
      free(data_);
    }
  }

  Packet(const Packet& other) {
    time_ = other.time_;
    data_ = (char *) malloc(other.length_);
    memcpy(data_, other.data_, other.length_);
    length_ = other.length_;
    dstAddr_ = other.dstAddr_;
    dstName_ = other.dstName_;
    priority_ = other.priority_;
    src_ = other.src_;
  }

  void Call(Nan::Callback* cb) {
    if (!data_) {
      return;
    }
    Nan::HandleScope scope;

    Local<Number> dstAddr = Nan::New<Number>(dstAddr_);

    char str[32];
    snprintf(str, sizeof(str), "%0llx", (unsigned long long) src_.can_addr.j1939.name);
    Local<String> srcName = Nan::New<String>(str).ToLocalChecked();
    Local<Number> pgn = Nan::New<Number>(src_.can_addr.j1939.pgn);
    Local<Number> addr = Nan::New<Number>(src_.can_addr.j1939.addr);
    Local<Date> timestamp =
      Nan::New<Date>(time_.tv_sec * 1000.0
                     + time_.tv_usec / 1000.0).ToLocalChecked();
    Local<Number> priority = Nan::New<Number>(priority_);

    Local<v8::Value> argv[] = {
      Nan::NewBuffer(data_, length_).ToLocalChecked(),
      timestamp,
      srcName,
      pgn,
      priority,
      dstAddr,
      addr
    };
    // buffer will be garbage collected by v8
    data_ = 0;
    cb->Call(
        Nan::GetCurrentContext()->Global(),
        sizeof(argv) / sizeof(argv[0]), argv);
  }

};

std::deque<Packet> receivedPackets;

}  // namespace


Nan::Persistent<v8::Function> J1939Socket::constructor;

NAN_MODULE_INIT(J1939Socket::Init) {
  v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("J1939Socket").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  Nan::SetPrototypeMethod(tpl, "open", open);
  Nan::SetPrototypeMethod(tpl, "fetch", fetch);

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
  Nan::HandleScope scope;

  J1939Socket* obj = Nan::ObjectWrap::Unwrap<J1939Socket>(info.This());

  Local<Function> f = info[0].As<Function>();
  //obj->callback_.Reset(f);
  obj->callback_.SetFunction(f);

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
  obj->socketHandle_.data = obj;
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

  static struct msghdr msg;
  memset(&msg, 0, sizeof(msg));

  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = &ctrlmsg;

  while (1) {
    Packet packet(packetLength_);

    iov.iov_base = packet.data_;
    iov.iov_len = packet.length_;
    msg.msg_controllen = sizeof(ctrlmsg);
    msg.msg_flags = 0;
    msg.msg_name = &packet.src_;
    msg.msg_namelen = sizeof(packet.src_);

    int ret = recvmsg(socket_, &msg, MSG_DONTWAIT);
    if (ret == EAGAIN || ret == EWOULDBLOCK) {
      break;
    }
    if (ret < 0) {
      // TODO: report error
      break;
    }
    packet.length_ = ret;

    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
         cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
      switch (cmsg->cmsg_level) {
        case SOL_SOCKET:
          if (cmsg->cmsg_type == SCM_TIMESTAMP) {
            memcpy(&packet.time_, CMSG_DATA(cmsg), sizeof(packet.time_));
          }
          break;
        case SOL_CAN_J1939:
          if (cmsg->cmsg_type == SCM_J1939_DEST_ADDR) {
            packet.dstAddr_ = *CMSG_DATA(cmsg);
          } else if (cmsg->cmsg_type == SCM_J1939_DEST_NAME) {
            memcpy(&packet.dstName_, CMSG_DATA(cmsg), cmsg->cmsg_len - CMSG_LEN(0));
          } else if (cmsg->cmsg_type == SCM_J1939_PRIO) {
            packet.priority_ = *CMSG_DATA(cmsg);
          }
          break;
      }
    }

    // We want to notify js code immediately about the new packet, with:
    //packet.Call(&callback_);
    // however, it makes node crash after a while.
    // as a workaround, we keep the packet until the js code calls fetch().
    receivedPackets.push_back(packet);
  }
}

NAN_METHOD(J1939Socket::fetch) {
  Nan::HandleScope scope;

  J1939Socket* obj = Nan::ObjectWrap::Unwrap<J1939Socket>(info.This());

  while (receivedPackets.size() > 0) {
    receivedPackets.front().Call(&obj->callback_);
    receivedPackets.pop_front();
  }
}

NODE_MODULE(j1939socket, J1939Socket::Init)
