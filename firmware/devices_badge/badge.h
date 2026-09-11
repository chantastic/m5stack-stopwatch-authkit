#pragma once
#include "init_wordmark.h"
#include "github_mark.h"
// X's original vector outline, filled with the even-odd rule.
void drawX(int x, int y, int size, uint16_t color=TFT_WHITE) {
  static const float outer[][2]={{14.234,10.162},{22.977,0},{20.905,0},{13.314,8.824},{7.251,0},{.258,0},{9.426,13.343},{.258,24},{2.33,24},{10.346,14.682},{16.749,24},{23.742,24}};
  static const float inner[][2]={{11.397,13.461},{10.468,12.132},{3.076,1.56},{6.258,1.56},{12.223,10.092},{13.152,11.421},{20.906,22.511},{17.724,22.511}};
  for(int row=0;row<size;row++) {
    float yy=(row+.5f)*24/size, hits[24]; int count=0;
    auto edges=[&](const float pts[][2],int n) {
      for(int i=0,j=n-1;i<n;j=i++) {
        if((pts[i][1]>yy)!=(pts[j][1]>yy)) hits[count++]=pts[i][0]+(yy-pts[i][1])*(pts[j][0]-pts[i][0])/(pts[j][1]-pts[i][1]);
      }
    };
    edges(outer,12); edges(inner,8); std::sort(hits,hits+count);
    for(int i=0;i+1<count;i+=2) {
      int a=ceilf(hits[i]*size/24-.5f),b=ceilf(hits[i+1]*size/24-.5f);
      M5.Display.fillRect(x+a,y+row,b-a,1,color);
    }
  }
}

// A monochrome LinkedIn "in" mark keeps the provider legible in every palette.
void drawLinkedIn(int x,int y,int size,uint16_t color=TFT_WHITE) {
  auto &d=M5.Display;
  auto rect=[&](float left,float top,float width,float height) {
    d.fillRect(x+int(left*size/24),y+int(top*size/24),max(1,int(ceilf(width*size/24))),max(1,int(ceilf(height*size/24))),color);
  };
  d.fillCircle(x+int(4*size/24.0f),y+int(4*size/24.0f),max(1,int(2.2f*size/24)),color);
  rect(2,8,4,14);rect(9,8,4,14);rect(19,14,4,8);
  for(int row=0;row<size;row++) {
    float yy=(row+.5f)*24/size;
    if(yy<8 || yy>14)continue;
    float dy=yy-14,outer=sqrtf(max(0.0f,36-dy*dy));
    float inner=fabsf(dy)<2?sqrtf(max(0.0f,4-dy*dy)):0;
    int left=x+floorf((17-outer)*size/24),right=x+ceilf((17+outer)*size/24);
    if(inner==0)d.fillRect(left,y+row,right-left,1,color);
    else {
      int holeLeft=x+int((17-inner)*size/24),holeRight=x+int((17+inner)*size/24);
      d.fillRect(left,y+row,max(0,holeLeft-left),1,color);d.fillRect(holeRight,y+row,max(0,right-holeRight),1,color);
    }
  }
}
void drawProviderIcon(int x,int y,int size,uint16_t color=TFT_WHITE) {
  if(selectedProvider==PROFILE_LINKEDIN)drawLinkedIn(x,y,size,color);
  else if(selectedProvider==PROFILE_GITHUB)drawGitHub(x,y,size,color);
  else drawX(x,y,size,color);
}

// Trim at UTF-8 boundaries so long display names remain inside the round face.
String badgeFitText(String text,int maxWidth) {
  auto &d=M5.Display;
  if(d.textWidth(text)<=maxWidth)return text;
  while(text.length() && d.textWidth(text+"...")>maxWidth) {
    int cut=text.length()-1;
    while(cut>0 && (uint8_t(text[cut])&0xC0)==0x80)--cut;
    text.remove(cut);
  }
  return text+"...";
}

String badgeHandle() {
  if(selectedProvider==PROFILE_LINKEDIN)return profileHandle.isEmpty()?String("LinkedIn"):profileHandle;
  return profileHandle.startsWith("@")?profileHandle:"@"+profileHandle;
}

