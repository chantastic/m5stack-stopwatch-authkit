#include <M5Unified.h>
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <time.h>
#include "trust.h"
#include "avatar_trust.h"
#include "background_http.h"
BadgeBackgroundHttp badgeHttp;
#if __has_include("linkedin_avatar_trust.h")
#include "linkedin_avatar_trust.h"
#define HAS_LINKEDIN_AVATAR_TRUST 1
#endif
#include "orientation_filter.h"
#include "button_gesture.h"
#include "badge_styles.h"
SET_LOOP_TASK_STACK_SIZE(32768);

static constexpr char CLIENT_ID[]="client_01M24S76WQHXQ6QJJ7S83CMR3F";
static constexpr char EXPECTED_ISSUER[]="https://api.workos.com";
static constexpr char TOKEN_URL[]="https://api.workos.com/user_management/authenticate";
Preferences settings;
enum Screen { BADGE, SETTINGS, WIFI_SETTINGS, AUTH_SETTINGS, PROFILE_SETTINGS };
enum ProfileProvider : uint8_t { PROFILE_X=0, PROFILE_LINKEDIN=1, PROFILE_GITHUB=2 };
#include "account_paging.h"
ProfileProvider selectedProvider=PROFILE_LINKEDIN,settingsProvider=PROFILE_LINKEDIN;
uint8_t profileProvider=255;
const char *providerName(ProfileProvider provider);
const char *providerId(ProfileProvider provider);
const char *providerConnectionsUrl(ProfileProvider provider);
const char *providerName(ProfileProvider provider) {return provider==PROFILE_LINKEDIN?"LinkedIn":provider==PROFILE_GITHUB?"GitHub":"X";}
const char *providerId(ProfileProvider provider) {return provider==PROFILE_LINKEDIN?"linkedin":provider==PROFILE_GITHUB?"github":"x";}
const char *providerConnectionsUrl(ProfileProvider provider) {return provider==PROFILE_LINKEDIN?
  "https://auth.chan.dev/connections#linkedin":provider==PROFILE_GITHUB?"https://auth.chan.dev/connections#github":"https://auth.chan.dev/connections";}
const char *profileProviderName() {return providerName(selectedProvider);}
const char *profileProviderId() {return providerId(selectedProvider);}
const char *profileConnectionsUrl() {return providerConnectionsUrl(selectedProvider);}

struct CachedProfile {
  String name,handle,url,avatarUrl,remoteId,owner,workspace,state="unknown";
  bool connected=false,avatarReady=false,saved=false,refreshed=false;
  M5Canvas avatar;
  CachedProfile():avatar(&M5.Display){}
};
CachedProfile profileCache[ACCOUNT_PROVIDER_COUNT];
BadgeButtonGesture buttonGesture;
static_assert(ACCOUNT_PROVIDER_COUNT==BADGE_STYLE_PROVIDER_COUNT,"Style storage must match the provider set");
BadgeStyles badgeStyles,savedBadgeStyles;
uint8_t selectedBadgeStyle() {return badgeStyles.style(uint8_t(selectedProvider));}

Screen screen=BADGE;
bool expanded=false, showBadge=true, authenticated=false;
uint8_t badgeDesign=BADGE_STYLE_COUNT;
bool stylePreferenceDirty=false;
uint32_t stylePreferenceSaveAfter=0;
void syncBadgeDesign() {badgeDesign=badgeStyles.design(uint8_t(selectedProvider));}
bool providerPreferenceDirty=false;
ProfileProvider pendingProviderPreference=PROFILE_LINKEDIN;
uint32_t providerPreferenceSaveAfter=0;
String statusText="Wi-Fi setup", detailText="Provision over USB";
String deviceCode,userCode,pairUrl,refreshToken,accessToken,accountEmail,sessionId,currentUserId;
String currentOrgId,profileName,profileHandle,profileUrl,profileState="loading",profileOwner,profileWorkspace;
bool profileReady=false,profileAvatarReady=false,profilePending=true,profilesWarmed=false;
uint32_t profileFetchCount=0,avatarFetchCount=0,profileCacheHits=0;
uint32_t nextProfileTry=0;
bool profileRefreshActive=false;
uint32_t cacheBootReadyMs=0;
M5Canvas *profileAvatar=&profileCache[PROFILE_LINKEDIN].avatar;
String serialInput;
uint32_t pairDeadline=0,nextPoll=0,pollInterval=5000,nextNetworkTry=0,lastStatus=0,lastInteraction=0;
uint32_t monitorUntil=0,lastLoopStart=0,idleMaxGap=0,inputPresses=0;
bool foregroundOperation=false;
OrientationFilter orientation;
uint32_t lastOrientationPoll=0,lastOrientationSample=0,orientationChanges=0;
float orientationAccelX=0,orientationAccelY=0,orientationAccelZ=0;
bool orientationSampleReady=false;
int64_t accessExpires=0;
bool wifiStarted=false,timeStarted=false,needPair=false,redraw=true,serialOverflow=false,offlineTestBoot=false;
void clearProfile(const String &reason);
void queueProfileRefresh();
void handleProfile();
void renderProfileSettings();
String cachedProfileStatus(ProfileProvider provider);
void selectProfileProvider(ProfileProvider provider,bool remember);
void selectProfileSettingsProvider(ProfileProvider provider);
bool cachedProfileIsReady(ProfileProvider provider);
uint8_t availableProfileMask();
uint8_t availableProfileCount();
uint8_t currentProfilePage();
void activateCachedProfile(ProfileProvider provider);
void showBestCachedProfile();
void pageAccounts(int step,bool remember);
void dispatchButtonAction(BadgeButtonAction action,bool remember);
#include "badge.h"
void renderStatus();
void state(const String &title,const String &detail);
bool due(uint32_t deadline);
#include "settings.h"

