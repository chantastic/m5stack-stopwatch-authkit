#pragma once
#include "avatar_decode.h"
static constexpr char X_PROFILE_URL[]="https://auth.chan.dev/devices/x";
static constexpr char LINKEDIN_PROFILE_URL[]="https://auth.chan.dev/devices/linkedin";
static constexpr char GITHUB_PROFILE_URL[]="https://auth.chan.dev/devices/github";
#include "profile_urls.h"
const char *providerProfileUrl(ProfileProvider provider) {return provider==PROFILE_LINKEDIN?LINKEDIN_PROFILE_URL:provider==PROFILE_GITHUB?GITHUB_PROFILE_URL:X_PROFILE_URL;}
static constexpr char WORKSPACE_URL[]="https://auth.chan.dev/devices/workspace";

#include "profile_store.h"

uint8_t profileRefreshCursor=0;
bool profileRefreshRetry=false,profileMetadataReady=false;
String refreshWorkspace,pendingName,pendingHandle,pendingUrl,pendingRemoteId,pendingAvatarUrl;
String pendingProfileOwner,pendingProfileWorkspace;
M5Canvas pendingAvatarCanvas(&M5.Display);

void resetProfileRefresh() {
  pendingAvatarCanvas.deleteSprite();
  profileRefreshActive=false;profileRefreshCursor=0;profileMetadataReady=false;
  refreshWorkspace="";pendingName="";pendingHandle="";pendingUrl="";pendingRemoteId="";pendingAvatarUrl="";
  pendingProfileOwner="";pendingProfileWorkspace="";
}
bool validSavedId(const String &id,const char *prefix) {
  size_t start=strlen(prefix);
  if(!id.startsWith(prefix) || id.length()<=start || id.length()>80)return false;
  for(size_t i=start;i<id.length();i++)if(!profileAsciiAlnum(id[i]))return false;
  return true;
}
bool validSavedUser(const String &id) {return validSavedId(id,"user_");}
bool validOrg(const String &id) {return validSavedId(id,"org_");}

bool cachedProfileIsReady(ProfileProvider provider) {
  if(uint8_t(provider)>=ACCOUNT_PROVIDER_COUNT)return false;
  const auto &cache=profileCache[provider];
  return cache.connected && sameAccountContext(cache.owner.c_str(),cache.workspace.c_str(),currentUserId.c_str(),currentOrgId.c_str());
}
uint8_t availableProfileMask() {
  uint8_t mask=0;
  for(uint8_t provider:ACCOUNT_PAGE_ORDER)if(cachedProfileIsReady(ProfileProvider(provider)))mask|=1U<<provider;
  return mask;
}
uint8_t availableProfileCount() {return accountPageCount(availableProfileMask());}
uint8_t currentProfilePage() {return accountPagePosition(uint8_t(selectedProvider),availableProfileMask());}
void clearActiveProfile(const String &reason) {
  profileReady=false;profileAvatarReady=false;profileProvider=255;expanded=false;
  profileName="";profileHandle="";profileUrl="";profileOwner="";profileWorkspace="";
  profileState=reason;redraw=true;
}
void clearCachedProfile(ProfileProvider provider,const String &reason,bool forgetDisk=true) {
  if(forgetDisk && !removeStoredProfile(provider))settings.remove("offline_ctx");
  auto &cache=profileCache[provider];
  cache.connected=false;cache.avatarReady=false;cache.saved=false;cache.refreshed=false;
  cache.name="";cache.handle="";cache.url="";cache.avatarUrl="";cache.remoteId="";cache.owner="";cache.workspace="";cache.state=reason;
  if(cache.avatar.getBuffer())cache.avatar.fillScreen(TFT_BLACK);
  if(provider==selectedProvider)clearActiveProfile(reason);
}
// Only auth rejection or a verified user/workspace change invalidates every
// cache. A normal page/setup-tab change must never call this function.
void clearProfile(const String &reason) {
  // Disable offline restoration before removing files; interrupted deletion
  // must not revive a profile after an explicit authentication rejection.
  settings.remove("offline_ctx");
  clearStoredProfiles();badgeHttp.cancel();resetProfileRefresh();
  for(uint8_t provider:ACCOUNT_PAGE_ORDER)clearCachedProfile(ProfileProvider(provider),reason,false);
  clearActiveProfile(reason);profilesWarmed=false;profilePending=true;nextProfileTry=0;
  providerPreferenceDirty=false;
}
void activateCachedProfile(ProfileProvider provider) {
  if(!cachedProfileIsReady(provider))return;
  const auto &cache=profileCache[provider];selectedProvider=provider;syncBadgeDesign();
  profileName=cache.name;profileHandle=cache.handle;profileUrl=cache.url;
  profileOwner=cache.owner;profileWorkspace=cache.workspace;profileProvider=uint8_t(provider);
  profileAvatar=&profileCache[provider].avatar;profileAvatarReady=cache.avatarReady;
  profileReady=true;profileState="connected";profileCacheHits++;expanded=false;redraw=true;
}
void showBestCachedProfile() {
  if(cachedProfileIsReady(selectedProvider))activateCachedProfile(selectedProvider);
  else {
    uint8_t first=nextAccountPage(NO_ACCOUNT_PAGE,1,availableProfileMask());
    if(first!=NO_ACCOUNT_PAGE)activateCachedProfile(ProfileProvider(first));
    else clearActiveProfile(profileCache[selectedProvider].state=="unknown"?"connect_required":profileCache[selectedProvider].state);
  }
}
void queueProfileRefresh() {
  if(profileRefreshActive)return;
  profilePending=true;nextProfileTry=0;nextNetworkTry=0;profileState="refreshing";redraw=true;
}

