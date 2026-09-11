void reset(){
 fakeMillis=1000;fakeNow=1800000000;WiFi.value=WL_CONNECTED;settings.values.clear();badgeHttp=MockHttp{};
 authenticated=false;profileRefreshActive=false;profileRefreshRetry=false;profilePending=true;profilesWarmed=false;profileMetadataReady=false;profileReady=false;portalActive=false;needPair=false;redraw=false;showBadge=true;providerPreferenceDirty=false;
 profileRefreshCursor=0;nextNetworkTry=0;nextProfileTry=0;pairDeadline=100000;nextPoll=0;pollInterval=5000;profileFetchCount=0;avatarFetchCount=0;accessExpires=0;screen=BADGE;
 deviceCode="";pairUrl="";userCode="";refreshToken="old-refresh";accessToken="old-access";accountEmail="";sessionId="";currentUserId="user_test";currentOrgId="org_test";profileState="";refreshWorkspace="";
 resets=loads=storageClears=profileCalls=0;stateTitle="";for(auto &cache:profileCache)cache=CachedProfile{};
 selectedProvider=PROFILE_LINKEDIN;profileProvider=255;profileAvatarReady=false;expanded=false;profileCacheHits=0;resetProfileRefresh();
}
static constexpr char newSession[]=R"({"access_token":"verified-new-access","refresh_token":"rotated-refresh","user":{"id":"user_test","email":"public@example.test"}})";
void expect(bool ok,const char *name){if(!ok){std::cerr<<"FAIL: "<<name<<"\n";abort();}std::cout<<"Passed: "<<name<<"\n";}
int main(){
 reset();needPair=true;nextNetworkTry=0;
 badgeHttp.own(BadgeHttpKind::PAIR_BEGIN,200,R"({"verification_uri_complete":"https://auth.example.test/pair","device_code":"device-test","user_code":"PUBLIC","expires_in":600,"interval":5})");
 handleAuthentication();
 expect(badgeHttp.calls.size()==1&&badgeHttp.calls[0]==BadgeHttpKind::PAIR_BEGIN&&!deviceCode.isEmpty(),"active pairing wins over due refresh");
 reset();WiFi.value=0;badgeHttp.own(BadgeHttpKind::AUTH_REFRESH,200,newSession);
 handleAuthentication();
 expect(authenticated&&refreshToken=="rotated-refresh"&&!badgeHttp.busy()&&settings.values.count("session"),"auth refresh consumed and rotated token saved offline");
 for(int condition=0;condition<3;condition++){
  reset();authenticated=true;accessExpires=fakeNow+100;currentOrgId="";profileRefreshActive=true;refreshWorkspace="org_test";
  if(condition==0)WiFi.value=0;
  if(condition==1)portalActive=true;
  if(condition==2)accessExpires=fakeNow-1;
  badgeHttp.own(BadgeHttpKind::WORKSPACE_SESSION);
  handleProfile();
  expect(badgeHttp.kind()==BadgeHttpKind::WORKSPACE_SESSION&&badgeHttp.canceledOwned==0,"pending workspace token is not canceled by connectivity guard");
  badgeHttp.own(BadgeHttpKind::WORKSPACE_SESSION,200,newSession);
  handleProfile();
  expect(refreshToken=="rotated-refresh"&&currentOrgId=="org_test"&&authenticated&&badgeHttp.canceledOwned==0,"workspace response consumed before Wi-Fi/portal/expiry guard");
  if(condition!=2)expect(profileCalls==0,"offline/portal workspace completion starts no profile fetch");
 }
 reset();authenticated=true;accessExpires=fakeNow+3600;profileRefreshActive=true;profileReady=true;WiFi.value=0;
 badgeHttp.own(BadgeHttpKind::PROFILE_X);
 handleAuthentication();handleProfile();
 expect(badgeHttp.canceledOwned==1&&!profileRefreshActive&&profilePending&&profileRefreshRetry&&profileReady,"ordinary profile GET cancels offline, retains display, and schedules retry");
 reset();authenticated=true;accessExpires=fakeNow+3600;currentOrgId="";profileRefreshActive=true;
 badgeHttp.own(BadgeHttpKind::WORKSPACE,401);
 handleProfile();
 expect(!authenticated&&profilePending&&!profilesWarmed&&!profileRefreshActive,"workspace 401 keeps discovery pending after invalidation");
 fakeMillis=nextNetworkTry;
 badgeHttp.own(BadgeHttpKind::AUTH_REFRESH,200,newSession);handleAuthentication();
 expect(authenticated&&profilePending&&!profilesWarmed,"renewal after workspace 401 preserves discovery request");
 fakeMillis=nextProfileTry;handleProfile();
 expect(profileCalls==1&&profileRefreshActive,"discovery resumes after renewal and retry deadline");
 reset();authenticated=true;accessExpires=fakeNow+3600;currentOrgId="";profileRefreshActive=true;refreshWorkspace="org_test";
 badgeHttp.own(BadgeHttpKind::WORKSPACE_SESSION,400,R"({"error":"invalid_grant"})");
 handleProfile();
 expect(!authenticated&&refreshToken.isEmpty()&&accessToken.isEmpty()&&profilePending&&stateTitle=="Sign in again","workspace invalid_grant clears rejected session and requests reconnect");
 reset();refreshToken="";accessToken="";currentUserId="";currentOrgId="";WiFi.value=0;
 settings.values["session"]=R"({"client_id":"client_test","refresh_token":"saved-refresh","user_id":"user_test","organization_id":"org_test","email":"public@example.test"})";
 settings.values["offline_ctx"]=R"({"client_id":"client_test","user_id":"user_test","organization_id":"org_test"})";
 restoreBootIdentity();handleAuthentication();handleProfile();
 expect(loads==3&&profileReady&&!authenticated&&accessToken.isEmpty()&&accessExpires==0&&badgeHttp.calls.empty(),"saved badge restores offline without granting live authentication");
 reset();settings.values["session"]=R"({"client_id":"client_test","refresh_token":"saved-refresh","user_id":"user_test","organization_id":"org_test"})";
 settings.values["offline_ctx"]=R"({"client_id":"client_test","user_id":"user_other","organization_id":"org_test"})";
 restoreBootIdentity();
 expect(loads==0&&!profileReady&&!authenticated,"mismatched saved owner cannot restore another badge");
 reset();authenticated=true;accessExpires=fakeNow+3600;selectedProvider=PROFILE_GITHUB;
 badgeHttp.own(BadgeHttpKind::PROFILE_LINKEDIN,200,R"({"state":"connected","provider":"linkedin","profile":{"name":"Example User","id":"linkedin-test","handle":null,"url":null,"avatarUrl":null}})");
 expect(refreshCachedProfile(PROFILE_LINKEDIN)==1&&profileCache[PROFILE_LINKEDIN].connected&&!profileReady&&selectedProvider==PROFILE_GITHUB,"cold LinkedIn fill preserves selected GitHub without early fallback");
 badgeHttp.own(BadgeHttpKind::PROFILE_X,200,R"({"state":"connected","provider":"x","profile":{"name":"Example User","id":"x-test","handle":"@example","url":"https://x.com/example","avatarUrl":null}})");
 expect(refreshCachedProfile(PROFILE_X)==1&&profileCache[PROFILE_X].connected&&!profileReady&&selectedProvider==PROFILE_GITHUB,"cold X fill preserves selected GitHub without early fallback");
 badgeHttp.own(BadgeHttpKind::PROFILE_GITHUB,200,R"({"state":"connected","provider":"github","profile":{"name":"Example User","id":"123","handle":"@example","url":"https://github.com/example","avatarUrl":null}})");
 expect(refreshCachedProfile(PROFILE_GITHUB)==1&&profileCache[PROFILE_GITHUB].connected&&profileReady&&selectedProvider==PROFILE_GITHUB&&profileProvider==PROFILE_GITHUB,"cold GitHub fill activates the remembered account when ready");
 std::cout<<"All extracted scheduler regression checks passed under ASan/UBSan.\n";
}