const char *currentScreenName() {
  switch(screen) {case BADGE:return "badge";case SETTINGS:return "settings";case WIFI_SETTINGS:return "wifi_settings";case AUTH_SETTINGS:return "auth_settings";case PROFILE_SETTINGS:return "profile_settings";}
  return "unknown";
}
void reportBadgeDesign() {
  Serial.printf("BADGE_DESIGN index=%u total=%u style=%u active_style_count=%u provider=%s name=%s\n",
    unsigned(badgeDesign),unsigned(BADGE_DESIGN_COUNT),unsigned(selectedBadgeStyle()),unsigned(BADGE_STYLE_COUNT),
    profileProviderId(),BADGE_DESIGN_NAMES[badgeDesign]);
}
void rememberCurrentStyle(bool remember) {
  if(!remember)return;
  savedBadgeStyles.select(uint8_t(selectedProvider),selectedBadgeStyle());
  stylePreferenceDirty=true;stylePreferenceSaveAfter=millis()+1500;
}
void selectBadgeStyle(int index,int step,bool remember) {
  bool accepted=step?badgeStyles.step(uint8_t(selectedProvider),step):badgeStyles.selectDesign(uint8_t(selectedProvider),index);
  if(!accepted) {Serial.println("COMMAND_REJECTED");return;}
  syncBadgeDesign();rememberCurrentStyle(remember);
  stopPortal();screen=BADGE;showBadge=true;expanded=false;lastInteraction=millis();redraw=true;
  reportBadgeDesign();
}
void rememberCurrentProvider(bool remember) {
  if(!remember)return;
  pendingProviderPreference=selectedProvider;providerPreferenceDirty=true;providerPreferenceSaveAfter=millis()+1500;
}
void reportAccountPage() {
  Serial.printf("BADGE_PAGE provider=%s page=%u available_count=%u fetch_count=%u avatar_fetch_count=%u cache_hits=%u profiles_pending=%u\n",
    profileProviderId(),unsigned(currentProfilePage()),unsigned(availableProfileCount()),unsigned(profileFetchCount),
    unsigned(avatarFetchCount),unsigned(profileCacheHits),unsigned(profilePending));
}
void pageAccounts(int step,bool remember) {
  if(step!=-1 && step!=1 && step!=0)return;
  bool fromBadge=screen==BADGE;
  stopPortal();screen=BADGE;showBadge=true;expanded=false;
  if(fromBadge && step) {
    uint8_t next=nextAccountPage(uint8_t(selectedProvider),step,availableProfileMask());
    if(next!=NO_ACCOUNT_PAGE) {
      bool changed=uint8_t(selectedProvider)!=next;
      activateCachedProfile(ProfileProvider(next));
      if(changed)rememberCurrentProvider(remember);
    }
  }
  lastInteraction=millis();redraw=true;
  reportAccountPage();
}
// Physical gestures and serial diagnostics enter through this same dispatch.
void dispatchButtonAction(BadgeButtonAction action,bool remember) {
  if(action==BadgeButtonAction::NONE)return;
  if(action==BadgeButtonAction::SETTINGS) {
    stopPortal();screen=SETTINGS;showBadge=false;expanded=false;lastInteraction=millis();redraw=true;
  } else if(action==BadgeButtonAction::BLUE)pageAccounts(1,remember);
  else {
    // Yellow changes only this account's style. From Settings it returns
    // without cycling, just as the blue pusher returns without paging.
    if(screen==BADGE)selectBadgeStyle(0,1,remember);
    else pageAccounts(0,false);
  }
  Serial.printf("BADGE_ACTION action=%s screen=%s provider=%s design=%u style=%u active_style_count=%u design_total=%u page=%u available_count=%u fetch_count=%u avatar_fetch_count=%u cache_hits=%u profiles_pending=%u input_presses=%u\n",
    action==BadgeButtonAction::BLUE?"blue":action==BadgeButtonAction::YELLOW?"yellow":"both",
    currentScreenName(),profileProviderId(),unsigned(badgeDesign),unsigned(selectedBadgeStyle()),unsigned(BADGE_STYLE_COUNT),unsigned(BADGE_DESIGN_COUNT),
    unsigned(currentProfilePage()),unsigned(availableProfileCount()),unsigned(profileFetchCount),unsigned(avatarFetchCount),unsigned(profileCacheHits),unsigned(profilePending),unsigned(inputPresses));
}
void selectProfileSettingsProvider(ProfileProvider provider) {
  if(uint8_t(provider)>=ACCOUNT_PROVIDER_COUNT)return;
  settingsProvider=provider;screen=PROFILE_SETTINGS;showBadge=false;expanded=false;redraw=true;
}
// A diagnostic may select an already connected cache directly. Unconnected
// providers open their setup tab, without changing the visible/remembered page.
void selectProfileProvider(ProfileProvider provider,bool remember) {
  if(uint8_t(provider)>=ACCOUNT_PROVIDER_COUNT)return;
  if(cachedProfileIsReady(provider)) {
    activateCachedProfile(provider);rememberCurrentProvider(remember);
    screen=BADGE;showBadge=true;expanded=false;redraw=true;
  } else selectProfileSettingsProvider(provider);
  reportAccountPage();
}