void restoreProfileStore() {
  if(!mountProfileStore())return;
  JsonDocument context;
  if(!refreshToken.length() || !validSavedUser(currentUserId) || !validOrg(currentOrgId) ||
      deserializeJson(context,settings.getString("offline_ctx","")) ||
      (context["client_id"]|String(""))!=CLIENT_ID ||
      (context["user_id"]|String(""))!=currentUserId ||
      (context["organization_id"]|String(""))!=currentOrgId)return;
  // These IDs came from a previous verified session, independently of files.
  // Restoring a public badge never establishes a current authenticated session.
  for(uint8_t provider:ACCOUNT_PAGE_ORDER)
    loadStoredProfile(ProfileProvider(provider),currentUserId,currentOrgId,profileCache[provider]);
  if(availableProfileCount())showBestCachedProfile();
}

void persistCachedProfile(ProfileProvider provider) {
  auto &cache=profileCache[provider];
  if(!authenticated || !cachedProfileIsReady(provider) || !cache.refreshed)return;
  if(!saveStoredProfile(provider,cache)) {cache.saved=false;return;}
  JsonDocument context;context["client_id"]=CLIENT_ID;context["user_id"]=currentUserId;context["organization_id"]=currentOrgId;
  String record;serializeJson(context,record);
  cache.saved=settings.getString("offline_ctx","")==record || settings.putString("offline_ctx",record)==record.length();
  if(!cache.saved)Serial.println("BADGE_STORE state=context_write_failed");
}

String cachedProfileStatus(ProfileProvider provider) {
  const auto &cache=profileCache[provider];
  if(!cachedProfileIsReady(provider))return "Connect or refresh";
  if(WiFi.status()!=WL_CONNECTED)return cache.saved?"Saved profile / offline":"Profile / offline";
  if(profileRefreshActive)return "Updating profile...";
  if(cache.state!="connected")return cache.saved?"Update unavailable / saved":"Update unavailable";
  if(!cache.refreshed)return "Saved profile / not updated";
  return cache.saved?"Updated and saved":"Updated / save unavailable";
}
void updateCacheConnectionState() {
  static bool lastWifi=false,lastAuth=false;
  bool online=WiFi.status()==WL_CONNECTED,auth=authenticated && time(nullptr)<accessExpires;
  if(lastWifi!=online || lastAuth!=auth)redraw=true;
  if(online && !lastWifi) {
    nextNetworkTry=0;
    if(!profilesWarmed || profileRefreshRetry) {profilePending=true;nextProfileTry=0;}
  }
  lastWifi=online;lastAuth=auth;
}
void reportProfileStore() {
  Serial.printf("BADGE_STORE ready=%u saved_mask=%u loads=%u writes=%u skipped=%u errors=%u boot_ready_ms=%u refreshing=%u network_busy=%u network_stack_free=%u\n",
    unsigned(profileStoreReady),unsigned(profileStoreSavedMask),unsigned(profileStoreLoads),unsigned(profileStoreWrites),
    unsigned(profileStoreSkipped),unsigned(profileStoreErrors),unsigned(cacheBootReadyMs),unsigned(profileRefreshActive),unsigned(badgeHttp.busy()),unsigned(badgeHttp.workerStackFree()));
}