void drawAvatarPlaceholder(int left,int top) {
  auto &d=M5.Display;
  d.fillCircle(left+80,top+80,79,0x18C3);
  d.fillCircle(left+80,top+57,24,0x8410);
  d.fillRoundRect(left+36,top+91,88,43,21,0x8410);
}

void drawProfileQr(int left,int top,int size) {
  // M5GFX selects a QR version and supplies a four-module white quiet zone.
  if(!profileUrl.isEmpty())M5.Display.qrcode(profileUrl.c_str(),left,top,size,1,true);
}
#include "palette_qr.h"

void drawProfileStatus(int cx) {
  auto &d=M5.Display;
  String title="Profile unavailable",hint="Press both buttons for Settings";
  if(profileState=="loading" || profileState=="refreshing") {
    title="Loading your profile";hint=String("Connecting to ")+profileProviderName();
  } else if(profileState=="wifi_required" || profileState=="offline") {
    title="Wi-Fi needed";hint="Press both buttons for Settings";
  } else if(profileState=="sign_in_required") {
    title="Connect your account";hint="Press both buttons for Settings";
  } else if(profileState=="connect_required") {
    title=String("Connect ")+profileProviderName();hint="auth.chan.dev/connections";
  } else if(profileState=="access_denied" || profileState=="credits_required") {
    title=String(profileProviderName())+" profile unavailable";hint="Check your connection";
  } else if(profileState=="rate_limited") {
    title="Please try again soon";hint=String(profileProviderName())+" is limiting requests";
  }
  drawAvatarPlaceholder(cx-80,34);
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextColor(TFT_WHITE,TFT_BLACK);
  d.drawString(badgeFitText(title,360),cx,234);
  d.setFont(&fonts::FreeSans9pt7b);
  d.setTextColor(0xAD75,TFT_BLACK);
  d.drawString(badgeFitText(hint,360),cx,277);
  drawProviderIcon(cx-15,325,30);
  d.setFont(&fonts::Font0);
  d.drawString("chan.dev / Devices",cx,403);
}

// Drawing-only faces share the same verified profile and expanded QR.
static constexpr uint8_t BADGE_DESIGN_COUNT=18;
const char* const BADGE_DESIGN_NAMES[BADGE_DESIGN_COUNT]={
  "X ASCII","X Timeline","X Portrait","X Summit","X Grove","X Tide",
  "LinkedIn ASCII","LinkedIn Profile","LinkedIn Card","LinkedIn Summit","LinkedIn Grove","LinkedIn Tide",
  "GitHub ASCII","GitHub Panels","GitHub README","GitHub Summit","GitHub Grove","GitHub Tide"
};
constexpr uint16_t badgeRgb(uint32_t rgb) {
  return ((rgb>>8)&0xF800)|((rgb>>5)&0x07E0)|((rgb>>3)&0x001F);
}
static constexpr uint16_t BADGE_INIT_BLACK=badgeRgb(0x0B0B0E),BADGE_INIT_WHITE=badgeRgb(0xF5F2E8);
static constexpr uint16_t BADGE_INIT_GRAY=badgeRgb(0x96969C);

bool badgeProfileIsReady() {
  return profileReady && profileProvider==uint8_t(selectedProvider) && profileOwner==currentUserId && profileWorkspace==currentOrgId &&
    !currentUserId.isEmpty() && !profileName.isEmpty() &&
    (selectedProvider==PROFILE_LINKEDIN || (!profileHandle.isEmpty() && !profileUrl.isEmpty()));
}
// Scaling uses the existing RAM sprite. Circle masks are drawn once per redraw.
void badgeAvatar(int cx,int cy,int size,bool circular,uint16_t background) {
  auto &d=M5.Display;
  const int left=cx-size/2,top=cy-size/2;
  if(profileAvatarReady) {
    profileAvatar->setPivot(profileAvatar->width()*.5f,profileAvatar->height()*.5f);
    float scaleX=float(size)/profileAvatar->width(),scaleY=float(size)/profileAvatar->height();
    profileAvatar->pushRotateZoom(cx,cy,0,scaleX,scaleY);
    if(circular) {
      float radius=size*.5f;
      for(int row=0;row<size;row++) {
        float dy=row+.5f-radius;
        int inset=ceilf(radius-sqrtf(max(0.0f,radius*radius-dy*dy)));
        if(inset>0) {
          d.fillRect(left,top+row,inset,1,background);
          d.fillRect(left+size-inset,top+row,inset,1,background);
        }
      }
    }
  } else {
    if(circular)d.fillCircle(cx,cy,size/2,badgeRgb(0x505B54));
    else d.fillRect(left,top,size,size,badgeRgb(0x505B54));
    d.fillCircle(cx,top+size*36/100,size*15/100,badgeRgb(0xA6B2A9));
    d.fillRoundRect(cx-size*28/100,top+size*57/100,size*56/100,size*27/100,size*13/100,badgeRgb(0xA6B2A9));
  }
}