void updateOrientation() {
  uint32_t now=millis();
  if(!M5.Imu.isEnabled() || uint32_t(now-lastOrientationPoll)<50)return;
  lastOrientationPoll=now;
  if(!(M5.Imu.update() & m5::IMU_Class::sensor_mask_accel)) {
    orientation.invalidate();return;
  }
  auto data=M5.Imu.getImuData();
  orientationAccelX=data.accel.x;orientationAccelY=data.accel.y;orientationAccelZ=data.accel.z;
  lastOrientationSample=now;orientationSampleReady=true;
  // StopWatch mounts BMI270 with native Y along display X, and native X
  // along display Y (the same swap used by its factory bubble-level app).
  if(orientation.update(data.accel.y,data.accel.x,data.accel.z,now,M5.Touch.getCount()!=0)) {
    // Called after touch dispatch and only with no contact/release detail left.
    // M5GFX transforms the next touch sample using this new display rotation.
    M5.Display.setRotation(orientation.rotation());redraw=true;orientationChanges++;
    Serial.printf("ORIENTATION_CHANGED rotation=%u degrees=%u\n",unsigned(orientation.rotation()),unsigned(orientation.rotation()*90));
  }
}

void reportOrientation() {
  Serial.printf("ORIENTATION_STATUS sensor=%s rotation=%u degrees=%u changes=%u ax=%.3f ay=%.3f az=%.3f sample_age_ms=%u\n",
    !M5.Imu.isEnabled()?"unavailable":orientationSampleReady?"ready":"waiting",
    unsigned(orientation.rotation()),unsigned(orientation.rotation()*90),unsigned(orientationChanges),
    orientationAccelX,orientationAccelY,orientationAccelZ,
    orientationSampleReady?unsigned(millis()-lastOrientationSample):0);
}