BadgeHttpKind profileRequestKind(ProfileProvider provider) {
  return provider==PROFILE_X?BadgeHttpKind::PROFILE_X:provider==PROFILE_LINKEDIN?BadgeHttpKind::PROFILE_LINKEDIN:BadgeHttpKind::PROFILE_GITHUB;
}
BadgeHttpKind avatarRequestKind(ProfileProvider provider) {
  return provider==PROFILE_X?BadgeHttpKind::AVATAR_X:provider==PROFILE_LINKEDIN?BadgeHttpKind::AVATAR_LINKEDIN:BadgeHttpKind::AVATAR_GITHUB;
}

// Return 0 while a background request is pending, 1 when ready, -1 on failure.
int selectProfileWorkspace() {
  if(currentOrgId.length())return 1;
  if(refreshWorkspace.isEmpty()) {
    JsonDocument workspace;
    int code=badgeHttp.getJson(BadgeHttpKind::WORKSPACE,WORKSPACE_URL,accessToken,workspace);
    if(code==BADGE_HTTP_PENDING)return 0;
    if(code==401) {authenticated=false;clearProfile("sign_in_required");nextNetworkTry=millis()+30000;return -1;}
    String organization=workspace["organizationId"]|"";
    if(code!=200 || (workspace["state"]|String(""))!="ready" || !validOrg(organization)) {
      profileState=code==200?"workspace_required":"unavailable";profileRefreshRetry=code<0 || code==429 || code>=500;
      Serial.printf("BADGE_WORKSPACE state=%s http=%d\n",profileState.c_str(),code);return -1;
    }
    refreshWorkspace=organization;
  }
  JsonDocument session;const String organization=refreshWorkspace;
  int result=badgeHttp.postJson(BadgeHttpKind::WORKSPACE_SESSION,TOKEN_URL,String("grant_type=refresh_token&client_id=")+CLIENT_ID+
    "&refresh_token="+formEscape(refreshToken)+"&organization_id="+formEscape(organization),session);
  if(result==BADGE_HTTP_PENDING)return 0;
  if((session["error"]|String(""))=="invalid_grant") {
    settings.remove("session");refreshToken="";accessToken="";authenticated=false;
    clearProfile("sign_in_required");needPair=false;state("Sign in again","Session expired");return -1;
  }
  if(result!=200 || !acceptSession(session,organization)) {
    profileState="workspace_required";profileRefreshRetry=result<0 || result==429 || result>=500;
    Serial.printf("BADGE_WORKSPACE state=not_selected http=%d\n",result);refreshWorkspace="";return -1;
  }
  refreshWorkspace="";Serial.println("BADGE_WORKSPACE state=selected");return 1;
}