void badgeTiny(const String &text,int x,int y,uint16_t color,uint16_t background,int size=1) {
  auto &d=M5.Display;d.setFont(&fonts::Font0);d.setTextSize(size);
  d.setTextDatum(middle_center);d.setTextColor(color,background);d.drawString(text,x,y);d.setTextSize(1);
}
void badgeName(int x,int y,int width,uint16_t color,uint16_t background,const lgfx::IFont *font) {
  auto &d=M5.Display;d.setTextSize(1);d.setFont(font);d.setTextDatum(middle_center);d.setTextColor(color,background);
  const String name=profileName.isEmpty()?profileHandle:profileName;
  // Fit ordinary full names before truncating, keeping each design's typeface.
  // Stop at 12pt so unusually long names cannot become unreadably small.
  bool mono=font==&fonts::FreeMonoBold24pt7b || font==&fonts::FreeMonoBold18pt7b || font==&fonts::FreeMonoBold12pt7b;
  bool serif=font==&fonts::FreeSerifBold24pt7b || font==&fonts::FreeSerifBold18pt7b || font==&fonts::FreeSerifBold12pt7b;
  bool large=font==&fonts::FreeMonoBold24pt7b || font==&fonts::FreeSerifBold24pt7b || font==&fonts::FreeSansBold24pt7b;
  if(d.textWidth(name)>width && large)d.setFont(mono?&fonts::FreeMonoBold18pt7b:serif?&fonts::FreeSerifBold18pt7b:&fonts::FreeSansBold18pt7b);
  if(d.textWidth(name)>width)d.setFont(mono?&fonts::FreeMonoBold12pt7b:serif?&fonts::FreeSerifBold12pt7b:&fonts::FreeSansBold12pt7b);
  d.drawString(badgeFitText(name,width),x,y);
}
void badgeIdentity(int cx,int y,int maxWidth,uint16_t color,uint16_t background,const lgfx::IFont *font=&fonts::FreeSans9pt7b,int icon=17) {
  auto &d=M5.Display;d.setTextSize(1);d.setFont(font);d.setTextColor(color,background);
  const String text=badgeFitText(badgeHandle(),maxWidth-icon-10);
  const int left=cx-(d.textWidth(text)+icon+10)/2;
  drawProviderIcon(left,y-icon/2,icon,color);
  d.setTextDatum(middle_left);d.drawString(text,left+icon+10,y);d.setTextDatum(middle_center);
}
void badgeFooter(uint16_t color,uint16_t background) {
  const uint8_t style=selectedBadgeStyle();
  String label=String(profileProviderName())+" / ";
  if(style>=3 && style<=5) {
    static const char * const outdoorNames[]={"Summit","Grove","Tide"};
    label=String(profileProviderName())+" "+outdoorNames[style-3]+" ";
    label+=String(style+1)+"/"+String(BADGE_STYLE_COUNT);
  } else label+=String(style+1)+" OF "+String(BADGE_STYLE_COUNT);
  badgeTiny(label,234,style>=3?438:445,color,background);
}

// Sample the current profile sprite into a 30 × 24 grid. Character density
// carries the portrait's luminance, so this remains the user's real avatar.
void badgeInitAsciiAvatar(int left,int top) {
  auto &d=M5.Display;
  if(!profileAvatarReady || profileAvatar->width()<1 || profileAvatar->height()<1) {
    badgeAvatar(left+90,top+96,160,false,BADGE_INIT_BLACK);return;
  }
  static constexpr char ramp[]=" .-=+in{}()#";
  d.setFont(&fonts::Font0);d.setTextSize(1);d.setTextDatum(top_left);
  d.setTextColor(BADGE_INIT_WHITE,BADGE_INIT_BLACK);
  for(int row=0;row<24;row++) {
    char text[31];
    int sourceY=(row*2+1)*profileAvatar->height()/48;
    for(int col=0;col<30;col++) {
      int sourceX=(col*2+1)*profileAvatar->width()/60;
      uint16_t pixel=profileAvatar->readPixel(sourceX,sourceY);
      int red=((pixel>>11)&31)*255/31,green=((pixel>>5)&63)*255/63,blue=(pixel&31)*255/31;
      int luminance=(77*red+150*green+29*blue)>>8;
      text[col]=ramp[luminance*(sizeof(ramp)-2)/255];
    }
    text[30]=0;d.drawString(text,left,top+row*8);
  }
  d.setTextDatum(middle_center);
}

