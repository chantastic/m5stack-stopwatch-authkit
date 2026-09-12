#include <Arduino.h>
#include <ArduinoJson.h>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <map>
#include <vector>
#include <iostream>
#define VOICE_REPLY_UI_HOST_TEST
#include "../../firmware/devices_badge/voice_reply_state.h"
#include "../../firmware/devices_badge/voice_reply_ui.h"

enum class BadgeHttpKind : uint8_t {NONE,AUTH_REFRESH,PAIR_BEGIN,PAIR_POLL,WORKSPACE,WORKSPACE_SESSION,
  PROFILE_X,PROFILE_LINKEDIN,PROFILE_GITHUB,AVATAR_X,AVATAR_LINKEDIN,AVATAR_GITHUB,
  REPLY_INBOX,TRANSCRIPTION,REPLY_SEND,REPLY_STATUS};
constexpr int BADGE_HTTP_PENDING=-10000;
struct BadgeVoiceEndpointConfig {
  const char *origin=nullptr,*inboxUrl=nullptr,*transcriptionUrl=nullptr,*replyUrl=nullptr,*ca=nullptr,*statusUrl=nullptr,*deepgramTranscriptionUrl=nullptr;
};
constexpr char CLIENT_ID[]="client_fixture",ROOT_CA[]="fixture-ca";
enum Screen {BADGE,SETTINGS,REPLIES};
Screen screen=BADGE;
bool authenticated=true,portalActive=false,profileRefreshActive=false,showBadge=true,expanded=false,redraw=false;
String accessToken="fixture-access",currentUserId="user_fixture",currentOrgId="org_fixture";
time_t accessExpires=2000000000;
time_t fixtureTime(time_t*) {return 1900000000;}
constexpr int WL_CONNECTED=3;
struct FakeWifi {int value=WL_CONNECTED;int status() const {return value;}} WiFi;
struct FakeButton {bool held=false;bool isPressed() const {return held;}};
struct FakeDisplay {unsigned draws=0;void display(){++draws;}};
struct FakeM5 {FakeButton BtnA,BtnB;FakeDisplay Display;} M5;
struct FakeGesture {void reset(bool,bool){}} buttonGesture;
struct FakeSerial {
  String output;
  template<class...Args>void printf(const char *format,Args...args){char bytes[1024];std::snprintf(bytes,sizeof(bytes),format,args...);output+=bytes;}
  void println(const char *value){output+=value;output+='\n';}
} Serial;
bool due(uint32_t value) {return int32_t(millis()-value)>=0;}
void stopPortal() {portalActive=false;}
const char *currentScreenName(){return screen==REPLIES?"replies":"other";}
void esp_fill_random(uint8_t *bytes,size_t length) {static uint8_t generation=0;for(size_t i=0;i<length;i++)bytes[i]=uint8_t(++generation);}
unsigned renderCount=0;
VoiceReplyView lastDraw;
void drawVoiceReply(const VoiceReplyView &view) {lastDraw=view;++renderCount;}
std::vector<std::string> events;
struct FakeSettings {
  std::map<std::string,String> values;
  bool failWrite=false,failReadback=false,failRemove=false;
  bool isKey(const char *key) const {return values.count(key);}
  size_t putString(const char *key,const String &value) {
    events.push_back("persist");if(failWrite)return 0;values[key]=value;return value.length();
  }
  String getString(const char *key,const String &fallback) const {
    if(failReadback)return "invalid readback";
    auto found=values.find(key);return found==values.end()?fallback:found->second;
  }
  bool remove(const char *key) {if(failRemove)return false;return values.erase(key);}
} settings;