uint16_t imageU16(const uint8_t *data) {return (uint16_t(data[0])<<8)|data[1];}
uint32_t imageU32(const uint8_t *data) {return (uint32_t(data[0])<<24)|(uint32_t(data[1])<<16)|(uint32_t(data[2])<<8)|data[3];}
bool imageDimensions(const uint8_t *data,size_t length,bool &png,int &width,int &height) {
  static const uint8_t pngMagic[]={137,80,78,71,13,10,26,10};
  png=length>=24 && !memcmp(data,pngMagic,8);
  if(png) {
    if(memcmp(data+12,"IHDR",4))return false;
    width=imageU32(data+16);height=imageU32(data+20);
  } else {
    if(length<4 || data[0]!=0xff || data[1]!=0xd8)return false;
    size_t offset=2;width=height=0;
    while(offset+4<=length) {
      if(data[offset++]!=0xff)return false;
      while(offset<length && data[offset]==0xff)offset++;
      if(offset>=length)return false;
      uint8_t marker=data[offset++];
      if(marker==0xd9 || marker==0xda)return false;
      if(marker==0x01 || (marker>=0xd0 && marker<=0xd7))continue;
      if(offset+2>length)return false;
      size_t size=imageU16(data+offset);
      if(size<2 || offset+size>length)return false;
      if((marker>=0xc0 && marker<=0xc3) || (marker>=0xc5 && marker<=0xc7) || (marker>=0xc9 && marker<=0xcb) || (marker>=0xcd && marker<=0xcf)) {
        if(size<8)return false;
        height=imageU16(data+offset+3);width=imageU16(data+offset+5);break;
      }
      offset+=size;
    }
  }
  return width>0 && height>0 && width<=1024 && height<=1024;
}

int loadProfileAvatar(ProfileProvider provider,const String &url,M5Canvas &target) {
  if(!target.getBuffer())return false;
  if(!allowedAvatarUrl(provider,url))return false;
  // avatars.githubusercontent.com was verified with this public ISRG bundle
  // on 2026-09-10 (YR1 / Root YR / ISRG Root X1, hostname verification on).
  const char *avatarCa=AVATAR_ROOT_CA;
  if(provider==PROFILE_LINKEDIN) {
#ifdef HAS_LINKEDIN_AVATAR_TRUST
    avatarCa=LINKEDIN_AVATAR_ROOT_CA;
#else
    Serial.println("BADGE_AVATAR state=trust_not_configured");return false;
#endif
  }
  uint8_t *bytes=nullptr;size_t length=0;String type;
  bool wasBusy=badgeHttp.busy();
  int code=badgeHttp.getBytes(avatarRequestKind(provider),url,avatarCa,bytes,length,type);
  if(!wasBusy && badgeHttp.kind()==avatarRequestKind(provider))avatarFetchCount++;
  if(code==BADGE_HTTP_PENDING)return BADGE_HTTP_PENDING;
  bool png=false;int width=0,height=0;bool drawn=false;
  if(code==200 && bytes && imageDimensions(bytes,length,png,width,height)) {
    target.fillScreen(TFT_BLACK);
    const int canvasSize=target.width();
    float scale=float(canvasSize)/max(width,height);
    int left=(canvasSize-int(width*scale))/2,top=(canvasSize-int(height*scale))/2;
    if(png)drawn=target.drawPng(bytes,length,left,top,canvasSize,canvasSize,0,0,scale,scale);
    else {
      // X serves progressive JPEG avatars, which the display's built-in JPEG
      // decoder cannot read. Decode in PSRAM, then reduce into the badge sprite.
      int decodedWidth=0,decodedHeight=0;
      uint8_t *pixels=badgeDecodeAvatarJpeg(bytes,length,decodedWidth,decodedHeight);
      if(pixels && decodedWidth==width && decodedHeight==height) {
        int targetWidth=max(1,int(width*scale)),targetHeight=max(1,int(height*scale));
        for(int y=0;y<targetHeight;y++) {
          int y0=y*height/targetHeight,y1=min(height,max(y0+1,(y+1)*height/targetHeight));
          for(int x=0;x<targetWidth;x++) {
            int x0=x*width/targetWidth,x1=min(width,max(x0+1,(x+1)*width/targetWidth));
            uint32_t red=0,green=0,blue=0,count=0;
            for(int sy=y0;sy<y1;sy++)for(int sx=x0;sx<x1;sx++) {
              const uint8_t *pixel=pixels+(sy*width+sx)*3;
              red+=pixel[0];green+=pixel[1];blue+=pixel[2];count++;
            }
            uint16_t color=uint16_t(((red/count)>>3)<<11)|uint16_t(((green/count)>>2)<<5)|uint16_t((blue/count)>>3);
            target.drawPixel(left+x,top+y,color);
          }
        }
        drawn=true;
      }
      badgeFreeAvatarPixels(pixels);
    }
  }
  if(bytes)free(bytes);
  Serial.printf("BADGE_AVATAR state=%s provider=%s http=%d bytes=%u\n",drawn?"ready":"unavailable",providerId(provider),code,unsigned(length));
  return drawn;
}