void badgeInitAscii() {
  auto &d=M5.Display;d.fillScreen(BADGE_INIT_BLACK);
  drawInitWordmark(120,52,228,BADGE_INIT_WHITE);
  badgeTiny("LIVE / ASCII",140,120,BADGE_INIT_GRAY,BADGE_INIT_BLACK);
  badgeInitAsciiAvatar(50,133);
  badgeTiny("SCAN / PROFILE",329,152,BADGE_INIT_GRAY,BADGE_INIT_BLACK);
  drawProfileQr(259,169,140);
  badgeName(234,362,366,BADGE_INIT_WHITE,BADGE_INIT_BLACK,&fonts::FreeMonoBold24pt7b);
  badgeIdentity(234,403,284,BADGE_INIT_WHITE,BADGE_INIT_BLACK,&fonts::FreeMono9pt7b,17);
  badgeFooter(BADGE_INIT_GRAY,BADGE_INIT_BLACK);
}

// Profile-header portrait masking keeps the two backgrounds intact where the
// circle crosses from the cover into the body. It uses only cached pixels.
void badgeSplitAvatar(int cx,int cy,int size,int splitY,uint16_t above,uint16_t below) {
  auto &d=M5.Display;badgeAvatar(cx,cy,size,false,below);
  const int left=cx-size/2,top=cy-size/2;const float radius=size*.5f;
  for(int row=0;row<size;row++) {
    float dy=row+.5f-radius;
    int inset=ceilf(radius-sqrtf(max(0.0f,radius*radius-dy*dy)));
    uint16_t background=top+row<splitY?above:below;
    if(inset) {
      d.fillRect(left,top+row,inset,1,background);
      d.fillRect(left+size-inset,top+row,inset,1,background);
    }
  }
}

// LinkedIn / Profile: an app-like blue cover and warm-white profile body.
// The avatar overlaps the cover; the QR occupies the companion column.
void badgeLinkedInProfile() {
  auto &d=M5.Display;
  const uint16_t blue=badgeRgb(0x0A66C2),paper=badgeRgb(0xF3F2EF);
  const uint16_t ink=badgeRgb(0x172B3B),muted=badgeRgb(0x596D7C);
  d.fillScreen(paper);d.fillRect(0,0,468,138,blue);
  drawInitWordmark(123,45,222,TFT_WHITE);
  badgeTiny("LINKEDIN / PROFILE",290,116,TFT_WHITE,blue);
  badgeSplitAvatar(139,186,146,138,blue,paper);
  d.drawCircle(139,186,74,paper);d.drawCircle(139,186,75,paper);
  badgeTiny("PROFILE PHOTO",139,283,muted,paper);
  badgeTiny("OPEN PROFILE",320,145,muted,paper);
  drawProfileQr(246,160,148);
  badgeName(234,349,370,ink,paper,&fonts::FreeSansBold24pt7b);
  badgeIdentity(234,391,330,blue,paper,&fonts::FreeSans9pt7b,18);
  d.drawFastHLine(161,420,146,badgeRgb(0xCED6DA));
  badgeFooter(muted,paper);
}

