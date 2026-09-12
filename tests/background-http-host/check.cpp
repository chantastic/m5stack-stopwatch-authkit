#include <Arduino.h>
#include <ArduinoJson.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <new>
#include <cassert>
#include <vector>
namespace UploadMemoryProof {
std::mutex lock;
std::map<void*,size_t> watched;
size_t wipedAndFreed=0;
void watch(void *pointer,size_t size) {std::lock_guard<std::mutex> hold(lock);assert(watched.emplace(pointer,size).second);}
size_t released() {std::lock_guard<std::mutex> hold(lock);return wipedAndFreed;}
void release(void *pointer) {
  std::lock_guard<std::mutex> hold(lock);
  auto found=watched.find(pointer);
  if(found!=watched.end()) {
    auto *bytes=static_cast<const uint8_t*>(pointer);
    for(size_t i=0;i<found->second;i++)assert(bytes[i]==0);
    watched.erase(found);wipedAndFreed++;
  }
  std::free(pointer);
}
}
#define private public
#define free UploadMemoryProof::release
#include "../../firmware/devices_badge/background_http.h"
#undef free
#undef private
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
void drainCancelled(){for(int i=0;i<2000&&client.busy();i++)std::this_thread::sleep_for(std::chrono::milliseconds(1));assert(!client.busy());}
void voiceChecks(JsonDocument &reply) {
  static constexpr char inbox[]="https://voice.example.test/v1/mentions";
  static constexpr char transcription[]="https://voice.example.test/v1/transcriptions";
  static constexpr char send[]="https://voice.example.test/v1/replies";
  static constexpr char status[]="https://voice.example.test/v1/replies/status";
  static constexpr char deepgram[]="https://voice.example.test/v1/transcriptions/deepgram";
  auto *clip=static_cast<uint8_t*>(ps_malloc(960044));assert(clip);
  std::memset(clip,0xa5,960044);std::memcpy(clip,"RIFF",4);
  auto *original=clip;
  int initialStarts=FakeNet::starts;
  // No voice origin is trusted by default, including the existing identity host.
  assert(client.getJson(Kind::REPLY_INBOX,inbox,"device",reply)==-40);
  assert(client.getReplyStatus(status,"device","intent-12345678",reply)==-40);
  assert(client.postDeviceJson(Kind::REPLY_SEND,send,"{}","device","intent-12345678",reply)==-40);
  assert(client.postWav(transcription,"device",clip,960044,reply)==-40 && clip==original);
  assert(client.getJson(Kind::REPLY_INBOX,"https://auth.chan.dev/devices/replies","device",reply)==-40);
  BadgeVoiceEndpointConfig config{"https://voice.example.test",inbox,transcription,send,"voice-test-ca",status,deepgram};
  auto invalid=config;invalid.origin="http://voice.example.test";assert(!client.configureVoiceEndpoints(invalid));
  invalid=config;invalid.origin="https://voice.example.test/";assert(!client.configureVoiceEndpoints(invalid));
  invalid=config;invalid.origin="https://voice.example.test:443";assert(!client.configureVoiceEndpoints(invalid));
  invalid=config;invalid.inboxUrl="https://voice.example.test.evil/v1/mentions";assert(!client.configureVoiceEndpoints(invalid));
  invalid=config;invalid.replyUrl="https://voice.example.test/v1/../replies";assert(!client.configureVoiceEndpoints(invalid));
  invalid=config;invalid.replyUrl="https://voice.example.test/v1/replies?target=1";assert(!client.configureVoiceEndpoints(invalid));
  invalid=config;invalid.replyUrl="https://voice.example.test/v1/replies#ignored";assert(!client.configureVoiceEndpoints(invalid));
  invalid=config;invalid.ca=nullptr;assert(!client.configureVoiceEndpoints(invalid));
  invalid=config;invalid.statusUrl="https://voice.example.test.evil/v1/replies/status";assert(!client.configureVoiceEndpoints(invalid));
  invalid=config;invalid.statusUrl=nullptr;assert(!client.configureVoiceEndpoints(invalid));
  invalid=config;invalid.deepgramTranscriptionUrl="https://elsewhere.example.test/v1/transcriptions";assert(!client.configureVoiceEndpoints(invalid));
  assert(FakeNet::starts==initialStarts);
  // Configuration values are snapshotted too; no borrowed caller CA lifetime.
  String ownedCa="voice-test-ca";config.ca=ownedCa.c_str();assert(client.configureVoiceEndpoints(config));ownedCa="overwritten";
  BadgeBackgroundHttp unstarted;
  config.ca="voice-test-ca";assert(unstarted.configureVoiceEndpoints(config));
  assert(unstarted.postWav(transcription,"device",clip,960044,reply)==-50 && clip==original);
  assert(client.getJson(Kind::REPLY_INBOX,"https://voice.example.test/v1/mentions/more","device",reply)==-40);
  assert(client.getJson(Kind::PROFILE_X,inbox,"device",reply)==-40);
  assert(client.postJson(Kind::REPLY_SEND,send,"{}",reply)==-40);
  assert(client.postDeviceJson(Kind::REPLY_SEND,transcription,"{}","device","intent-12345678",reply)==-40);
  assert(client.postDeviceJson(Kind::REPLY_SEND,send,"{}","device","bad\r\nHeader:value",reply)==-40);
  assert(client.postDeviceJson(Kind::REPLY_SEND,send,"{}","device","",reply)==-40);
  assert(client.postDeviceJson(Kind::REPLY_SEND,send,"{}","device",String(129,'x'),reply)==-40);
  assert(client.postDeviceJson(Kind::REPLY_SEND,send,String(4097,'x'),"device","intent-12345678",reply)==-40);
  assert(client.getReplyStatus(String(status)+"?key=intent-12345678","device","intent-12345678",reply)==-40);
  assert(client.getReplyStatus(status,"device","",reply)==-40);
  assert(client.getReplyStatus(status,"device","bad\r\nInjected:yes",reply)==-40);
  assert(client.getReplyStatus(status,"device",String(129,'x'),reply)==-40);
  assert(client.getReplyStatus(status,"bad\nBearer:value","intent-12345678",reply)==-40);
  assert(client.getJson(Kind::REPLY_STATUS,status,"device",reply)==-40);
  assert(client.postWav(transcription,"bad\nAuthorization",clip,960044,reply)==-40 && clip==original);
  assert(client.postWav(transcription,"device",clip,960045,reply)==-40 && clip==original);
  assert(client.postWav(transcription,"device",clip,43,reply)==-40 && clip==original);
  assert(FakeNet::starts==initialStarts);
  // Snapshot allocation failure likewise leaves ownership with the caller.
  FakePsram::failNext=true;
  assert(client.postWav(transcription,"device",clip,960044,reply)==-43 && clip==original);
  assert(clip[4]==0xa5 && !client.busy());
  // An enqueue failure never takes or wipes the caller's audio.
  FakeRtos::rejectNextSend=true;
  assert(client.postWav(transcription,"device",clip,960044,reply)==-50 && clip==original);
  assert(clip[4]==0xa5 && !client.busy());
  // Maximum clip goes straight to POST, with no megabyte String/body copy.
  configure("{\"state\":\"transcribed\",\"text\":\"review before sending\"}");pause();
  String bearer="original-device-token";
  size_t released=UploadMemoryProof::released();UploadMemoryProof::watch(clip,960044);
  assert(client.postWav(transcription,bearer,clip,960044,reply)==BADGE_HTTP_PENDING && clip==nullptr);entered();
  bearer="changed-after-enqueue";
  assert(FakeNet::seenBodyPointer==original && FakeNet::seenBodyLength==960044);
  assert(FakeNet::seenBody.substr(0,4)=="RIFF" && uint8_t(FakeNet::seenBody[960043])==0xa5);
  assert(FakeNet::seenHeaders.at("Content-Type")=="audio/wav");
  assert(FakeNet::seenHeaders.at("Authorization")=="Bearer original-device-token");
  assert(FakeNet::seenHeaders.count("Idempotency-Key")==0);
  assert(FakeNet::seenCa=="voice-test-ca" && FakeNet::seenSocketTimeout==60000 && FakeNet::seenConnectTimeout==10000);
  assert(!client.configureVoiceEndpoints({}));
  int starts=FakeNet::starts;auto before=millis();
  for(int i=0;i<1000;i++)assert(client.postWav(transcription,bearer,clip,0,reply)==BADGE_HTTP_PENDING);
  assert(millis()-before<100 && FakeNet::starts==starts && UploadMemoryProof::released()==released);
  assert(client.getJson(Kind::REPLY_INBOX,inbox,"device",reply)==BADGE_HTTP_PENDING);
  resume();assert(complete([&]{return client.postWav(transcription,bearer,clip,0,reply);})==200);
  assert(UploadMemoryProof::released()==released+1);
  assert(std::string(reply["text"].as<const char*>())=="review before sending");
  assert(client.postWav(transcription,bearer,clip,0,reply)==-40 && FakeNet::starts==starts);
  // A provider failure consumes/wipes the clip; the transport cannot replay it
  // to the fallback endpoint. Deepgram needs a separately supplied recording.
  configure("{\"state\":\"transcription_failed\"}",502);
  clip=static_cast<uint8_t*>(ps_malloc(44));std::memset(clip,0x6a,44);
  released=UploadMemoryProof::released();UploadMemoryProof::watch(clip,44);
  assert(complete([&]{return client.postWav(transcription,"device",clip,44,reply);})==502 && !clip);
  assert(UploadMemoryProof::released()==released+1);starts=FakeNet::starts;
  assert(client.postWav(deepgram,"device",clip,44,reply)==-40 && FakeNet::starts==starts);
  configure("{\"state\":\"transcribed\",\"text\":\"fresh fallback recording\"}");pause();
  clip=static_cast<uint8_t*>(ps_malloc(44));std::memset(clip,0x6b,44);
  released=UploadMemoryProof::released();UploadMemoryProof::watch(clip,44);
  assert(client.postWav(deepgram,"device",clip,44,reply)==BADGE_HTTP_PENDING && !clip);entered();
  assert(FakeNet::seenUrl==deepgram && FakeNet::seenHeaders.at("Content-Type")=="audio/wav");
  // Same-kind polling cannot mutate the in-flight selected provider URL.
  assert(client.postWav(transcription,"device",clip,0,reply)==BADGE_HTTP_PENDING);
  assert(FakeNet::seenUrl==deepgram);resume();
  assert(complete([&]{return client.postWav(deepgram,"device",clip,0,reply);})==200);
  assert(UploadMemoryProof::released()==released+1);
  // Reply JSON and its idempotency key are immutable per explicit user intent.
  configure("{\"state\":\"sent\",\"replyId\":\"123\"}",201);pause();
  String body="{\"targetId\":\"456\",\"text\":\"approved\"}",key="intent-original-123";
  assert(client.postDeviceJson(Kind::REPLY_SEND,send,body,"device",key,reply)==BADGE_HTTP_PENDING);entered();
  body="{\"text\":\"changed\"}";key="intent-changed-456";
  assert(FakeNet::seenBody=="{\"targetId\":\"456\",\"text\":\"approved\"}");
  assert(FakeNet::seenHeaders.at("Content-Type")=="application/json");
  assert(FakeNet::seenHeaders.at("Idempotency-Key")=="intent-original-123");
  assert(FakeNet::seenHeaders.at("Authorization")=="Bearer device" && FakeNet::seenSocketTimeout==60000);
  resume();assert(complete([&]{return client.postDeviceJson(Kind::REPLY_SEND,send,body,"device",key,reply);})==201);
  assert(std::string(reply["replyId"].as<const char*>())=="123");
  // GET errors carry useful backend state, while old profile behavior is stable.
  configure("{\"state\":\"credits_unavailable\"}",402);
  assert(complete([&]{return client.getJson(Kind::REPLY_INBOX,inbox,"device",reply);})==402);
  assert(std::string(reply["state"].as<const char*>())=="credits_unavailable");
  configure("temporary proxy error",503,-2,"text/html");
  assert(complete([&]{return client.getJson(Kind::REPLY_INBOX,inbox,"device",reply);})==503 && reply.size()==0);
  configure("{\"state\":\"outcome_unknown\"}",409);
  assert(complete([&]{return client.postDeviceJson(Kind::REPLY_SEND,send,"{}","device","intent-uncertain",reply);})==409);
  assert(std::string(reply["state"].as<const char*>())=="outcome_unknown");
  starts=FakeNet::starts;std::this_thread::sleep_for(std::chrono::milliseconds(20));assert(FakeNet::starts==starts);
  // Read-only reconciliation keeps its intent key and bearer immutable. It
  // cannot silently repost, including an unknown/not-found status response.
  configure("{\"state\":\"sent\",\"replyId\":\"123\"}");pause();
  key="intent-status-original";bearer="status-device-original";
  assert(client.getReplyStatus(status,bearer,key,reply)==BADGE_HTTP_PENDING);entered();
  key="intent-status-changed";bearer="status-device-changed";
  assert(FakeNet::seenMethod=="GET" && FakeNet::seenBody.empty() && FakeNet::seenBodyPointer==nullptr);
  assert(FakeNet::seenUrl==status && FakeNet::seenHeaders.at("Idempotency-Key")=="intent-status-original");
  assert(FakeNet::seenHeaders.at("Authorization")=="Bearer status-device-original");
  assert(FakeNet::seenHeaders.count("Content-Type")==0 && FakeNet::seenCa=="voice-test-ca");
  assert(FakeNet::seenSocketTimeout==60000);
  starts=FakeNet::starts;
  for(int i=0;i<1000;i++)assert(client.getReplyStatus(status,bearer,key,reply)==BADGE_HTTP_PENDING);
  assert(FakeNet::starts==starts);
  assert(client.postDeviceJson(Kind::REPLY_SEND,send,"{}","device","new-intent",reply)==BADGE_HTTP_PENDING);
  resume();assert(complete([&]{return client.getReplyStatus(status,bearer,key,reply);})==200);
  assert(std::string(reply["replyId"].as<const char*>())=="123");
  configure("{\"state\":\"not_found\"}",404);
  assert(complete([&]{return client.getReplyStatus(status,"device","unknown-intent",reply);})==404);
  assert(std::string(reply["state"].as<const char*>())=="not_found" && FakeNet::seenMethod=="GET");
  starts=FakeNet::starts;std::this_thread::sleep_for(std::chrono::milliseconds(20));assert(FakeNet::starts==starts);
  configure("{\"state\":\"reauthorize\"}",401);
  assert(complete([&]{return client.getReplyStatus(status,"device","known-intent",reply);})==401);
  assert(std::string(reply["state"].as<const char*>())=="reauthorize");
  configure("{}",200,8193);
  assert(complete([&]{return client.getReplyStatus(status,"device","known-intent",reply);})==-42);
  configure();pause();assert(client.getReplyStatus(status,"device","known-intent",reply)==BADGE_HTTP_PENDING);entered();
  client.cancel();assert(client.busy());resume();drainCancelled();
  configure("{}",200,8193);
  assert(complete([&]{return client.getJson(Kind::REPLY_INBOX,inbox,"device",reply);})==-42);
  configure("not JSON",201);
  assert(complete([&]{return client.postDeviceJson(Kind::REPLY_SEND,send,"{}","device","intent-malformed",reply);})==-45);
  // While another kind owns the worker, the caller still owns its pending clip.
  configure();pause();assert(client.getJson(Kind::REPLY_INBOX,inbox,"device",reply)==BADGE_HTTP_PENDING);entered();
  clip=static_cast<uint8_t*>(ps_malloc(44));original=clip;std::memset(clip,0x6c,44);
  assert(client.postWav(transcription,"device",clip,44,reply)==BADGE_HTTP_PENDING && clip==original && clip[0]==0x6c);
  resume();assert(complete([&]{return client.getJson(Kind::REPLY_INBOX,inbox,"device",reply);})==200);
  // Cancellation waits for the bounded worker, then wipes audio exactly once.
  configure();pause();released=UploadMemoryProof::released();UploadMemoryProof::watch(clip,44);
  assert(client.postWav(transcription,"device",clip,44,reply)==BADGE_HTTP_PENDING && !clip);entered();
  client.cancel();assert(client.busy() && UploadMemoryProof::released()==released);
  resume();drainCancelled();assert(UploadMemoryProof::released()==released+1);
  // Upload is also scrubbed if allocating its response fails on the worker.
  configure();pause();clip=static_cast<uint8_t*>(ps_malloc(44));std::memset(clip,0x7d,44);
  released=UploadMemoryProof::released();UploadMemoryProof::watch(clip,44);
  assert(client.postWav(transcription,"device",clip,44,reply)==BADGE_HTTP_PENDING && !clip);entered();
  FakePsram::failNext=true;resume();
  assert(complete([&]{return client.postWav(transcription,"device",clip,0,reply);})==-43);
  assert(UploadMemoryProof::released()==released+1);
  // Cancellation of an already completed audio result also frees the result.
  configure();clip=static_cast<uint8_t*>(ps_malloc(44));std::memset(clip,0x7d,44);released=UploadMemoryProof::released();UploadMemoryProof::watch(clip,44);
  assert(client.postWav(transcription,"device",clip,44,reply)==BADGE_HTTP_PENDING && !clip);entered();
  for(int i=0;i<2000 && UploadMemoryProof::released()==released;i++)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(UploadMemoryProof::released()==released+1);assert(client.busy());client.cancel();assert(!client.busy());
  assert(client.configureVoiceEndpoints({}));starts=FakeNet::starts;
  assert(client.getJson(Kind::REPLY_INBOX,inbox,"device",reply)==-40);
  assert(client.postDeviceJson(Kind::REPLY_SEND,send,"{}","device","intent-disabled",reply)==-40);
  assert(client.getReplyStatus(status,"device","intent-disabled",reply)==-40);
  assert(FakeNet::starts==starts);
  configure();assert(complete([&]{return client.getJson(Kind::PROFILE_X,xUrl,"device",reply);})==200);
  assert(FakeNet::seenCa==ROOT_CA);
  assert(FakeNet::seenSocketTimeout==12000);
  assert(UploadMemoryProof::watched.empty());
}
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
  assert(!client.busy());voiceChecks(reply);FakeRtos::stop();
  std::cout<<"Background HTTP ownership, cancellation, immutable requests, auth boundaries, JSON lifetime, response limits, disabled-by-default voice endpoints, no-copy audio upload/wiping, explicit reply idempotency, and voice error bodies passed.\n";
}
