#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <new>
#include "trust.h"

static constexpr int BADGE_HTTP_PENDING=-10000;
enum class BadgeHttpKind : uint8_t {
  NONE, AUTH_REFRESH, PAIR_BEGIN, PAIR_POLL, WORKSPACE, WORKSPACE_SESSION,
  PROFILE_X, PROFILE_LINKEDIN, PROFILE_GITHUB,
  AVATAR_X, AVATAR_LINKEDIN, AVATAR_GITHUB
};

// One instance must live for the firmware's lifetime. Only the main task calls
// its public methods. Queues transfer exclusive ownership of each immutable
// request to the worker, and ownership of the completed response back again.
// No display, profile, token, filesystem, or Preferences object is shared.
class BadgeBackgroundHttp {
 public:
  BadgeBackgroundHttp()=default;
  BadgeBackgroundHttp(const BadgeBackgroundHttp&)=delete;
  BadgeBackgroundHttp& operator=(const BadgeBackgroundHttp&)=delete;

  bool begin() {
    if(worker_)return true;
    requests_=xQueueCreate(1,sizeof(Job*));
    responses_=xQueueCreate(1,sizeof(Job*));
    if(!requests_ || !responses_) {releaseQueues();return false;}
    // Network work stays off the display/input task. TLS has its own stack;
    // image decoding and JSON parsing still happen on the consuming task.
    if(xTaskCreatePinnedToCore(workerEntry,"badge_https",16384,this,1,&worker_,0)!=pdPASS) {
      worker_=nullptr;releaseQueues();return false;
    }
    return true;
  }

  bool busy() {collect();return active_;}
  BadgeHttpKind kind() {collect();return active_?activeKind_:BadgeHttpKind::NONE;}
  // ESP-IDF reports the minimum remaining stack in bytes, including TLS work.
  uint32_t workerStackFree() const {return worker_?uint32_t(uxTaskGetStackHighWaterMark(worker_)):0;}

  // Cancellation never touches worker-owned memory or deletes a running task.
  // The bounded request finishes, then its obsolete result is scrubbed. A new
  // job may start only after that completion has been drained.
  void cancel() {if(active_)discard_=true;collect();}

  int postJson(BadgeHttpKind kind,const String &url,const String &body,JsonDocument &reply) {
    if(!postAllowed(kind,url) || body.length()>16384)return -40;
    int code=pollOrStart(kind,url,body,String(),ROOT_CA,32768,true);
    return consumeJson(code,reply);
  }

  int getJson(BadgeHttpKind kind,const String &url,const String &bearer,JsonDocument &reply) {
    if(!getAllowed(kind,url) || bearer.isEmpty() || bearer.length()>8192 || !headerSafe(bearer))return -40;
    int code=pollOrStart(kind,url,String(),bearer,ROOT_CA,8192,false);
    return consumeJson(code,reply);
  }

  // Avatar URLs must also pass the caller's provider-specific identity checks.
  // The transport verifies their exact HTTPS origin and never adds a bearer.
  // On terminal success the caller owns bytes and must free it after use.
  int getBytes(BadgeHttpKind kind,const String &url,const char *ca,
               uint8_t *&bytes,size_t &length,String &contentType) {
    bytes=nullptr;length=0;contentType="";
    if(!avatarAllowed(kind,url))return -40;
    int code=pollOrStart(kind,url,String(),String(),ca,262144,false);
    if(code==BADGE_HTTP_PENDING || !completed_)return code;
    contentType=completed_->contentType;
    if(code==200) {
      bytes=completed_->bytes;length=completed_->length;
      completed_->bytes=nullptr;completed_->length=0;
    }
    finish();return code;
  }

 private:
  struct Job {
    BadgeHttpKind kind=BadgeHttpKind::NONE;
    String url,body,bearer,contentType;
    char *ca=nullptr;
    size_t caLength=0,limit=0,length=0;
    uint8_t *bytes=nullptr;
    bool post=false;
    int code=-1;
  };