// LinkedIn / Card: restrained navy stationery, large serif identity, and a
// photograph/QR pair beneath it. No role, company, or credential is invented.
void badgeLinkedInCard() {
  auto &d=M5.Display;
  const uint16_t navy=badgeRgb(0x0A2238),white=badgeRgb(0xF5F2E8);
  const uint16_t blue=badgeRgb(0x8CCBFF),line=badgeRgb(0x37546A);
  d.fillScreen(navy);
  drawInitWordmark(111,43,246,white);
  badgeTiny("LINKEDIN / CONTACT",234,115,blue,navy);
  d.drawFastHLine(99,137,270,line);
  badgeName(234,177,370,white,navy,&fonts::FreeSerifBold24pt7b);
  badgeIdentity(234,216,350,blue,navy,&fonts::FreeSans9pt7b,18);
  badgeAvatar(134,311,124,true,navy);
  d.drawCircle(134,311,67,line);d.drawCircle(134,311,70,line);
  drawProfileQr(246,244,146);
  badgeTiny("PROFILE",134,399,blue,navy);
  d.drawFastHLine(171,422,126,line);
  badgeFooter(white,navy);
}

// X / Timeline: a real profile header and a simple thread rail leading to its
// public-profile QR. The face makes no claims about posts, follows, or counts.
void badgeXTimeline() {
  auto &d=M5.Display;
  const uint16_t black=badgeRgb(0x050505),white=badgeRgb(0xF5F2E8);
  const uint16_t muted=badgeRgb(0x8C939B),line=badgeRgb(0x30363D);
  d.fillScreen(black);
  drawInitWordmark(109,43,250,white);
  d.drawFastHLine(76,117,316,line);
  badgeAvatar(102,173,76,true,black);
  badgeName(270,163,242,white,black,&fonts::FreeSansBold18pt7b);
  badgeIdentity(270,201,230,muted,black,&fonts::FreeSans9pt7b,16);
  d.drawFastVLine(102,218,136,line);
  d.drawCircle(102,374,16,line);drawX(92,364,20,white);
  badgeTiny("PUBLIC PROFILE / X.COM",285,232,muted,black);
  drawProfileQr(202,248,166);
  badgeFooter(muted,black);
}

// X / Portrait: a photograph-led poster with a high-contrast header and caption
// block. A solid QR quiet zone stays readable regardless of the source image.
void badgeXPortrait() {
  auto &d=M5.Display;
  const uint16_t paper=badgeRgb(0xFAFAF7),ink=badgeRgb(0x101010);
  d.fillScreen(ink);
  badgeAvatar(234,214,468,false,ink);
  d.fillRect(0,0,468,112,ink);
  drawInitWordmark(112,42,244,paper);
  drawProfileQr(274,148,140);
  d.fillRect(0,313,468,155,paper);
  d.fillRect(71,313,326,4,ink);
  badgeName(234,352,368,ink,paper,&fonts::FreeSansBold24pt7b);
  badgeIdentity(234,397,320,ink,paper,&fonts::FreeMono9pt7b,18);
  badgeFooter(ink,paper);
}

// GitHub / Panels: the native dark profile palette, two bordered content panes,
// then the actual account identity. The panels are visual frames, not buttons.
void badgeGitHubPanels() {
  auto &d=M5.Display;
  const uint16_t bg=badgeRgb(0x0D1117),panel=badgeRgb(0x161B22);
  const uint16_t white=badgeRgb(0xF0F3F6),muted=badgeRgb(0x9198A1),line=badgeRgb(0x3D444D);
  d.fillScreen(bg);
  drawInitWordmark(116,44,236,white);
  badgeTiny("GITHUB / PROFILE",234,116,muted,bg);
  badgeTiny("AVATAR",143,143,muted,bg);
  badgeTiny("GITHUB.COM",319,143,muted,bg);
  d.fillRoundRect(62,156,160,160,10,panel);d.drawRoundRect(62,156,160,160,10,line);
  badgeAvatar(142,236,140,false,panel);
  d.fillRoundRect(240,156,160,160,10,panel);d.drawRoundRect(240,156,160,160,10,line);
  drawProfileQr(243,159,154);
  badgeName(234,353,370,white,bg,&fonts::FreeSansBold24pt7b);
  badgeIdentity(234,393,330,white,bg,&fonts::FreeMono9pt7b,18);
  d.drawFastHLine(163,420,142,line);
  badgeFooter(muted,bg);
}