// The actual recorder and HTTP worker have separate ownership tests. Here the
// fake boundaries expose precise completion/failure schedules to the real UI
// controller, without audio drivers, threads, NVS, or network access.
class VoiceRecorder {
 public:
  static constexpr int OK=0;
  unsigned starts=0,stops=0,cancels=0;
  bool active=false,ready=false;
  int errorCode=0;
  uint32_t duration=0;
  bool begin(){return true;}
  bool start(){if(active||ready)return false;active=true;duration=0;errorCode=0;++starts;return true;}
  void stop(){if(active)++stops;}
  void cancel(){++cancels;active=ready=false;errorCode=0;}
  bool busy() const{return active;}
  bool recording() const{return active;}
  uint32_t durationMs() const{return duration;}
  uint32_t sampleCount() const{return duration*16;}
  uint32_t peak() const{return 1000;}
  uint32_t meanAbs() const{return 100;}
  uint32_t workerStackFree() const{return 6000;}
  int error() const{return errorCode;}
  void finish(uint32_t milliseconds){duration=milliseconds;active=false;ready=true;}
  bool take(uint8_t *&wav,size_t &bytes,uint32_t &milliseconds) {
    wav=nullptr;bytes=milliseconds=0;if(!ready)return false;
    ready=false;milliseconds=duration;bytes=44+duration*32;
    wav=static_cast<uint8_t*>(std::calloc(1,bytes));assert(wav);return true;
  }
};
struct FakeHttp {
  BadgeHttpKind active=BadgeHttpKind::NONE;
  bool terminal=false,cancelled=false,failEnqueue=false;
  int code=200;
  String response,lastBody,lastKey,lastBearer,lastUrl;
  unsigned sendStarts=0,statusStarts=0,wavStarts=0,inboxStarts=0,cancels=0;
  uint8_t *audio=nullptr;size_t audioBytes=0;
  bool configureVoiceEndpoints(const BadgeVoiceEndpointConfig &config) {return config.statusUrl && config.deepgramTranscriptionUrl;}
  bool busy() const{return active!=BadgeHttpKind::NONE;}
  BadgeHttpKind kind() const{return active;}
  void clearAudio(){if(audio){std::memset(audio,0,audioBytes);std::free(audio);}audio=nullptr;audioBytes=0;}
  void cancel(){if(busy()){++cancels;cancelled=true;}}
  void complete(int result,const String &json) {
    assert(busy());clearAudio();
    if(cancelled){active=BadgeHttpKind::NONE;cancelled=false;return;}
    code=result;response=json;terminal=true;
  }
  int request(BadgeHttpKind requested,const String &url,const String &body,const String &bearer,const String &key,JsonDocument &reply) {
    if(busy()) {
      if(active!=requested || !terminal)return BADGE_HTTP_PENDING;
      reply.clear();if(!response.isEmpty())assert(!deserializeJson(reply,response));
      int result=code;active=BadgeHttpKind::NONE;terminal=false;return result;
    }
    if(failEnqueue)return -50;
    active=requested;lastBody=body;lastBearer=bearer;lastKey=key;lastUrl=url;
    if(requested==BadgeHttpKind::REPLY_SEND){++sendStarts;events.push_back("send");assert(settings.isKey("voice_pending"));}
    else if(requested==BadgeHttpKind::REPLY_STATUS){++statusStarts;events.push_back("status");}
    else if(requested==BadgeHttpKind::TRANSCRIPTION)++wavStarts;
    else if(requested==BadgeHttpKind::REPLY_INBOX)++inboxStarts;
    return BADGE_HTTP_PENDING;
  }
  int getJson(BadgeHttpKind kind,const String &url,const String &bearer,JsonDocument &reply){return request(kind,url,"",bearer,"",reply);}
  int postDeviceJson(BadgeHttpKind kind,const String &url,const String &body,const String &bearer,const String &key,JsonDocument &reply){return request(kind,url,body,bearer,key,reply);}
  int getReplyStatus(const String &url,const String &bearer,const String &key,JsonDocument &reply){return request(BadgeHttpKind::REPLY_STATUS,url,"",bearer,key,reply);}
  int postWav(const String &url,const String &bearer,uint8_t *&bytes,size_t length,JsonDocument &reply) {
    bool idle=!busy();int result=request(BadgeHttpKind::TRANSCRIPTION,url,"",bearer,"",reply);
    if(idle && result==BADGE_HTTP_PENDING){assert(bytes);audio=bytes;audioBytes=length;bytes=nullptr;}
    return result;
  }
} badgeHttp;
#define time fixtureTime
#include "../../firmware/devices_badge/voice_reply.h"
#undef time

