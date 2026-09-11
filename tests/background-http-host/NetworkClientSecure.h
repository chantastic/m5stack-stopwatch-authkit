#pragma once
#include "Arduino.h"
class NetworkClientSecure {
 public:
  virtual ~NetworkClientSecure()=default;
  virtual int available(){return stopped?0:int(body.size()-offset);}
  virtual uint8_t connected(){return !stopped&&offset<body.size();}
  virtual int read(){uint8_t c=0;return read(&c,1)==1?c:-1;}
  virtual int read(uint8_t *p,size_t n){size_t count=std::min(n,body.size()-offset);if(count)std::memcpy(p,body.data()+offset,count);offset+=count;return int(count);}
  virtual size_t write(uint8_t){return 1;}
  virtual size_t write(const uint8_t*,size_t n){return n;}
  void stop(){stopped=true;}
  void setCACert(const char *ca){certificate=ca;}
  void setHandshakeTimeout(int){}
  void fill(const std::string &input){body=input;offset=0;stopped=false;}
  std::string body,certificate;
  size_t offset=0;
  bool stopped=false;
};