// GitHub / README: light Markdown-like typography and a compact portrait. The
// heading is the live name, and the only link destination is the real profile.
void badgeGitHubReadme() {
  auto &d=M5.Display;
  const uint16_t paper=badgeRgb(0xF6F8FA),ink=badgeRgb(0x1F2328);
  const uint16_t blue=badgeRgb(0x0969DA),muted=badgeRgb(0x59636E),line=badgeRgb(0xD1D9E0);
  d.fillScreen(paper);
  drawInitWordmark(120,45,228,ink);
  badgeTiny("README.md",234,115,muted,paper,2);
  d.drawFastHLine(77,139,314,line);
  badgeAvatar(115,210,100,true,paper);
  badgeTiny("# PROFILE",284,161,blue,paper,2);
  badgeName(285,199,222,ink,paper,&fonts::FreeSansBold18pt7b);
  badgeIdentity(284,238,220,muted,paper,&fonts::FreeMono9pt7b,16);
  badgeTiny("## links",136,303,ink,paper,2);
  badgeTiny("github.com",136,331,muted,paper);
  d.drawFastHLine(99,361,92,blue);
  d.fillTriangle(192,355,202,361,192,367,blue);
  drawProfileQr(246,256,146);
  badgeFooter(muted,paper);
}

#include "outdoor_badge.h"

// A missing LinkedIn URL is setup, not a guessed destination. Keep the
// connected name/photo visible and label the separate Connections QR clearly.
void drawProfileLinkSetup() {
  auto &d=M5.Display;
  d.fillScreen(TFT_BLACK);
  if(expanded) {
    d.setFont(&fonts::FreeSansBold12pt7b);d.setTextColor(TFT_WHITE,TFT_BLACK);d.setTextDatum(middle_center);
    drawInitWordmark(164,33,140,TFT_WHITE);
    d.drawString("Add profile link",234,95);
    d.qrcode(profileConnectionsUrl(),94,136,280,1,true);
    badgeTiny("OPEN CONNECTIONS, THEN REFRESH",234,438,0xAD75,TFT_BLACK);
    return;
  }
  badgeAvatar(234,89,104,true,TFT_BLACK);
  badgeName(234,172,350,TFT_WHITE,TFT_BLACK,&fonts::FreeSansBold18pt7b);
  badgeIdentity(234,211,330,TFT_WHITE,TFT_BLACK);
  d.setFont(&fonts::FreeSans9pt7b);d.setTextColor(0xAD75,TFT_BLACK);d.setTextDatum(middle_center);
  d.drawString("Add your public profile link",234,251);
  d.qrcode(profileConnectionsUrl(),164,274,140,1,true);
  badgeTiny("SCAN TO OPEN CONNECTIONS",234,438,0xAD75,TFT_BLACK);
}

void drawBadge() {
  auto &d=M5.Display;
  d.startWrite();d.fillScreen(TFT_BLACK);d.setTextSize(1);
  d.setTextColor(TFT_WHITE,TFT_BLACK);d.setTextDatum(middle_center);
  if(!badgeProfileIsReady()) {
    // A previous user's personal imagery never appears on an unready face.
    drawProfileStatus(d.width()/2);
    badgeFooter(0xAD75,TFT_BLACK);
  } else if(profileUrl.isEmpty()) {
    drawProfileLinkSetup();
  } else if(expanded) {
    d.setFont(&fonts::FreeSansBold12pt7b);
    drawInitWordmark(164,33,140,TFT_WHITE);
    d.drawString(badgeFitText(badgeHandle(),300),234,95);
    drawProfileQr(94,136,280);
    d.setFont(&fonts::FreeSans9pt7b);d.setTextColor(0xAD75,TFT_BLACK);
    String shortUrl=profileUrl;if(shortUrl.startsWith("https://"))shortUrl.remove(0,8);
    d.drawString(badgeFitText(shortUrl,200),234,438);
  } else {
    uint8_t style=selectedBadgeStyle();
    if(style>=3) {
      if(style==3)badgeOutdoorSummit();
      else if(style==4)badgeOutdoorGrove();
      else badgeOutdoorTide();
    } else if(style==0)badgeInitAscii();
    else if(selectedProvider==PROFILE_LINKEDIN) {
      if(style==1)badgeLinkedInProfile();else badgeLinkedInCard();
    } else if(selectedProvider==PROFILE_GITHUB) {
      if(style==1)badgeGitHubPanels();else badgeGitHubReadme();
    } else {
      if(style==1)badgeXTimeline();else badgeXPortrait();
    }
  }
  d.setTextDatum(middle_center);d.setTextSize(1);d.endWrite();d.display();
}