bool due(uint32_t deadline) { return int32_t(millis()-deadline)>=0; }
void state(const String &title,const String &detail="") {
  if(statusText!=title || detailText!=detail) {statusText=title;detailText=detail;redraw=true;}
}
void renderStatus() {
  auto &d=M5.Display; int cx=d.width()/2;
  if(screen==BADGE) {drawBadge();return;}
  if(screen==SETTINGS) {renderSettings();return;}
  if(screen==WIFI_SETTINGS) {renderWifi();return;}
  if(screen==PROFILE_SETTINGS) {renderProfileSettings();return;}
  d.startWrite(); d.fillScreen(TFT_BLACK); d.setTextDatum(middle_center); d.setTextColor(TFT_WHITE,TFT_BLACK);
  d.setFont(&fonts::FreeSansBold18pt7b); d.drawString("Devices",cx,51);
  d.setFont(&fonts::FreeSans9pt7b); d.setTextColor(0xAD75,TFT_BLACK);d.drawString("chan.dev / Production",cx,87);
  if(deviceCode.length() && pairUrl.length()) {
    d.qrcode(pairUrl.c_str(),cx-143,103,286,1,true);
    d.setFont(&fonts::FreeSansBold12pt7b);d.setTextColor(TFT_WHITE,TFT_BLACK);d.drawString(userCode,cx,411);
    d.setFont(&fonts::Font0);d.setTextSize(2);d.drawString("SCAN & CONFIRM CODE",cx,440);d.setTextSize(1);
  } else {
    bool active=authenticated && time(nullptr)<accessExpires && WiFi.status()==WL_CONNECTED;
    d.drawCircle(cx,155,18,active?0x5E69:0xFD20);
    if(active) {d.drawLine(cx-9,155,cx-2,162,0x5E69);d.drawLine(cx-2,162,cx+11,148,0x5E69);}
    d.setFont(&fonts::FreeSansBold12pt7b);d.setTextColor(TFT_WHITE,TFT_BLACK);d.drawString(statusText,cx,210);
    d.setFont(&fonts::FreeSans9pt7b);d.drawString(detailText,cx,252);
    if(authenticated) {
      d.setTextColor(0xAD75,TFT_BLACK);d.drawString("Application confirmed",cx,300);
      d.drawString(WiFi.status()==WL_CONNECTED?"Wi-Fi connected":"Wi-Fi disconnected",cx,329);
    }
    d.setFont(&fonts::Font0);d.setTextSize(2);d.setTextColor(0x9CF3,TFT_BLACK);
    d.drawString(authenticated?"SESSION ACTIVE":"TAP TO CONNECT",cx,350);d.setTextSize(1);
    drawBack();
  }
  d.endWrite();d.display();
}
String formEscape(const String &s) {
  static const char h[]="0123456789ABCDEF";String r;r.reserve(s.length()*3);
  for(size_t i=0;i<s.length();i++) {uint8_t c=s[i];if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~') r+=char(c);else {r+='%';r+=h[c>>4];r+=h[c&15];}}
  return r;
}
bool decodeClaims(const String &jwt,JsonDocument &claims) {
  int a=jwt.indexOf('.'),b=jwt.indexOf('.',a+1);if(a<1||b<a+2) return false;
  String segment=jwt.substring(a+1,b);segment.replace('-','+');segment.replace('_','/');while(segment.length()%4)segment+='=';
  if(segment.length()>8192)return false;
  uint8_t *out=(uint8_t*)ps_malloc(6145);size_t len=0;
  if(!out)return false;
  bool valid=!mbedtls_base64_decode(out,6144,&len,(const uint8_t*)segment.c_str(),segment.length());
  if(valid) {out[len]=0;valid=!deserializeJson(claims,out,len);}
  memset(out,0,6145);free(out);return valid;
}
// Tokens arrive only from WorkOS over CA-validated HTTPS. This checks application
// context on that trusted response; it is not a general JWT signature verifier.
bool acceptSession(JsonDocument &r,const String &requestedOrg="") {
  String access=r["access_token"]|"",refresh=r["refresh_token"]|"";
  JsonDocument claims;
  if(access.isEmpty()||refresh.isEmpty()||!decodeClaims(access,claims))return false;
  String client=claims["client_id"]|"",issuer=claims["iss"]|"",subject=claims["sub"]|"";
  String organization=claims["org_id"]|"";
  if(client!=CLIENT_ID || issuer!=EXPECTED_ISSUER || subject.isEmpty() || subject!=(r["user"]["id"]|String(""))) {
    Serial.printf("AUTH_REJECTED client_match=%d issuer_match=%d user_match=%d\n",client==CLIENT_ID,issuer==EXPECTED_ISSUER,subject==(r["user"]["id"]|String("")));return false;
  }
  int64_t expires=claims["exp"]|int64_t(0);
  if(expires<=time(nullptr)+30)return false;
  JsonDocument saved;saved["client_id"]=CLIENT_ID;saved["refresh_token"]=refresh;
  saved["organization_id"]=organization;
  saved["email"]=r["user"]["email"]|"";saved["user_id"]=subject;
  String record;serializeJson(saved,record);
  // One committed NVS blob avoids partially replacing a rotated refresh token.
  if(settings.putString("session",record)!=record.length()) {state("Storage error","Sign in again");return false;}
  // Preserve a rotated refresh token even if WorkOS declined the requested
  // workspace. The backend still requires that workspace in a signed token.
  if(currentUserId!=subject || currentOrgId!=organization)clearProfile("loading");
  refreshToken=refresh;accessToken=access;accessExpires=expires;
  currentUserId=subject;currentOrgId=organization;accountEmail=r["user"]["email"]|"";sessionId=claims["sid"]|"";
  authenticated=true;deviceCode="";pairUrl="";userCode="";needPair=false;
  if(!profilesWarmed)profilePending=true;
  state("Connected",accountEmail);redraw=true;
  Serial.printf("AUTH_CONNECTED application=Devices environment=Production client_id=%s user_id=%s session_id=%s\n",CLIENT_ID,currentUserId.c_str(),sessionId.c_str());
  return requestedOrg.isEmpty() || organization==requestedOrg;
}
void beginPairing() {
  JsonDocument r;state("Connecting","Requesting sign-in code");
  int code=badgeHttp.postJson(BadgeHttpKind::PAIR_BEGIN,"https://api.workos.com/user_management/authorize/device",String("client_id=")+CLIENT_ID,r);
  if(code==BADGE_HTTP_PENDING)return;
  if(code!=200) {state("Sign-in unavailable","Tap Connect to retry");needPair=false;Serial.printf("PAIR_ERROR http=%d\n",code);return;}
  String uri=r["verification_uri_complete"]|"",dc=r["device_code"]|"",uc=r["user_code"]|"";
  int expiry=r["expires_in"]|0,interval=r["interval"]|5;
  if(!uri.startsWith("https://")||dc.isEmpty()||uc.isEmpty()||expiry<1||expiry>1800||interval<1||interval>120) {
    state("Sign-in unavailable","Invalid authorization response");needPair=false;return;
  }
  deviceCode=dc;userCode=uc;pairUrl=uri;pairDeadline=millis()+expiry*1000;
  pollInterval=interval*1000;nextPoll=millis()+pollInterval;needPair=false;
  if(screen==AUTH_SETTINGS)showBadge=false;redraw=true;
  // Only the public user code/verification URL are reported. Device code stays on board.
  Serial.printf("PAIR_READY user_code=%s url=%s\n",userCode.c_str(),pairUrl.c_str());
}
void pollPairing() {
  if(due(pairDeadline)) {badgeHttp.cancel();deviceCode="";pairUrl="";state("Code expired","Tap Connect for a new code");redraw=true;return;}
  if(!due(nextPoll))return;
  JsonDocument r;int code=badgeHttp.postJson(BadgeHttpKind::PAIR_POLL,TOKEN_URL,String("grant_type=urn:ietf:params:oauth:grant-type:device_code&client_id=")+CLIENT_ID+"&device_code="+formEscape(deviceCode),r);
  if(code==BADGE_HTTP_PENDING)return;
  String err=r["error"]|"";
  if(code==200) {
    if(!acceptSession(r)) {deviceCode="";pairUrl="";state("Account not accepted","Please retry sign-in");redraw=true;}
  } else if(err=="slow_down"||code==429) {pollInterval+=5000;}
  else if(err=="authorization_pending") {}
  else if(code<0||code>=500) {pollInterval=min(pollInterval+5000,uint32_t(60000));}
  else {deviceCode="";pairUrl="";state("Sign-in stopped","Tap Connect to retry");redraw=true;Serial.printf("AUTH_STOPPED http=%d\n",code);}
  nextPoll=millis()+pollInterval;
}
void renewSession() {
  JsonDocument r;
  String body=String("grant_type=refresh_token&client_id=")+CLIENT_ID+"&refresh_token="+formEscape(refreshToken);
  if(currentOrgId.length())body+="&organization_id="+formEscape(currentOrgId);
  int code=badgeHttp.postJson(BadgeHttpKind::AUTH_REFRESH,TOKEN_URL,body,r);
  if(code==BADGE_HTTP_PENDING)return;
  if(code==200) {
    if(!acceptSession(r)) {authenticated=false;clearProfile("sign_in_required");state("Session not accepted","Tap Connect to reconnect");}
  } else if((r["error"]|String(""))=="invalid_grant") {
    Serial.println("AUTH_REFRESH state=expired");
    settings.remove("session");refreshToken="";accessToken="";authenticated=false;
    clearProfile("sign_in_required");
    needPair=false;state("Sign in again","Session expired");
  } else {state("Reconnecting","Retrying WorkOS shortly");Serial.printf("AUTH_REFRESH state=retry http=%d\n",code);}
  nextNetworkTry=millis()+30000;
}
// Continue an owned request before starting timer-based work. In particular,
// a pending sign-in cannot be starved by a refresh timer becoming due. Consume
// completed auth responses even after Wi-Fi drops, preserving rotated tokens.
void handleAuthentication() {
  BadgeHttpKind kind=badgeHttp.kind();
  if(kind==BadgeHttpKind::AUTH_REFRESH) {renewSession();return;}
  if(kind==BadgeHttpKind::PAIR_BEGIN) {beginPairing();return;}
  if(kind==BadgeHttpKind::PAIR_POLL) {pollPairing();return;}
  if(kind!=BadgeHttpKind::NONE || WiFi.status()!=WL_CONNECTED || portalActive || time(nullptr)<=1780000000)return;
  if(deviceCode.length())pollPairing();
  else if(refreshToken.length() && !profileRefreshActive && (!authenticated || accessExpires-time(nullptr)<120) && due(nextNetworkTry))renewSession();
  else if(!authenticated && needPair)beginPairing();
  else if(authenticated && time(nullptr)<accessExpires)state("Connected",accountEmail);
}
#include "profile.h"
void reportStatus() {
  const char *auth=authenticated&&time(nullptr)<accessExpires?"connected":"not_connected";
  Serial.printf("DEVICE_STATUS wifi=%s auth=%s application=Devices environment=Production client_id=%s user_id=%s\n",WiFi.status()==WL_CONNECTED?"connected":"disconnected",auth,CLIENT_ID,authenticated?currentUserId.c_str():"none");
  Serial.printf("BADGE_STATUS profile=%s avatar=%s workspace=%s stack_free=%u idle_max_ms=%u input_presses=%u design=%u style=%u provider=%s available_count=%u page=%u profiles_pending=%u fetch_count=%u avatar_fetch_count=%u cache_hits=%u psram_free=%u heap_free=%u screen=%s active_style_count=%u design_total=%u expanded=%u\n",
    profileReady?"ready":profileState.c_str(),profileAvatarReady?"ready":"not_loaded",currentOrgId.length()?"selected":"none",
    unsigned(uxTaskGetStackHighWaterMark(nullptr)),unsigned(idleMaxGap),unsigned(inputPresses),unsigned(badgeDesign),unsigned(selectedBadgeStyle()),profileProviderId(),
    unsigned(availableProfileCount()),unsigned(currentProfilePage()),unsigned(profilePending),unsigned(profileFetchCount),unsigned(avatarFetchCount),unsigned(profileCacheHits),unsigned(ESP.getFreePsram()),unsigned(ESP.getFreeHeap()),currentScreenName(),unsigned(BADGE_STYLE_COUNT),unsigned(BADGE_DESIGN_COUNT),unsigned(expanded));
  reportProfileStore();
  reportOrientation();
}
void serialCommands() {
  while(Serial.available()) {
    char c=Serial.read();
    if(c=='\r')continue;
    if(c!='\n') {if(serialInput.length()<2048&&!serialOverflow)serialInput+=c;else serialOverflow=true;continue;}
    if(serialOverflow) {serialInput="";serialOverflow=false;Serial.println("COMMAND_REJECTED");continue;}
    JsonDocument cmd;bool valid=!deserializeJson(cmd,serialInput);serialInput="";
    if(!valid) {Serial.println("COMMAND_REJECTED");continue;}
    String op=cmd["op"]|"";
    if(op=="status") {if(cmd["reset_metrics"]|false)idleMaxGap=0;monitorUntil=millis()+20000;reportStatus();}
    else if(op=="orientation")reportOrientation();
    else if(op=="reboot") {
      // One test boot with the radio off proves file restoration without
      // changing saved credentials. The flag is consumed at the next boot.
      if(cmd["offline_once"]|false) {
        if(settings.putBool("offline_once",true)!=1) {Serial.println("COMMAND_REJECTED");continue;}
      }
      ESP.restart();
    }
    else if(op=="settings")dispatchButtonAction(BadgeButtonAction::SETTINGS,false);
    else if(op=="button") {
      String value=cmd["value"]|"";
      if(value=="blue" || value=="yellow" || value=="both")dispatchButtonAction(value=="blue"?BadgeButtonAction::BLUE:value=="yellow"?BadgeButtonAction::YELLOW:BadgeButtonAction::SETTINGS,cmd["remember"]|false);
      else Serial.println("COMMAND_REJECTED");
    }
    else if(op=="refresh_profile")queueProfileRefresh();
    else if(op=="page") {
      if(cmd["step"].is<int>() && (cmd["step"].as<int>()==1 || cmd["step"].as<int>()==-1))pageAccounts(cmd["step"].as<int>(),cmd["remember"]|false);
      else Serial.println("COMMAND_REJECTED");
    }
    else if(op=="provider") {
      String provider=cmd["value"]|"";
      if(provider=="x" || provider=="linkedin" || provider=="github")selectProfileProvider(provider=="linkedin"?PROFILE_LINKEDIN:provider=="github"?PROFILE_GITHUB:PROFILE_X,cmd["remember"]|false);
      else Serial.println("COMMAND_REJECTED");
    }
    else if(op=="connect" && !authenticated && WiFi.status()==WL_CONNECTED) {needPair=true;nextNetworkTry=0;screen=AUTH_SETTINGS;redraw=true;}
    else if(op=="badge") {screen=BADGE;expanded=cmd["expanded"]|false;lastInteraction=millis();redraw=true;}
    else if(op=="badge_tap") {
      // Exercise public badge touch dispatch only; setup and sign-in screens
      // are outside the scope of this diagnostic.
      if(screen==BADGE && cmd["x"].is<int>() && cmd["y"].is<int>() &&
          cmd["x"].as<int>()>=0 && cmd["x"].as<int>()<M5.Display.width() &&
          cmd["y"].as<int>()>=0 && cmd["y"].as<int>()<M5.Display.height())handleTap(cmd["x"].as<int>(),cmd["y"].as<int>());
      else Serial.println("COMMAND_REJECTED");
    }
    else if(op=="design") {
      if(cmd["index"].is<int>() && cmd["step"].isNull())selectBadgeStyle(cmd["index"].as<int>(),0,cmd["remember"]|false);
      else if(cmd["index"].isNull() && cmd["step"].is<int>() && (cmd["step"].as<int>()==1 || cmd["step"].as<int>()==-1))selectBadgeStyle(0,cmd["step"].as<int>(),cmd["remember"]|false);
      else Serial.println("COMMAND_REJECTED");
    }
    else if(op=="capture_badge") {
      // Only the public badge can be captured; Wi-Fi and sign-in screens cannot.
      if(screen!=BADGE) {Serial.println("CAPTURE_REJECTED");continue;}
      renderStatus();redraw=false;
      int w=M5.Display.width(),h=M5.Display.height();uint8_t row[480*3]={};
      if(w>480 || h>480) {Serial.println("CAPTURE_REJECTED");continue;}
      foregroundOperation=true;
      // Only an explicitly requested frame may wait briefly for the USB host.
      // Abort if it stops reading; never leave normal input waiting on logs.
      Serial.setTxTimeoutMs(10);uint32_t captureDeadline=millis()+5000;bool complete=true;
      Serial.printf("BADGE_CAPTURE %d %d\n",w,h);
      for(int y=0;y<h;y++) {
        if(due(captureDeadline)) {complete=false;break;}
        M5.Display.readRectRGB(0,y,w,1,row);
        if(Serial.write(row,w*3)!=size_t(w*3)) {complete=false;break;}
      }
      Serial.println(complete?"\nBADGE_CAPTURE_END":"\nBADGE_CAPTURE_ABORTED");
      Serial.setTxTimeoutMs(0);
    }
    else Serial.println("COMMAND_REJECTED");
  }
}
void setup() {
  Serial.setTxBufferSize(4096);Serial.begin(115200);Serial.setTxTimeoutMs(0);serialInput.reserve(2048);
  auto cfg=M5.config();cfg.internal_imu=true;cfg.internal_rtc=false;cfg.internal_mic=false;cfg.internal_spk=false;cfg.fallback_board=m5::board_t::board_M5StopWatch;
  M5.begin(cfg);M5.Display.setRotation(0);M5.Display.setBrightness(150);
  // Each provider owns one independent RGB565 cache in PSRAM. Paging only
  // points the badge at an existing sprite; it never downloads or copies it.
  for(auto &cache:profileCache) {cache.avatar.setPsram(true);cache.avatar.setColorDepth(16);cache.avatar.createSprite(400,400);}
  settings.begin("devices",false);
  badgeStyles.restore(settings.getUInt("styles_v1",0));savedBadgeStyles=badgeStyles;
  uint8_t savedProvider=settings.getUChar("provider",uint8_t(PROFILE_LINKEDIN));
  selectedProvider=savedProvider<ACCOUNT_PROVIDER_COUNT?ProfileProvider(savedProvider):PROFILE_LINKEDIN;
  syncBadgeDesign();
  settingsProvider=selectedProvider;profileAvatar=&profileCache[selectedProvider].avatar;
  offlineTestBoot=settings.getBool("offline_once",false);
  if(offlineTestBoot)settings.remove("offline_once");
  WiFi.persistent(false);WiFi.mode(offlineTestBoot?WIFI_OFF:WIFI_STA);WiFi.setAutoReconnect(true);
  JsonDocument saved;
  if(!deserializeJson(saved,settings.getString("session","")) && (saved["client_id"]|String(""))==CLIENT_ID) {
    refreshToken=saved["refresh_token"]|"";
    String organization=saved["organization_id"]|"",user=saved["user_id"]|"";
    if(refreshToken.length() && validOrg(organization) && validSavedUser(user)) {
      currentOrgId=organization;currentUserId=user;accountEmail=saved["email"]|"";
    }
  }
  restoreProfileStore();
  renderStatus();redraw=false;
  if(profileReady)cacheBootReadyMs=millis();
  badgeHttp.begin();
  JsonDocument wifi;
  if(!offlineTestBoot && !deserializeJson(wifi,settings.getString("wifi",""))) {
    String ssid=wifi["ssid"]|"",password=wifi["password"]|"";
    if(ssid.length()) {WiFi.begin(ssid.c_str(),password.c_str());wifiStarted=true;state("Connecting Wi-Fi","Using saved device settings");nextNetworkTry=millis()+25000;}
  }
  renderStatus();redraw=false;
  Serial.printf("DEVICES_READY settings=both_buttons styles_per_account=%u\n",unsigned(BADGE_STYLE_COUNT));
}
void loop() {
  uint32_t now=millis();
  if(lastLoopStart && !foregroundOperation)idleMaxGap=max(idleMaxGap,now-lastLoopStart);
  lastLoopStart=now;foregroundOperation=false;
  M5.update();serialCommands();badgeHttp.busy();
  handlePortal();
  BadgeButtonAction buttonAction=buttonGesture.update(M5.BtnA.isPressed(),M5.BtnB.isPressed(),millis());
  if(buttonAction!=BadgeButtonAction::NONE) {inputPresses++;dispatchButtonAction(buttonAction,true);}
  if(M5.Touch.getCount()) {
    auto t=M5.Touch.getDetail();
    // Use the default completed-tap gesture so drags and holds do not select.
    if(t.wasClicked()) {inputPresses++;handleTap(t.x,t.y);}
  }
  updateOrientation();
  if(screen==BADGE&&expanded&&millis()-lastInteraction>45000) {expanded=false;redraw=true;}
  if(WiFi.status()!=WL_CONNECTED) {
    if(wifiStarted && !portalActive && due(nextNetworkTry)) {WiFi.reconnect();nextNetworkTry=millis()+25000;state("Wi-Fi unavailable","Retrying saved network");}
  } else {
    if(!timeStarted) {configTime(0,0,"time.google.com","pool.ntp.org");timeStarted=true;state("Setting clock","Preparing secure sign-in");}
  }
  handleAuthentication();
  if(deviceCode.isEmpty())handleProfile();
  if(!profileReady && !authenticated && deviceCode.isEmpty() && !refreshToken.length()) {
    String visibleState=WiFi.status()==WL_CONNECTED?"sign_in_required":"wifi_required";
    if(profileState!=visibleState) {profileState=visibleState;redraw=true;}
  }
  updateCacheConnectionState();
  if(redraw) {renderStatus();redraw=false;}
  if(providerPreferenceDirty && due(providerPreferenceSaveAfter)) {
    providerPreferenceDirty=false;
    if(cachedProfileIsReady(pendingProviderPreference) && settings.getUChar("provider",255)!=uint8_t(pendingProviderPreference) &&
        settings.putUChar("provider",uint8_t(pendingProviderPreference))!=1)Serial.println("BADGE_PROVIDER save=failed");
  }
  if(stylePreferenceDirty && due(stylePreferenceSaveAfter)) {
    stylePreferenceDirty=false;
    uint32_t packed=savedBadgeStyles.packed();
    if(settings.getUInt("styles_v1",0)!=packed && settings.putUInt("styles_v1",packed)!=sizeof(packed))Serial.println("BADGE_STYLE save=failed");
  }
  if(monitorUntil && !due(monitorUntil) && millis()-lastStatus>5000) {lastStatus=millis();reportStatus();}
  delay(20);
}
