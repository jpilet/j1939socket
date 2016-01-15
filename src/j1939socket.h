#ifndef J1939_SOCKET_H
#define J1939_SOCKET_H

#include <nan.h>

class J1939Socket : public Nan::ObjectWrap {
  public:
    static NAN_MODULE_INIT(Init);

  private:
    explicit J1939Socket(std::string ifname) : socket_(0), ifname_(ifname) { }
    ~J1939Socket() { }
    void receiveMessages(int status, int events);
    static void receiveMessagesCB(uv_poll_t* handle, int status, int events);

    static NAN_METHOD(New);
    static NAN_METHOD(open);

    static Nan::Persistent<v8::Function> constructor;

    Nan::Callback callback_;

    int socket_;
    uv_poll_t socketHandle_;

    std::string ifname_;
    static const int packetLength_ = 1024;
};

#endif // J1939_SOCKET_H
