#include <Arduino.h>
#include <ArduinoJson.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <new>
#define private public
#include "../../firmware/devices_badge/background_http.h"
#undef private
#include <cassert>
#include <iostream>
using Kind=BadgeHttpKind;
static BadgeBackgroundHttp client;
static constexpr char tokenUrl[]="https://api.workos.com/user_management/authenticate";
static constexpr char xUrl[]="https://auth.chan.dev/devices/x";
void configure(std::string body="{\"state\":\"ready\"}",int code=200,int expected=-2,std::string type="application/json",std::string transfer="",std::string encoding=""){
  assert(!client.busy());
  std::lock_guard<std::mutex> hold(FakeNet::lock);FakeNet::body=body;FakeNet::code=code;FakeNet::expected=expected;FakeNet::type=type;FakeNet::transfer=transfer;FakeNet::encoding=encoding;FakeNet::entered=false;
}
void pause(){std::lock_guard<std::mutex> hold(FakeNet::lock);FakeNet::paused=true;}
void entered(){std::unique_lock<std::mutex> hold(FakeNet::lock);assert(FakeNet::changed.wait_for(hold,std::chrono::seconds(2),[]{return FakeNet::entered;}));}
void resume(){std::lock_guard<std::mutex> hold(FakeNet::lock);FakeNet::paused=false;FakeNet::changed.notify_all();}
template<class F> int complete(F call){int code=BADGE_HTTP_PENDING;for(int i=0;i<2000&&code==BADGE_HTTP_PENDING;i++){code=call();if(code==BADGE_HTTP_PENDING)std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(code!=BADGE_HTTP_PENDING);return code;}
int main(){
  {
    BadgeBackgroundHttp::BoundedTls capped(millis()+10000);
    capped.fill(std::string(8193,'x'));
    std::vector<uint8_t> header(8193);
    assert(capped.read(header.data(),header.size())==8192);
    assert(capped.available()==0&&capped.aborted());
    BadgeBackgroundHttp::BoundedTls expired(millis()-1);
    expired.fill("unused");
    assert(expired.available()==0&&expired.aborted());
    assert(expired.write(reinterpret_cast<const uint8_t*>("request"),7)==0);
  }
  JsonDocument reply;
  assert(client.getJson(Kind::PROFILE_X,xUrl,"test-bearer",reply)==-50);
  assert(client.workerStackFree()==0);assert(client.begin());assert(client.begin());assert(client.workerStackFree()==12288);
  assert(client.getJson(Kind::PROFILE_X,"https://auth.chan.dev.evil/devices/x","test-bearer",reply)==-40);
  assert(client.getJson(Kind::PROFILE_LINKEDIN,xUrl,"test-bearer",reply)==-40);
  assert(client.getJson(Kind::PROFILE_X,xUrl,"bad\r\nInjected:yes",reply)==-40);
  assert(client.postJson(Kind::PAIR_BEGIN,tokenUrl,"client_id=public",reply)==-40);
  configure("{\"access_token\":\"copied-token\"}");pause();
  String body="grant_type=refresh_token&refresh_token=first";
  assert(client.postJson(Kind::AUTH_REFRESH,tokenUrl,body,reply)==BADGE_HTTP_PENDING);
  body="changed after enqueue";entered();
  assert(FakeNet::seenBody=="grant_type=refresh_token&refresh_token=first");
  auto started=millis();
  for(int i=0;i<1000;i++)assert(client.postJson(Kind::AUTH_REFRESH,tokenUrl,body,reply)==BADGE_HTTP_PENDING);
  assert(millis()-started<100);assert(client.busy());assert(FakeNet::starts==1);
  assert(client.getJson(Kind::PROFILE_X,xUrl,"other",reply)==BADGE_HTTP_PENDING);
  resume();assert(complete([&]{return client.postJson(Kind::AUTH_REFRESH,tokenUrl,body,reply);})==200);
  assert(std::string(reply["access_token"].as<const char*>())=="copied-token");assert(!client.busy());
  assert(FakeNet::seenHeaders.at("Content-Type")=="application/x-www-form-urlencoded");
  assert(FakeNet::seenHeaders.at("Accept-Encoding")=="identity");
  assert(FakeNet::seenHeaders.count("Authorization")==0);
  configure();pause();
  assert(client.getJson(Kind::PROFILE_X,xUrl,"owned-session",reply)==BADGE_HTTP_PENDING);entered();
  client.cancel();assert(client.busy());
  assert(client.getJson(Kind::PROFILE_X,xUrl,"new-session",reply)==BADGE_HTTP_PENDING);
  resume();for(int i=0;i<2000&&client.busy();i++)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(!client.busy());assert(FakeNet::seenHeaders.at("Authorization")=="Bearer owned-session");
  // Cancellation after completion was collected must also release the slot.
  configure();assert(client.getJson(Kind::PROFILE_X,xUrl,"session",reply)==BADGE_HTTP_PENDING);entered();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));assert(client.busy());client.cancel();assert(!client.busy());
  configure("{\"error\":\"invalid_grant\"}",400);
  assert(complete([&]{return client.postJson(Kind::AUTH_REFRESH,tokenUrl,"dummy",reply);})==400);
  assert(std::string(reply["error"].as<const char*>())=="invalid_grant");
  configure("no access",401);assert(complete([&]{return client.getJson(Kind::PROFILE_X,xUrl,"session",reply);})==401);
  configure("{}",200,-2,"application/json","chunked");assert(complete([&]{return client.getJson(Kind::PROFILE_X,xUrl,"session",reply);})==-41);
  configure("{}",200,8193);assert(complete([&]{return client.getJson(Kind::PROFILE_X,xUrl,"session",reply);})==-42);
  configure("short",200,10);assert(complete([&]{return client.getJson(Kind::PROFILE_X,xUrl,"session",reply);})==-44);
  configure("not json");assert(complete([&]{return client.getJson(Kind::PROFILE_X,xUrl,"session",reply);})==-45);
  configure("{}",200,-2,"text/html");assert(complete([&]{return client.getJson(Kind::PROFILE_X,xUrl,"session",reply);})==-45);
  configure("{\"state\":\"ready\"}",200,-1);assert(complete([&]{return client.getJson(Kind::PROFILE_X,xUrl,"session",reply);})==200);
  configure("image bytes",200,-2,"image/png");
  uint8_t *bytes=nullptr;size_t length=0;String type;
  assert(client.getBytes(Kind::AVATAR_X,"https://pbs.twimg.com.evil/profile_images/a","public-ca",bytes,length,type)==-40);
  assert(complete([&]{return client.getBytes(Kind::AVATAR_X,"https://pbs.twimg.com/profile_images/a","public-ca",bytes,length,type);})==200);
  assert(length==11&&std::string(reinterpret_cast<char*>(bytes),length)=="image bytes");assert(type=="image/png");
  assert(FakeNet::seenHeaders.count("Authorization")==0);assert(FakeNet::seenCa=="public-ca");free(bytes);
  configure("redirect",302,-2,"text/html");assert(complete([&]{return client.getBytes(Kind::AVATAR_X,"https://pbs.twimg.com/profile_images/a","public-ca",bytes,length,type);})==302);assert(!bytes&&!length);
  assert(!client.busy());FakeRtos::stop();
  std::cout<<"Background HTTP queue ownership, cancellation, immutable requests, auth boundaries, JSON lifetime, header/deadline caps, bounded bodies, errors, and caller-owned avatar buffers passed.\n";
}