// An update advances one bounded step per loop. The old owned profile remains
// displayable while HTTPS runs on the worker and until replacement is complete.
int refreshCachedProfile(ProfileProvider provider) {
  if(!profileMetadataReady) {
    JsonDocument response;
    bool wasBusy=badgeHttp.busy();
    int code=badgeHttp.getJson(profileRequestKind(provider),providerProfileUrl(provider),accessToken,response);
    if(!wasBusy && badgeHttp.kind()==profileRequestKind(provider))profileFetchCount++;
    if(code==BADGE_HTTP_PENDING)return 0;
    String result=response["state"]|"unavailable";
    if(code==401) {
      authenticated=false;clearProfile("sign_in_required");nextNetworkTry=millis()+30000;
      Serial.println("BADGE_PROFILE state=sign_in_required http=401");return -1;
    }
    if(code!=200 || result!="connected") {
      bool retryable=code<0 || code==408 || code==429 || code>=500 || (code==200 && (result=="unavailable" || result=="rate_limited"));
      bool transient=retryable || (code==200 && result=="credits_required");
      String reason=transient?"unavailable":result=="connect_required"?"connect_required":"access_denied";
      if(!transient || !cachedProfileIsReady(provider))clearCachedProfile(provider,reason,!transient);
      else profileCache[provider].state=reason;
      profileRefreshRetry|=retryable;
      Serial.printf("BADGE_PROFILE state=%s provider=%s http=%d\n",reason.c_str(),providerId(provider),code);return 1;
    }
    String handle=response["profile"]["handle"]|"",name=response["profile"]["name"]|"",url=response["profile"]["url"]|"";
    String remoteId=response["profile"]["id"]|"",avatarUrl=response["profile"]["avatarUrl"]|"";
    bool valid=name.length()>0 && name.length()<=400 && remoteId.length()>0 && remoteId.length()<=256;
    for(size_t i=0;i<name.length();i++)if(uint8_t(name[i])<32 || uint8_t(name[i])==127)valid=false;
    for(size_t i=0;i<remoteId.length();i++)if(uint8_t(remoteId[i])<32 || uint8_t(remoteId[i])==127)valid=false;
    if(provider==PROFILE_X) {
      valid=valid && handle.startsWith("@") && handle.length()>=2 && handle.length()<=16;
      for(size_t i=1;i<handle.length();i++)if(!profileAsciiAlnum(handle[i]) && handle[i]!='_')valid=false;
      valid=valid && url=="https://x.com/"+handle.substring(1);
    } else if(provider==PROFILE_LINKEDIN) {
      valid=valid && (response["provider"]|String(""))=="linkedin";
      if(response["profile"]["url"].isNull())valid=valid && response["profile"]["handle"].isNull();
      else {String slug;valid=valid && response["profile"]["url"].is<String>() && validLinkedInProfileUrl(url,slug) && handle==slug;}
    } else if(provider==PROFILE_GITHUB) {
      valid=valid && (response["provider"]|String(""))=="github" && validGitHubId(remoteId) &&
        handle.startsWith("@") && validGitHubProfileUrl(url,handle.substring(1));
      if(!response["profile"]["avatarUrl"].isNull())valid=valid && response["profile"]["avatarUrl"].is<String>() && validGitHubAvatarUrl(avatarUrl,remoteId);
    } else valid=false;
    valid=valid && (avatarUrl.isEmpty() || allowedAvatarUrl(provider,avatarUrl));
    if(!valid) {
      if(cachedProfileIsReady(provider))profileCache[provider].state="unavailable";
      else clearCachedProfile(provider,"unavailable",false);
      profileRefreshRetry=true;Serial.printf("BADGE_PROFILE state=invalid_profile provider=%s\n",providerId(provider));return 1;
    }
    // A verified change of remote account invalidates the old badge before
    // waiting for its new avatar, including across power loss during the wait.
    if(profileCache[provider].remoteId.length() && profileCache[provider].remoteId!=remoteId)clearCachedProfile(provider,"loading");
    pendingName=name;pendingHandle=handle;pendingUrl=url;pendingRemoteId=remoteId;pendingAvatarUrl=avatarUrl;
    pendingProfileOwner=currentUserId;pendingProfileWorkspace=currentOrgId;profileMetadataReady=true;
  }
  if(!authenticated || !sameAccountContext(pendingProfileOwner.c_str(),pendingProfileWorkspace.c_str(),currentUserId.c_str(),currentOrgId.c_str()))return -1;
  auto &cache=profileCache[provider];
  bool keepAvatar=cachedProfileIsReady(provider) && cache.avatarReady && cache.remoteId==pendingRemoteId;
  bool sameAvatar=keepAvatar && cache.avatarUrl==pendingAvatarUrl;
  bool avatarReady=sameAvatar;
  if(!sameAvatar && !pendingAvatarUrl.isEmpty()) {
    if(!pendingAvatarCanvas.getBuffer()) {
      pendingAvatarCanvas.setPsram(true);pendingAvatarCanvas.setColorDepth(16);pendingAvatarCanvas.createSprite(400,400);
    }
    if(cache.avatar.getBuffer() && pendingAvatarCanvas.getBuffer()) {
      int loaded=loadProfileAvatar(provider,pendingAvatarUrl,pendingAvatarCanvas);
      if(loaded==BADGE_HTTP_PENDING)return 0;
      avatarReady=loaded==1;
      if(avatarReady)pendingAvatarCanvas.pushSprite(&cache.avatar,0,0);
      else profileRefreshRetry=true;
    } else profileRefreshRetry=true;
  }
  pendingAvatarCanvas.deleteSprite();
  if(pendingAvatarUrl.isEmpty())keepAvatar=false;
  if(!avatarReady && !keepAvatar && cache.avatar.getBuffer())cache.avatar.fillScreen(TFT_BLACK);
  String actualAvatarUrl=(avatarReady || !keepAvatar)?pendingAvatarUrl:cache.avatarUrl;
  cache.name=pendingName;cache.handle=pendingHandle;cache.url=pendingUrl;cache.remoteId=pendingRemoteId;cache.avatarUrl=actualAvatarUrl;
  cache.owner=pendingProfileOwner;cache.workspace=pendingProfileWorkspace;cache.avatarReady=avatarReady||keepAvatar;
  cache.connected=true;cache.state="connected";cache.refreshed=true;
  persistCachedProfile(provider);profileMetadataReady=false;
  // Refresh the visible account without moving a user who paged during HTTPS.
  if(provider==selectedProvider)showBestCachedProfile();
  Serial.printf("BADGE_PROFILE state=ready provider=%s avatar=%s link=%s\n",providerId(provider),cache.avatarReady?"ready":"unavailable",cache.url.length()?"ready":"setup_required");
  return 1;
}