  // HTTPClient's timeout alone resets on every received header. This wrapper
  // adds a total transaction deadline and a hard header-byte budget, so a
  // trickling or oversized response cannot hold the single worker indefinitely.
  // Each underlying connect/handshake/socket operation also has a timeout.
  class BoundedTls : public NetworkClientSecure {
   public:
    explicit BoundedTls(uint32_t deadline):deadline_(deadline){}
    void bodyBudget(size_t bytes) {remaining_=bytes;}
    bool aborted() const {return aborted_;}
    int available() override {return blocked()?0:NetworkClientSecure::available();}
    uint8_t connected() override {return blocked()?0:NetworkClientSecure::connected();}
    int read() override {uint8_t value=0;return read(&value,1)==1?value:-1;}
    int read(uint8_t *bytes,size_t length) override {
      if(blocked())return -1;
      if(length>remaining_)length=remaining_;
      int got=NetworkClientSecure::read(bytes,length);
      if(got>0)remaining_-=size_t(got);
      return got;
    }
    size_t write(uint8_t value) override {return write(&value,1);}
    size_t write(const uint8_t *bytes,size_t length) override {
      size_t written=0;
      while(written<length && !blocked()) {
        size_t amount=length-written;
        if(amount>1024)amount=1024;
        size_t sent=NetworkClientSecure::write(bytes+written,amount);
        if(!sent)break;
        written+=sent;
      }
      return written;
    }
   private:
    bool blocked() {
      if(aborted_)return true;
      if(!remaining_ || int32_t(millis()-deadline_)>=0) {
        aborted_=true;NetworkClientSecure::stop();return true;
      }
      return false;
    }
    uint32_t deadline_;
    size_t remaining_=8192;
    bool aborted_=false;
  };

  static bool headerSafe(const String &value) {
    for(size_t i=0;i<value.length();i++)if(uint8_t(value[i])<=32 || uint8_t(value[i])>=127)return false;
    return true;
  }
  static bool postAllowed(BadgeHttpKind kind,const String &url) {
    if(kind==BadgeHttpKind::PAIR_BEGIN)return url=="https://api.workos.com/user_management/authorize/device";
    return (kind==BadgeHttpKind::AUTH_REFRESH || kind==BadgeHttpKind::PAIR_POLL || kind==BadgeHttpKind::WORKSPACE_SESSION) &&
      url=="https://api.workos.com/user_management/authenticate";
  }
  static bool getAllowed(BadgeHttpKind kind,const String &url) {
    if(kind==BadgeHttpKind::WORKSPACE)return url=="https://auth.chan.dev/devices/workspace";
    if(kind==BadgeHttpKind::PROFILE_X)return url=="https://auth.chan.dev/devices/x";
    if(kind==BadgeHttpKind::PROFILE_LINKEDIN)return url=="https://auth.chan.dev/devices/linkedin";
    if(kind==BadgeHttpKind::PROFILE_GITHUB)return url=="https://auth.chan.dev/devices/github";
    return false;
  }
  static bool avatarAllowed(BadgeHttpKind kind,const String &url) {
    if(url.length()>2048 || !headerSafe(url))return false;
    for(size_t i=0;i<url.length();i++)if(url[i]=='\\' || url[i]=='#')return false;
    if(kind==BadgeHttpKind::AVATAR_X)return url.startsWith("https://pbs.twimg.com/profile_images/");
    if(kind==BadgeHttpKind::AVATAR_LINKEDIN)return url.startsWith("https://media.licdn.com/") || url.startsWith("https://media.licdn-ei.com/");
    if(kind==BadgeHttpKind::AVATAR_GITHUB)return url.startsWith("https://avatars.githubusercontent.com/u/");
    return false;
  }

  static void wipe(void *bytes,size_t length) {
    volatile uint8_t *cursor=static_cast<volatile uint8_t*>(bytes);
    while(length--)*cursor++=0;
  }
  static void wipeString(String &value) {
    if(value.length())wipe(const_cast<char*>(value.c_str()),value.length());
    value="";
  }
  static void destroy(Job *job) {
    if(!job)return;
    wipeString(job->body);wipeString(job->bearer);
    if(job->bytes) {wipe(job->bytes,job->limit+1);free(job->bytes);}
    if(job->ca)free(job->ca);
    delete job;
  }
  void releaseQueues() {
    if(requests_)vQueueDelete(requests_);
    if(responses_)vQueueDelete(responses_);
    requests_=nullptr;responses_=nullptr;
  }
  void finish() {
    destroy(completed_);completed_=nullptr;
    active_=false;discard_=false;activeKind_=BadgeHttpKind::NONE;
  }
  void collect() {
    if(!active_ || !responses_)return;
    Job *result=nullptr;
    if(!completed_ && xQueueReceive(responses_,&result,0)==pdTRUE)completed_=result;
    if(completed_ && discard_)finish();
  }
  int pollOrStart(BadgeHttpKind kind,const String &url,const String &body,
                  const String &bearer,const char *ca,size_t limit,bool post) {
    collect();
    if(active_) {
      if(discard_ || activeKind_!=kind || !completed_)return BADGE_HTTP_PENDING;
      return completed_->code;
    }
    if(!worker_ || !ca)return -50;
    size_t caLength=strnlen(ca,65537);
    if(!caLength || caLength>65536)return -40;
    Job *job=new(std::nothrow) Job;
    if(!job)return -43;
    job->kind=kind;job->url=url;job->body=body;job->bearer=bearer;
    job->limit=limit;job->post=post;job->caLength=caLength;
    job->ca=static_cast<char*>(ps_malloc(caLength+1));
    if(!job->ca || job->url!=url || job->body!=body || job->bearer!=bearer) {destroy(job);return -43;}
    memcpy(job->ca,ca,caLength+1);
    if(xQueueSend(requests_,&job,0)!=pdTRUE) {destroy(job);return -50;}
    active_=true;activeKind_=kind;discard_=false;
    return BADGE_HTTP_PENDING;
  }
  int consumeJson(int code,JsonDocument &reply) {
    if(code==BADGE_HTTP_PENDING || !completed_)return code;
    reply.clear();
    if(completed_->bytes && code>0) {
      // A const input forces ArduinoJson to copy strings before the response
      // buffer is scrubbed, including access/refresh tokens in auth responses.
      if(!completed_->contentType.startsWith("application/json") ||
          deserializeJson(reply,static_cast<const uint8_t*>(completed_->bytes),completed_->length))code=-45;
    }
    finish();return code;
  }