using Kind=BadgeHttpKind;
using Stage=VoiceReplyStage;
using Gate=VoiceSendState;
void reset(bool preserveSettings=false) {
  voiceFreeClip();badgeHttp.clearAudio();badgeHttp=FakeHttp{};voiceRecorder=VoiceRecorder{};
  if(!preserveSettings)settings=FakeSettings{};
  events.clear();voiceHold=VoiceHoldGesture{};voiceSendGate=VoiceSendGate{};voiceView=VoiceReplyView{};
  for(auto &item:voiceMentions)item=VoiceMention{};
  voiceOwner=voiceWorkspace=voiceSender=voiceConnection="";
  voiceKey=voicePendingOwner=voicePendingWorkspace=voicePendingTarget=voicePendingSender=voicePendingConnection="";
  voiceRequestBearer=voiceRequestBody="";voiceRequest=Kind::NONE;
  voiceRequestStarted=voiceConfigured=voiceXai=voiceDeepgram=voiceUseDeepgram=false;
  voiceFallbackOffered=voiceStorageBlocked=voiceCaptureExpected=false;
  voicePostingAllowed=voiceDraftValid=false;
  voiceRecordRender=voiceReviewedThrough=voiceDiagnosticStop=0;voiceDiagnosticLocal=voiceSendTouchArmed=false;
  authenticated=true;portalActive=profileRefreshActive=false;showBadge=true;expanded=redraw=false;
  accessToken="fixture-access";currentUserId="user_fixture";currentOrgId="org_fixture";accessExpires=2000000000;
  WiFi.value=WL_CONNECTED;M5=FakeM5{};screen=BADGE;fixtureMillis=1000;renderCount=0;lastDraw=VoiceReplyView{};Serial.output="";
  setupVoiceReply();assert(voiceConfigured);
}
void readyInbox() {
  screen=REPLIES;voiceOwner=currentUserId;voiceWorkspace=currentOrgId;
  voiceSender="sender123";voiceConnection="connection_fixture";voiceXai=voiceDeepgram=voicePostingAllowed=true;
  voiceView.count=2;voiceView.index=0;
  voiceMentions[0]={"target123","An incoming mention.","@example"};
  voiceMentions[1]={"target456","A different incoming mention.","@second"};
  voiceShowMention();renderVoiceReply();
}
void draft(const String &text="Reviewed reply.") {
  readyInbox();assert(voiceSendGate.setDraft());voiceView.transcript=text;voiceReviewedThrough=0;voiceDraftValid=true;voiceStage(Stage::REVIEW);renderVoiceReply();
}
void touch(voice_reply_ui::Rect rect,bool begin=true) {
  const int x=rect.x+rect.w/2,y=rect.y+rect.h/2;
  if(begin)beginVoiceTouch(x,y);handleVoiceTap(x,y);
}
void approveSend() {touch(voice_reply_ui::RIGHT);assert(voiceRequest==Kind::REPLY_SEND);}
String receipt(const char *state,bool correct=true) {
  JsonDocument reply;reply["state"]=state;reply["key"]=voiceKey;reply["targetId"]=correct?voicePendingTarget:String("wrong_target");reply["senderId"]=voicePendingSender;
  String text;serializeJson(reply,text);return text;
}
void completeRequest(int code,const String &json) {badgeHttp.complete(code,json);handleVoiceReply();}

void durableSendChecks() {
  reset();draft();assert(voiceView.canSend);approveSend();
  assert(settings.isKey("voice_pending") && badgeHttp.sendStarts==0 && events==std::vector<std::string>{"persist"});
  JsonDocument saved;assert(!deserializeJson(saved,settings.getString("voice_pending","")));
  assert(saved["key"].as<String>()==voiceKey && saved["target"].as<String>()=="target123");
  assert(!saved.containsKey("text") && !saved.containsKey("transcript") && !saved.containsKey("access_token"));
  handleVoiceReply();assert(badgeHttp.sendStarts==1 && voiceRequestStarted && events==std::vector<std::string>({"persist","send"}));
  JsonDocument sent;assert(!deserializeJson(sent,badgeHttp.lastBody));
  assert(sent["targetId"].as<String>()=="target123" && sent["text"].as<String>()=="Reviewed reply.");
  assert(sent["senderId"].as<String>()=="sender123" && sent["connectionId"].as<String>()=="connection_fixture");
  accessToken="rotated-fixture-access";assert(voiceRequestBearer=="fixture-access");
  for(int i=0;i<100;i++)handleVoiceReply();assert(badgeHttp.sendStarts==1);
  const String persistedKey=voiceKey;completeRequest(-44,"");
  assert(voiceView.uncertain && voiceUnresolved() && voiceKey==persistedKey && voiceSendGate.state()==Gate::UNCERTAIN);
  for(int i=0;i<100;i++){handleVoiceReply();voiceBeginSend();}assert(badgeHttp.sendStarts==1);

  reset();draft();settings.failWrite=true;touch(voice_reply_ui::RIGHT);
  assert(voiceStorageBlocked && voiceRequest==Kind::NONE && badgeHttp.sendStarts==0 && voiceView.uncertain);
  reset();draft();settings.failReadback=true;touch(voice_reply_ui::RIGHT);
  assert(voiceStorageBlocked && voiceRequest==Kind::NONE && badgeHttp.sendStarts==0);
  reset();draft();voiceView.transcript=String(3000,'\\');voiceBeginSend();
  assert(!settings.isKey("voice_pending") && voiceKey.isEmpty() && badgeHttp.sendStarts==0 && voiceRequest==Kind::NONE);
  reset();draft();approveSend();badgeHttp.failEnqueue=true;handleVoiceReply();
  assert(badgeHttp.sendStarts==0 && voiceRequest==Kind::NONE && !voiceUnresolved() && !settings.isKey("voice_pending"));
  for(int i=0;i<20;i++)handleVoiceReply();assert(badgeHttp.sendStarts==0);
}

