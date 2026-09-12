#pragma once
WebServer portal(80);
DNSServer portalDns;
bool portalActive=false,portalConnecting=false;
String portalName,portalPassword,portalNonce,pendingSsid,pendingPassword,portalMessage;
uint32_t portalDeadline=0,portalConnectDeadline=0;

void drawBack() {
 auto &d=M5.Display;d.fillRoundRect(155,393,158,46,18,0x2104);d.setTextDatum(middle_center);d.setTextColor(TFT_WHITE,TFT_BLACK);d.setFont(&fonts::FreeSans9pt7b);d.drawString("Back",234,416);
}
void menuItem(int y,const char *title,const String &subtitle,int height=82) {
 auto &d=M5.Display;d.fillRoundRect(65,y,338,height,18,0x18C3);
 d.setTextDatum(middle_left);d.setTextColor(TFT_WHITE);d.setFont(&fonts::FreeSansBold12pt7b);d.drawString(title,90,y+(height==82?25:19));
 d.setFont(&fonts::FreeSans9pt7b);d.setTextColor(0xAD75);d.drawString(subtitle,90,y+(height==82?58:45));
}
void renderSettings() {
 auto &d=M5.Display;d.startWrite();d.fillScreen(TFT_BLACK);d.setTextDatum(middle_center);d.setTextColor(TFT_WHITE);d.setFont(&fonts::FreeSansBold18pt7b);d.drawString("Settings",234,65);
 menuItem(96,"Wi-Fi",WiFi.status()==WL_CONNECTED?"Connected":"Set up a network",64);
 menuItem(169,"AuthKit / chan.dev",authenticated&&time(nullptr)<accessExpires?"Production Devices: connected":"Connect to Production Devices",64);
 menuItem(242,"Profile",badgeProfileIsReady()?cachedProfileStatus(selectedProvider):String(profileProviderName())+" / connect or refresh",64);
 menuItem(315,"X replies","Hold blue to speak a reply",64);
 drawBack();d.endWrite();d.display();
}
void renderWifi() {
 auto &d=M5.Display;d.startWrite();d.fillScreen(TFT_BLACK);d.setTextDatum(middle_center);d.setTextColor(TFT_WHITE);d.setFont(&fonts::FreeSansBold18pt7b);d.drawString("Wi-Fi",234,49);
 if(portalActive) {
   d.setFont(&fonts::FreeSans9pt7b);d.drawString("Scan to join setup hotspot",234,83);
   String qr="WIFI:T:WPA;S:"+portalName+";P:"+portalPassword+";;";
   d.qrcode(qr.c_str(),94,103,280,1,true);
   d.setFont(&fonts::Font0);d.setTextSize(2);d.drawString(portalName,234,401);d.setTextSize(1);
   d.setFont(&fonts::FreeSans9pt7b);d.drawString(portalConnecting?"Connecting...":"Then open 192.168.4.1",234,430);
 } else {
   d.setFont(&fonts::FreeSansBold12pt7b);d.drawString(WiFi.status()==WL_CONNECTED?"Connected":"Not connected",234,163);
   d.setFont(&fonts::FreeSans9pt7b);d.drawString(portalMessage.length()?portalMessage:"Set up using your phone",234,205);
   menuItem(258,"Set up Wi-Fi","Open setup hotspot");drawBack();
 }
 d.endWrite();d.display();
}
String htmlEscape(String s) {s.replace("&","&amp;");s.replace("<","&lt;");s.replace(">","&gt;");s.replace("\"","&quot;");s.replace("'","&#39;");return s;}
String portalPage(const String &message="") {
 String page=F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Devices Wi-Fi</title><style>body{font:17px system-ui;background:#101114;color:#fff;max-width:420px;margin:48px auto;padding:24px}h1{font-size:34px}label{display:block;margin:24px 0 8px}input,button{box-sizing:border-box;width:100%;padding:15px;border-radius:12px;border:1px solid #555;font:inherit}input{background:#24262b;color:#fff}button{margin-top:24px;background:#fff;color:#111;font-weight:650}p{color:#bbb;line-height:1.5}</style></head><body><p>chan.dev / Devices</p><h1>Connect Wi-Fi</h1>");
 page+="<p>"+htmlEscape(message.length()?message:"Choose a 2.4 GHz network. Your password stays on this device.")+"</p>";
 page+="<form method='post' action='/save'><input type='hidden' name='nonce' value='"+portalNonce+"'><label for='ssid'>Network name</label><input id='ssid' name='ssid' maxlength='32' required autocomplete='off'><label for='password'>Password</label><input id='password' name='password' type='password' maxlength='64' autocomplete='off'><button>Connect</button></form></body></html>";
 return page;
}
bool fromHotspot() {return portal.client().localIP()==WiFi.softAPIP();}
void privateHeaders() {portal.sendHeader("Cache-Control","no-store");portal.sendHeader("Referrer-Policy","no-referrer");portal.sendHeader("X-Content-Type-Options","nosniff");portal.sendHeader("Content-Security-Policy","default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'");}
void stopPortal() {
 if(!portalActive)return;
 portal.stop();portalDns.stop();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);
 portalActive=false;portalConnecting=false;portalPassword="";portalNonce="";pendingPassword="";
 if(wifiStarted && WiFi.status()!=WL_CONNECTED)WiFi.reconnect();
 redraw=true;
}
void startPortal() {
 if(portalActive)return;
 uint8_t random[16];esp_fill_random(random,sizeof(random));
 char pass[13],nonce[33];for(int i=0;i<6;i++)snprintf(pass+i*2,3,"%02x",random[i]);for(int i=0;i<16;i++)snprintf(nonce+i*2,3,"%02x",random[i]);
 portalPassword=pass;portalNonce=nonce;portalName="Chan-Devices-AAA0";
 WiFi.mode(WIFI_AP_STA);WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
 if(!WiFi.softAP(portalName.c_str(),portalPassword.c_str(),1,0,2)) {portalMessage="Could not start hotspot";redraw=true;return;}
 portalDns.start(53,"*",WiFi.softAPIP());
 portal.on("/",HTTP_GET,[](){if(!fromHotspot()){portal.send(403);return;}privateHeaders();portal.send(200,"text/html",portalPage());});
 portal.on("/save",HTTP_POST,[](){
   if(!fromHotspot()||portal.arg("nonce")!=portalNonce) {portal.send(403);return;}
   String ssid=portal.arg("ssid"),password=portal.arg("password");
   if(ssid.length()<1||ssid.length()>32||password.length()>64) {privateHeaders();portal.send(400,"text/html",portalPage("Check the network name and password."));return;}
   pendingSsid=ssid;pendingPassword=password;portalConnecting=true;portalConnectDeadline=millis()+25000;
   WiFi.disconnect(false,false);WiFi.begin(ssid.c_str(),password.c_str());wifiStarted=true;
   privateHeaders();portal.send(200,"text/html",portalPage("Connecting. Watch your device for confirmation. If it cannot connect, check the password and try again."));redraw=true;
 });
 portal.onNotFound([](){if(!fromHotspot()){portal.send(403);return;}portal.sendHeader("Location","http://192.168.4.1/",true);privateHeaders();portal.send(302,"text/plain","");});
 portal.begin();portalActive=true;portalDeadline=millis()+600000;screen=WIFI_SETTINGS;redraw=true;Serial.println("PORTAL_STARTED");
}
void handlePortal() {
 if(!portalActive)return;
 portalDns.processNextRequest();portal.handleClient();
 if(portalConnecting && WiFi.status()==WL_CONNECTED && WiFi.SSID()==pendingSsid) {
   JsonDocument w;w["ssid"]=pendingSsid;w["password"]=pendingPassword;String blob;serializeJson(w,blob);
   if(settings.putString("wifi",blob)!=blob.length())portalMessage="Connected; could not save";else portalMessage="Network saved";
   stopPortal();nextNetworkTry=0;redraw=true;Serial.println("WIFI_CONNECTED settings_saved");
 } else if(portalConnecting && due(portalConnectDeadline)) {
   portalConnecting=false;pendingPassword="";portalMessage="Check password and retry";Serial.println("WIFI_CONNECT_FAILED");redraw=true;
 }
 if(due(portalDeadline)) {stopPortal();portalMessage="Setup timed out. Tap to retry.";}
}
void handleTap(int x,int y) {
 if(screen==REPLIES) {handleVoiceTap(x,y);return;}
 if(screen==BADGE) {
   expanded=!expanded;lastInteraction=millis();
 } else if(screen==SETTINGS) {
   if(x>=65&&x<=403&&y>=96&&y<160) {screen=WIFI_SETTINGS;portalMessage="";}
   else if(x>=65&&x<=403&&y>=169&&y<233) {screen=AUTH_SETTINGS;state(authenticated?"Connected":"Not connected",authenticated?accountEmail:"Tap below to sign in");}
   else if(x>=65&&x<=403&&y>=242&&y<306) {settingsProvider=selectedProvider;screen=PROFILE_SETTINGS;}
   else if(x>=65&&x<=403&&y>=315&&y<379)enterVoiceReplies();
   else if(y>=393) {screen=BADGE;showBadge=true;}
 } else if(screen==WIFI_SETTINGS) {
   if(!portalActive&&y>=258&&y<=340)startPortal();
   else if(!portalActive&&y>=393)screen=SETTINGS;
 } else if(screen==AUTH_SETTINGS) {
   if(deviceCode.isEmpty()&&y>=393)screen=SETTINGS;
   else if(deviceCode.isEmpty()&&!authenticated&&y>=290&&y<=380) {
     if(WiFi.status()!=WL_CONNECTED)state("Wi-Fi needed","Connect in Settings / Wi-Fi");
     else {needPair=true;nextNetworkTry=0;}
   }
 } else if(screen==PROFILE_SETTINGS) {
   if(y>=393)screen=SETTINGS;
   else if(y>=91 && y<=135) {
     for(uint8_t i=0;i<ACCOUNT_PROVIDER_COUNT;i++) {
       int left=58+i*120;
       if(x>=left && x<=left+112)selectProfileSettingsProvider(ProfileProvider(ACCOUNT_PAGE_ORDER[i]));
     }
   } else if(y>=350 && y<=384 && x>=120 && x<=348) {
     if(WiFi.status()!=WL_CONNECTED) {profileState="wifi_required";screen=WIFI_SETTINGS;}
     else if(!authenticated && refreshToken.isEmpty()) {profileState="sign_in_required";screen=AUTH_SETTINGS;}
     else queueProfileRefresh();
   }
 }
 redraw=true;
 Serial.printf("UI_NAV screen=%d\n",int(screen));
}
