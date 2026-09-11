#include <Arduino.h>
#include <ArduinoJson.h>
#include <cassert>
#include <iostream>
#include <map>
#include <vector>
#include <ctime>
#include <algorithm>
inline void convertFromJson(JsonVariantConst src,String &dst){dst=src.as<const char*>()?src.as<const char*>() : "";}
inline bool canConvertFromJson(JsonVariantConst src,const String&){return src.is<const char*>();}
inline void convertToJson(const String &src,JsonVariant dst){dst.set(src.c_str());}
using std::min;
static uint32_t fakeMillis=1000;
static time_t fakeNow=1800000000;
uint32_t schedulerMillis(){return fakeMillis;}
time_t schedulerTime(time_t*){return fakeNow;}
#define millis schedulerMillis
#define time schedulerTime
static constexpr int BADGE_HTTP_PENDING=-10000,WL_CONNECTED=3;
enum class BadgeHttpKind:uint8_t{NONE,AUTH_REFRESH,PAIR_BEGIN,PAIR_POLL,WORKSPACE,WORKSPACE_SESSION,PROFILE_X,PROFILE_LINKEDIN,PROFILE_GITHUB,AVATAR_X,AVATAR_LINKEDIN,AVATAR_GITHUB};
enum ProfileProvider:uint8_t{PROFILE_X,PROFILE_LINKEDIN,PROFILE_GITHUB};
#include "../../firmware/devices_badge/account_paging.h"
#include "../../firmware/devices_badge/profile_urls.h"
int profileCalls=0;
static constexpr char CLIENT_ID[]="client_test",EXPECTED_ISSUER[]="https://api.workos.com",TOKEN_URL[]="https://api.workos.com/user_management/authenticate",WORKSPACE_URL[]="https://auth.chan.dev/devices/workspace";
enum Screen{BADGE,AUTH_SETTINGS};
struct Radio{int value=WL_CONNECTED;int status(){return value;}}WiFi;
struct SerialFake {void println(const char*){} template<class...Args>void printf(const char*,Args...){} }Serial;
struct PreferencesFake {
 std::map<std::string,std::string> values;
 bool remove(const char *key){return values.erase(key)>0;}
 String getString(const char *key,const char *fallback){auto i=values.find(key);return i==values.end()?String(fallback):String(i->second);}
 size_t putString(const char *key,const String &value){values[key]=value;return value.length();}
}settings;
struct MockHttp {
 BadgeHttpKind active=BadgeHttpKind::NONE;
 bool ready=false;
 int result=BADGE_HTTP_PENDING,canceledOwned=0;
 String response;
 std::vector<BadgeHttpKind> calls;
 BadgeHttpKind kind(){return active;}
 bool busy(){return active!=BadgeHttpKind::NONE;}
 void cancel(){if(busy())canceledOwned++;active=BadgeHttpKind::NONE;ready=false;}
 void own(BadgeHttpKind kind,int code=BADGE_HTTP_PENDING,const char *json="{}"){active=kind;result=code;ready=code!=BADGE_HTTP_PENDING;response=json;}
 int run(BadgeHttpKind kind,JsonDocument &reply){
  calls.push_back(kind);if(kind==BadgeHttpKind::PROFILE_X||kind==BadgeHttpKind::PROFILE_LINKEDIN||kind==BadgeHttpKind::PROFILE_GITHUB)profileCalls++;
  if(active==BadgeHttpKind::NONE){active=kind;return BADGE_HTTP_PENDING;}
  if(active!=kind||!ready)return BADGE_HTTP_PENDING;
  int code=result;active=BadgeHttpKind::NONE;ready=false;
  auto error=deserializeJson(reply,response.c_str());assert(!error);return code;
 }
 int postJson(BadgeHttpKind kind,const String&,const String&,JsonDocument &reply){return run(kind,reply);}
 int getJson(BadgeHttpKind kind,const String&,const String&,JsonDocument &reply){return run(kind,reply);}
}badgeHttp;
struct Canvas{
 bool allocated=false;
 void deleteSprite(){allocated=false;}
 void *getBuffer(){return allocated?this:nullptr;}
 void setPsram(bool){} void setColorDepth(int){} void createSprite(int,int){allocated=true;}
 void pushSprite(Canvas*,int,int){} void fillScreen(int){}
};
using M5Canvas=Canvas;
static constexpr int TFT_BLACK=0;
struct CachedProfile{bool connected=false,avatarReady=false,saved=false,refreshed=false;String name,handle,url,avatarUrl,remoteId,owner,workspace,state="unknown";Canvas avatar;}profileCache[3];
Canvas pendingAvatarCanvas;
Canvas *profileAvatar=nullptr;
ProfileProvider selectedProvider=PROFILE_LINKEDIN;
bool profileAvatarReady=false,expanded=false;
uint8_t profileProvider=255;
uint32_t profileCacheHits=0;
String profileName,profileHandle,profileUrl,profileOwner,profileWorkspace;
void syncBadgeDesign(){}
const char *providerId(ProfileProvider provider){return provider==PROFILE_LINKEDIN?"linkedin":provider==PROFILE_GITHUB?"github":"x";}
const char *providerProfileUrl(ProfileProvider provider){return provider==PROFILE_LINKEDIN?"https://auth.chan.dev/devices/linkedin":provider==PROFILE_GITHUB?"https://auth.chan.dev/devices/github":"https://auth.chan.dev/devices/x";}
void persistCachedProfile(ProfileProvider){}
int loadProfileAvatar(ProfileProvider,const String&,Canvas&){assert(false&&"avatar decoding is outside this scheduler fixture");return 0;}
bool authenticated=false,profileRefreshActive=false,profileRefreshRetry=false,profilePending=true,profilesWarmed=false,profileMetadataReady=false,profileReady=false,portalActive=false,needPair=false,redraw=false,showBadge=true,providerPreferenceDirty=false;
uint8_t profileRefreshCursor=0;
uint32_t nextNetworkTry=0,nextProfileTry=0,pairDeadline=0,nextPoll=0,pollInterval=5000,profileFetchCount=0,avatarFetchCount=0;
int64_t accessExpires=0;
Screen screen=BADGE;
String deviceCode,pairUrl,userCode,refreshToken,accessToken,accountEmail,sessionId,currentUserId,currentOrgId,profileState;
String refreshWorkspace,pendingName,pendingHandle,pendingUrl,pendingRemoteId,pendingAvatarUrl,pendingProfileOwner,pendingProfileWorkspace;
int resets=0,loads=0,storageClears=0;
String stateTitle;
bool due(uint32_t t){return int32_t(millis()-t)>=0;}
void state(const String &title,const String&){stateTitle=title;}
String formEscape(const String &s){return s;}
void clearStoredProfiles(){storageClears++;}
void clearCachedProfile(ProfileProvider p,const String&,bool=true){profileCache[p].connected=false;}
bool mountProfileStore(){return true;}
void loadStoredProfile(ProfileProvider p,const String &owner,const String &org,CachedProfile &cache){loads++;cache.connected=true;cache.owner=owner;cache.workspace=org;}
bool decodeClaims(const String &token,JsonDocument &claims){
 if(token!="verified-new-access")return false;
 claims["client_id"]=CLIENT_ID;claims["iss"]=EXPECTED_ISSUER;claims["sub"]="user_test";claims["org_id"]="org_test";claims["exp"]=int64_t(fakeNow+3600);claims["sid"]="session_test";return true;
}