void reviewAndTouchChecks() {
  reset();draft(String(361,'a'));
  assert(voiceReplyPageCount(voiceView)==3 && voiceReviewedThrough==1 && !voiceView.canSend);
  touch(voice_reply_ui::RIGHT);assert(voiceRequest==Kind::NONE);
  touch(voice_reply_ui::PAGER);assert(voiceView.textPage==1 && voiceReviewedThrough==1);
  renderVoiceReply();assert(voiceReviewedThrough==2 && !voiceView.canSend);
  touch(voice_reply_ui::PAGER);renderVoiceReply();assert(voiceReviewedThrough==3 && voiceView.canSend);
  approveSend();assert(badgeHttp.sendStarts==0); // Gesture only queues; worker is polled separately.

  reset();readyInbox();beginVoiceTouch(302,396);draft();
  touch(voice_reply_ui::RIGHT,false);assert(voiceRequest==Kind::NONE); // Contact began on previous screen.
  beginVoiceTouch(302,396);voiceStage(Stage::REVIEW);renderVoiceReply();
  touch(voice_reply_ui::RIGHT,false);assert(voiceRequest==Kind::NONE); // Stage transition invalidated the arm.
  beginVoiceTouch(302,396);authenticated=false;touch(voice_reply_ui::RIGHT,false);
  assert(voiceRequest==Kind::NONE && !settings.isKey("voice_pending"));
  authenticated=true;renderVoiceReply();approveSend();

  reset();draft();voicePostingAllowed=false;renderVoiceReply();
  assert(voiceCanRecord() && !voiceView.canSend);voiceBeginSend();assert(voiceRequest==Kind::NONE);
  assert(voiceView.progress=="RECONNECT X FOR REPLY PERMISSION");
  reset();draft();redraw=false;WiFi.value=0;handleVoiceReply();assert(redraw);renderVoiceReply();
  assert(!voiceView.canSend && !voiceView.canRecord);
  redraw=false;WiFi.value=WL_CONNECTED;handleVoiceReply();assert(redraw);renderVoiceReply();
  assert(voiceView.canSend && voiceView.canRecord);
}

