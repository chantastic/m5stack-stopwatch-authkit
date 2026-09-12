#pragma once
#include "NetworkClientSecure.h"
#include <map>
#include <mutex>
#include <condition_variable>
constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS=0;
namespace FakeNet {
inline std::mutex lock;
inline std::condition_variable changed;
inline bool paused=false,entered=false;
inline int starts=0,code=200,expected=-2;
inline std::string body="{\"state\":\"ready\"}",type="application/json",transfer,encoding;
inline std::string seenUrl,seenBody,seenCa,seenMethod;
inline const uint8_t *seenBodyPointer=nullptr;
inline size_t seenBodyLength=0;
inline int seenSocketTimeout=0,seenConnectTimeout=0;
inline std::map<std::string,std::string> seenHeaders;
}
class HTTPClient {
 public:
  void useHTTP10(bool b){if(!b)abort();}
  void setReuse(bool b){if(b)abort();}
  void setConnectTimeout(int n){connectTimeout=n;}
  void setTimeout(int n){socketTimeout=n;}
  void setFollowRedirects(int n){if(n!=0)abort();}
  void collectHeaders(const char **,int){}
  bool begin(NetworkClientSecure &tls,const String &u){stream=&tls;url=u;return true;}
  void addHeader(const char *key,const String &value){headers[key]=value;}
  int POST(uint8_t *p,size_t n){return run(std::string(reinterpret_cast<char*>(p),n),p,n);}
  int GET(){return run("",nullptr,0);}
  int run(const std::string &sent,const uint8_t *pointer,size_t length){
    std::unique_lock<std::mutex> hold(FakeNet::lock);
    FakeNet::entered=true;FakeNet::starts++;FakeNet::seenUrl=url;FakeNet::seenBody=sent;FakeNet::seenHeaders=headers;FakeNet::seenCa=stream->certificate;FakeNet::changed.notify_all();
    FakeNet::seenBodyPointer=pointer;FakeNet::seenBodyLength=length;FakeNet::seenSocketTimeout=socketTimeout;FakeNet::seenConnectTimeout=connectTimeout;
    FakeNet::seenMethod=pointer?"POST":"GET";
    FakeNet::changed.wait(hold,[]{return !FakeNet::paused;});
    stream->fill(FakeNet::body);type=FakeNet::type;transfer=FakeNet::transfer;encoding=FakeNet::encoding;
    expected=FakeNet::expected==-2?int(FakeNet::body.size()):FakeNet::expected;
    return FakeNet::code;
  }
  String header(const char *key){return std::string(key)=="Content-Type"?type:std::string(key)=="Transfer-Encoding"?transfer:encoding;}
  int getSize(){return expected;}
  NetworkClientSecure *getStreamPtr(){return stream;}
  void end(){stream->stop();}
  NetworkClientSecure *stream=nullptr;
  std::string url,type,transfer,encoding;
  std::map<std::string,std::string> headers;
  int expected=0,socketTimeout=0,connectTimeout=0;
};