void handleProfile() {
  // Workspace selection rotates a refresh token. Always consume that owned
  // result even if the radio drops or the old access token expires meanwhile.
  if(profileRefreshActive && refreshWorkspace.length() && badgeHttp.kind()==BadgeHttpKind::WORKSPACE_SESSION) {
    int workspace=selectProfileWorkspace();
    if(workspace==0)return;
    if(workspace<0) {
      bool retry=profileRefreshRetry;resetProfileRefresh();
      profilesWarmed=authenticated;profilePending=retry || !authenticated;
      nextProfileTry=millis()+60000;redraw=true;return;
    }
  }
  if(!authenticated || time(nullptr)>=accessExpires || portalActive || WiFi.status()!=WL_CONNECTED) {
    if(profileRefreshActive) {badgeHttp.cancel();resetProfileRefresh();profileRefreshRetry=true;profilePending=true;}
    return;
  }
  if(!profilePending || !due(nextProfileTry))return;
  if(!profileRefreshActive) {
    if(badgeHttp.busy())return;
    profileRefreshActive=true;profileRefreshCursor=0;profileRefreshRetry=false;profileMetadataReady=false;
    profileState=profileReady?"refreshing":"loading";redraw=true;
  }
  int workspace=selectProfileWorkspace();
  if(workspace==0)return;
  if(workspace<0) {
    bool retry=profileRefreshRetry;resetProfileRefresh();
    profilesWarmed=authenticated;profilePending=retry || !authenticated;
    nextProfileTry=millis()+60000;redraw=true;return;
  }
  // A workspace-bound session may have reset the context and refresh cursor.
  profileRefreshActive=true;
  ProfileProvider provider=ProfileProvider(ACCOUNT_PAGE_ORDER[profileRefreshCursor]);
  int result=refreshCachedProfile(provider);
  if(result==0)return;
  if(result<0) {resetProfileRefresh();profilePending=false;redraw=true;return;}
  profileMetadataReady=false;
  if(++profileRefreshCursor<ACCOUNT_PROVIDER_COUNT)return;
  bool retry=profileRefreshRetry;resetProfileRefresh();profilesWarmed=true;profilePending=retry;
  nextProfileTry=millis()+60000;showBestCachedProfile();redraw=true;
  Serial.printf("BADGE_CACHE ready=1 available_count=%u fetch_count=%u avatar_fetch_count=%u\n",unsigned(availableProfileCount()),unsigned(profileFetchCount),unsigned(avatarFetchCount));
}

