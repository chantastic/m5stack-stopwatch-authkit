#pragma once
#include <string>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <cstdint>
#define PROGMEM
class String : public std::string {
 public:
  using std::string::string;
  String()=default;
  String(const std::string &s):std::string(s){}
  size_t write(uint8_t byte){push_back(char(byte));return 1;}
  size_t write(const uint8_t *bytes,size_t count){append(reinterpret_cast<const char*>(bytes),count);return count;}
  bool isEmpty()const{return empty();}
  bool startsWith(const char *prefix)const{return rfind(prefix,0)==0;}
  bool endsWith(const char *suffix)const{size_t n=std::strlen(suffix);return size()>=n&&compare(size()-n,n,suffix)==0;}
  String substring(size_t first)const{return substr(first);}
  String substring(size_t first,size_t last)const{return substr(first,last-first);}
};
inline uint32_t millis(){using namespace std::chrono;return uint32_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());}
inline void *ps_malloc(size_t n){return malloc(n);}