void captureChecks() {
  for(int reason=0;reason<4;reason++) {
    reset();readyInbox();voiceStartRecording();assert(voiceCaptureExpected && voiceRecorder.starts==1);
    if(reason==0)authenticated=false;
    if(reason==1)WiFi.value=0;
    if(reason==2)accessExpires=1800000000;
    if(reason==3)currentUserId="user_changed";
    handleVoiceReply();assert(!voiceCaptureExpected && voiceRecorder.cancels==1 && badgeHttp.wavStarts==0);
    assert(voiceView.stage==Stage::ERROR);
  }
  reset();readyInbox();M5.BtnB.held=true;handleVoiceButtons();fixtureMillis+=250;handleVoiceButtons();
  assert(voiceRecorder.starts==1 && voiceCaptureExpected);
  voiceRecorder.finish(30000);handleVoiceReply();
  assert(!voiceCaptureExpected && voiceView.stage==Stage::TRANSCRIBING && badgeHttp.wavStarts==1 && !voiceWav);
  for(int i=0;i<20;i++)handleVoiceButtons();assert(voiceRecorder.starts==1);
  JsonDocument result;result["context"]["userId"]=currentUserId;result["context"]["organizationId"]=currentOrgId;
  result["state"]="transcribed";result["text"]="A short reviewed reply.";result["replyValid"]=true;String text;serializeJson(result,text);
  completeRequest(200,text);renderVoiceReply();assert(voiceView.stage==Stage::REVIEW && voiceView.canSend);
  handleVoiceButtons();assert(voiceRecorder.starts==1);M5.BtnB.held=false;handleVoiceButtons();
  assert(badgeHttp.sendStarts==0 && voiceRecorder.starts==1); // Release never sends.

  reset();readyInbox();voiceStartRecording();voiceRecorder.finish(300);handleVoiceReply();
  assert(voiceView.stage==Stage::ERROR && badgeHttp.wavStarts==0 && !voiceWav);
  reset();readyInbox();voiceStartRecording();M5.BtnB.held=M5.BtnA.held=true;handleVoiceButtons();
  assert(screen==SETTINGS && !voiceCaptureExpected && voiceRecorder.cancels==1 && badgeHttp.wavStarts==0);
}

void receiptChecks() {
  reset();draft();approveSend();leaveVoiceReplies(false);
  assert(screen==BADGE && !voiceUnresolved() && !settings.isKey("voice_pending") && badgeHttp.sendStarts==0);
  reset();draft();approveSend();handleVoiceReply();const String key=voiceKey;
  M5.BtnA.held=M5.BtnB.held=true;handleVoiceButtons();
  assert(screen==SETTINGS && voiceKey==key && voiceUnresolved() && settings.isKey("voice_pending") && badgeHttp.cancels==1);
  badgeHttp.complete(200,receipt("sent")); // Canceled worker completion must not clear its persisted receipt.
  assert(voiceKey==key && voiceUnresolved());
  reset(true);assert(voiceUnresolved() && voiceSendGate.state()==Gate::UNCERTAIN);
  enterVoiceReplies();assert(voiceView.uncertain);touch(voice_reply_ui::CENTER);handleVoiceReply();
  assert(badgeHttp.statusStarts==1 && badgeHttp.sendStarts==0 && voiceOwner==currentUserId);
  completeRequest(404,receipt("not_found"));assert(voiceUnresolved() && voiceKey==key);
  for(int i=0;i<20;i++)handleVoiceReply();assert(badgeHttp.sendStarts==0 && badgeHttp.statusStarts==1);
  touch(voice_reply_ui::CENTER);handleVoiceReply();completeRequest(200,receipt("sent",false));
  assert(voiceView.uncertain && voiceUnresolved());
  touch(voice_reply_ui::CENTER);handleVoiceReply();completeRequest(200,receipt("sent"));
  assert(voiceView.stage==Stage::SENT && !voiceUnresolved() && !settings.isKey("voice_pending"));

  reset();draft();approveSend();handleVoiceReply();completeRequest(503,receipt("unknown"));
  reset(true);currentUserId="other_user";enterVoiceReplies();touch(voice_reply_ui::CENTER);handleVoiceReply();
  assert(badgeHttp.statusStarts==0 && badgeHttp.sendStarts==0 && settings.isKey("voice_pending"));
  reset(true);enterVoiceReplies();touch(voice_reply_ui::CENTER);handleVoiceReply();settings.failRemove=true;
  completeRequest(200,receipt("sent"));assert(voiceStorageBlocked && voiceUnresolved() && settings.isKey("voice_pending"));
}

