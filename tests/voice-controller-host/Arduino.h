#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
class String : public std::string {
 public:
  using std::string::string;
  String()=default;
  String(const std::string &value):std::string(value){}
  bool isEmpty() const {return empty();}
  void setCharAt(size_t index,char value) {at(index)=value;}
  size_t write(uint8_t value) {push_back(char(value));return 1;}
  size_t write(const uint8_t *bytes,size_t count) {append(reinterpret_cast<const char*>(bytes),count);return count;}
};
using std::max;
inline uint32_t fixtureMillis=1000;
inline uint32_t millis() {return fixtureMillis;}