  static void workerEntry(void *context) {
    auto *self=static_cast<BadgeBackgroundHttp*>(context);
    for(;;) {
      Job *job=nullptr;
      if(xQueueReceive(self->requests_,&job,portMAX_DELAY)!=pdTRUE || !job)continue;
      execute(*job);
      wipeString(job->body);wipeString(job->bearer);
      free(job->ca);job->ca=nullptr;job->caLength=0;
      // Only one outstanding job exists, so its response queue is empty here.
      xQueueSend(self->responses_,&job,portMAX_DELAY);
    }
  }
  static void execute(Job &job) {
    BoundedTls tls(millis()+60000);
    tls.setCACert(job.ca);tls.setHandshakeTimeout(10);
    HTTPClient http;http.useHTTP10(true);http.setReuse(false);
    http.setConnectTimeout(10000);http.setTimeout(12000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    const char *keys[]={"Content-Type","Transfer-Encoding","Content-Encoding"};
    http.collectHeaders(keys,3);
    if(!http.begin(tls,job.url)) {job.code=-1;return;}
    http.addHeader("Accept-Encoding","identity");
    http.addHeader("User-Agent","chan-dev-devices/1.0 ESP32-S3");
    http.addHeader("Accept",job.post || job.bearer.length()?"application/json":"image/jpeg,image/png");
    if(job.post)http.addHeader("Content-Type","application/x-www-form-urlencoded");
    if(job.bearer.length())http.addHeader("Authorization","Bearer "+job.bearer);
    job.code=job.post?http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(job.body.c_str())),job.body.length()):http.GET();
    job.contentType=http.header("Content-Type");
    if(tls.aborted())job.code=-47;
    if(job.code<=0 || (!job.post && job.code!=200)) {http.end();return;}
    String transfer=http.header("Transfer-Encoding"),encoding=http.header("Content-Encoding");
    if((transfer.length() && transfer!="identity") || (encoding.length() && encoding!="identity")) {
      job.code=-41;http.end();return;
    }
    int expected=http.getSize();
    if(expected==0 || expected>int(job.limit)) {job.code=-42;http.end();return;}
    job.bytes=static_cast<uint8_t*>(ps_malloc(job.limit+1));
    if(!job.bytes) {job.code=-43;http.end();return;}
    tls.bodyBudget(job.limit+1);
    auto *stream=http.getStreamPtr();
    uint32_t deadline=millis()+15000;
    bool complete=false;
    while(int32_t(millis()-deadline)<0) {
      int available=stream->available();
      if(available>0) {
        if(job.length>=job.limit)break;
        size_t amount=size_t(available);
        if(amount>job.limit-job.length)amount=job.limit-job.length;
        if(expected>=0 && amount>size_t(expected)-job.length)amount=size_t(expected)-job.length;
        int got=stream->read(job.bytes+job.length,amount);
        if(got>0)job.length+=size_t(got);
        if(expected>=0 && job.length==size_t(expected)) {complete=true;break;}
      } else if(!stream->connected()) {
        complete=!tls.aborted() && (expected<0 || job.length==size_t(expected));break;
      }
      vTaskDelay(1);
    }
    http.end();
    if(!complete || !job.length) {job.code=-44;return;}
    job.bytes[job.length]=0;
  }

  QueueHandle_t requests_=nullptr,responses_=nullptr;
  TaskHandle_t worker_=nullptr;
  // These fields are owned exclusively by the main task.
  Job *completed_=nullptr;
  BadgeHttpKind activeKind_=BadgeHttpKind::NONE;
  bool active_=false,discard_=false;
};