void fallbackAndDiagnosticsChecks() {
  reset();readyInbox();voiceStartRecording();voiceRecorder.finish(1000);handleVoiceReply();
  completeRequest(-44,"");assert(voiceView.stage==Stage::ERROR && voiceFallbackOffered && !voiceWav && badgeHttp.wavStarts==1);
  touch(voice_reply_ui::CENTER);assert(voiceUseDeepgram && voiceView.stage==Stage::INBOX && voiceRecorder.starts==1);
  voiceStartRecording();assert(voiceRecorder.starts==2);voiceRecorder.finish(1000);handleVoiceReply();
  assert(badgeHttp.wavStarts==2 && badgeHttp.lastUrl==VOICE_DEEPGRAM_URL);
  completeRequest(200,"{\"state\":\"transcribed\",\"text\":\"unverified text\"}");
  assert(voiceView.stage==Stage::ERROR && voiceView.transcript.isEmpty() && !voiceView.canSend);

  // Read-only X permission must leave dictation available, but canSend must be
  // a real true boolean from the verified inbox response before posting opens.
  for(int allowed=0;allowed<3;allowed++) {
    reset();enterVoiceReplies();handleVoiceReply();
    JsonDocument inbox;inbox["context"]["userId"]=currentUserId;inbox["context"]["organizationId"]=currentOrgId;
    inbox["state"]="ready";inbox["transcription"]["xai"]=true;inbox["transcription"]["deepgram"]=true;
    inbox["sender"]["id"]="sender123";inbox["sender"]["connectionId"]="connection_fixture";
    if(allowed==2)inbox["canSend"]="true";else inbox["canSend"]=allowed==1;
    auto mention=inbox["items"].to<JsonArray>().add<JsonObject>();mention["id"]="target123";mention["text"]="A mention";mention["author"]["username"]="example";
    String encoded;serializeJson(inbox,encoded);completeRequest(200,encoded);renderVoiceReply();
    assert(voiceView.count==1 && voiceCanRecord() && voicePostingAllowed==(allowed==1));
  }
  for(int valid=0;valid<3;valid++) {
    reset();readyInbox();JsonDocument result;result["state"]="transcribed";
    result["context"]["userId"]=currentUserId;result["context"]["organizationId"]=currentOrgId;
    result["text"]="The complete transcript remains available to review.";
    if(valid==2)result["replyValid"]="true";else result["replyValid"]=valid==1;
    voiceConsume(Kind::TRANSCRIPTION,200,result);renderVoiceReply();
    assert(voiceView.stage==Stage::REVIEW && voiceView.transcript==result["text"].as<String>());
    assert(voiceCanRecord() && voiceView.canSend==(valid==1));
    if(valid!=1){assert(voiceView.progress=="RECORD A SHORTER REPLY");voiceBeginSend();assert(voiceRequest==Kind::NONE);}
  }

  reset();readyInbox();JsonDocument command;command["action"]="record_test";command["duration_ms"]=499;
  voiceDiagnostic(command);assert(voiceRecorder.starts==0);command["duration_ms"]=5001;voiceDiagnostic(command);assert(voiceRecorder.starts==0);
  command["duration_ms"]=1000;voiceDiagnostic(command);assert(voiceRecorder.starts==1 && voiceCaptureExpected && !voiceDiagnosticLocal);
  fixtureMillis+=1000;handleVoiceReply();assert(voiceRecorder.stops==1);
  voiceRecorder.finish(1000);handleVoiceReply();assert(badgeHttp.wavStarts==1 && badgeHttp.sendStarts==0);
  reset();readyInbox();command["action"]="mic_test";voiceDiagnostic(command);assert(voiceDiagnosticLocal);
  voiceRecorder.finish(1000);handleVoiceReply();assert(badgeHttp.wavStarts==0 && !voiceWav);
  reset();draft("  A  known\nPHRASE. ");command.clear();command["action"]="verify_transcript";command["expected"]="a known phrase.";
  Serial.output="";voiceDiagnostic(command);assert(Serial.output.find("match=1")!=String::npos);
  assert(Serial.output.find("known")==String::npos && Serial.output.find("fixture-access")==String::npos);
  command["expected"]="a different phrase.";Serial.output="";voiceDiagnostic(command);assert(Serial.output.find("match=0")!=String::npos);
  command["expected"]=String(161,'x');Serial.output="";voiceDiagnostic(command);assert(Serial.output=="COMMAND_REJECTED\n");
  command["action"]="send";Serial.output="";voiceDiagnostic(command);assert(Serial.output=="COMMAND_REJECTED\n" && badgeHttp.sendStarts==0);
}

int main() {
  durableSendChecks();
  reviewAndTouchChecks();captureChecks();receiptChecks();fallbackAndDiagnosticsChecks();
  reset();
  std::cout<<"Voice controller: durable intent before send, bounded serialized text, fresh reviewed taps, posting permission, recording/context cancellation, 30-second completion, no replay, status reconciliation, fresh fallback audio, and private bounded diagnostics passed.\n";
}