void renderProfileSettings() {
  auto &d=M5.Display;d.startWrite();d.fillScreen(TFT_BLACK);d.setTextDatum(middle_center);
  d.setFont(&fonts::FreeSansBold18pt7b);d.setTextColor(TFT_WHITE);d.drawString("Connections",234,54);
  for(uint8_t i=0;i<ACCOUNT_PROVIDER_COUNT;i++) {
    ProfileProvider provider=ProfileProvider(ACCOUNT_PAGE_ORDER[i]);
    int x=58+i*120;bool selected=settingsProvider==provider;
    d.fillRoundRect(x,91,112,44,13,selected?0xDEFB:0x2104);
    d.setFont(&fonts::FreeSansBold9pt7b);d.setTextColor(selected?TFT_BLACK:TFT_WHITE);
    d.drawString(providerName(provider),x+56,113);
  }
  bool ready=cachedProfileIsReady(settingsProvider);const auto &cache=profileCache[settingsProvider];
  if(ready && !cache.url.isEmpty()) {
    d.setFont(&fonts::FreeSansBold12pt7b);d.setTextColor(TFT_WHITE);
    d.drawString(badgeFitText(cache.name,340),234,186);
    d.setFont(&fonts::FreeSans9pt7b);d.setTextColor(0xAD75);
    d.drawString(cachedProfileStatus(settingsProvider),234,230);
    d.drawString("Pushers return to your badge",234,271);
  } else {
    d.setFont(&fonts::FreeSans9pt7b);d.setTextColor(TFT_WHITE);
    d.drawString(ready?"Add your public profile link":String("Connect your ")+providerName(settingsProvider)+" account",234,161);
    d.qrcode(providerConnectionsUrl(settingsProvider),157,179,154,1,true);
    d.setFont(&fonts::Font0);d.setTextColor(0xAD75);
    d.drawString("Use the same chan.dev account",234,341);
  }
  d.fillRoundRect(120,350,228,34,12,0x2104);
  d.setTextColor(TFT_WHITE);d.setFont(&fonts::FreeSans9pt7b);
  d.drawString(profileRefreshActive?"Updating...":ready?"Refresh accounts":"Check connections",234,367);
  drawBack();d.endWrite();d.display();
}
