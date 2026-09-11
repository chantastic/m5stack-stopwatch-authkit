#pragma once

// Flat camp-emblem colors sampled by eye from the supplied visual reference.
static constexpr uint16_t OUT_BROWN=badgeRgb(0x3A3027),OUT_SAND=badgeRgb(0xD8C79A);
static constexpr uint16_t OUT_TEAL=badgeRgb(0x86A69E),OUT_RUST=badgeRgb(0xCC7047);
static constexpr uint16_t OUT_OLIVE=badgeRgb(0x62664A),OUT_GOLD=badgeRgb(0xCDA653);
static constexpr uint16_t OUT_CLAY=badgeRgb(0xA28C62);

void outdoorStroke(int x1,int y1,int x2,int y2,int width,uint16_t color) {
  auto &d=M5.Display;
  float dx=x2-x1,dy=y2-y1,length=sqrtf(dx*dx+dy*dy);
  if(length<1) {d.fillCircle(x1,y1,max(1,width/2),color);return;}
  float half=width*.5f,px=-dy*half/length,py=dx*half/length;
  d.fillTriangle(int(x1+px),int(y1+py),int(x2+px),int(y2+py),int(x2-px),int(y2-py),color);
  d.fillTriangle(int(x1+px),int(y1+py),int(x2-px),int(y2-py),int(x1-px),int(y1-py),color);
  d.fillCircle(x1,y1,max(1,width/2),color);d.fillCircle(x2,y2,max(1,width/2),color);
}

void outdoorPeak(int left,int bottom,int peakX,int peakY,int right,uint16_t fill) {
  auto &d=M5.Display;d.fillTriangle(left,bottom,peakX,peakY,right,bottom,fill);
  outdoorStroke(left,bottom,peakX,peakY,6,OUT_BROWN);
  outdoorStroke(peakX,peakY,right,bottom,6,OUT_BROWN);
  outdoorStroke(left,bottom,right,bottom,6,OUT_BROWN);
}

void outdoorPine(int cx,int top,int height,int halfWidth,uint16_t color,int weight=4) {
  outdoorStroke(cx,top,cx,top+height,weight,color);
  for(int branch=0;branch<5;branch++) {
    int y=top+height*(18+branch*14)/100;
    int span=halfWidth*(45+branch*14)/100;
    outdoorStroke(cx,y-8,cx-span,y+height/10,weight,color);
    outdoorStroke(cx,y-8,cx+span,y+height/10,weight,color);
  }
}

// Convert the current avatar to four ink tones one row at a time. Explicit
// rgb565_t avoids display byte-order state. Maximum stack storage is 288 bytes;
// there is no image allocation, decoding, network access, or persistent copy.
void outdoorPortrait(int cx,int cy,int size) {
  auto &d=M5.Display;if(size<2 || size>144)return;
  const int radius=size/2,left=cx-radius,top=cy-radius;
  d.fillCircle(cx,cy,radius+7,OUT_BROWN);d.fillCircle(cx,cy,radius+3,OUT_GOLD);
  if(!profileAvatarReady || !profileAvatar || profileAvatar->width()<1 || profileAvatar->height()<1) {
    d.fillCircle(cx,cy,radius,OUT_SAND);
    d.fillCircle(cx,cy-size/7,size/7,OUT_BROWN);
    d.fillRoundRect(cx-size/4,cy+size/12,size/2,size/4,size/8,OUT_BROWN);
    return;
  }
  lgfx::rgb565_t row[144];
  for(int y=0;y<size;y++) {
    float dy=y+.5f-radius;
    int inset=ceilf(radius-sqrtf(max(0.0f,float(radius*radius)-dy*dy)));
    int count=size-2*inset;if(count<1)continue;
    int sourceY=(2*y+1)*profileAvatar->height()/(2*size);
    for(int x=0;x<count;x++) {
      int sourceX=(2*(x+inset)+1)*profileAvatar->width()/(2*size);
      uint16_t pixel=profileAvatar->readPixel(sourceX,sourceY);
      int red=((pixel>>11)&31)*255/31,green=((pixel>>5)&63)*255/63,blue=(pixel&31)*255/31;
      int light=(77*red+150*green+29*blue)>>8;
      row[x]=light<62?OUT_BROWN:light<118?OUT_OLIVE:light<181?OUT_CLAY:OUT_SAND;
    }
    d.pushImage(left+inset,top+y,count,1,row);
  }
  d.drawCircle(cx,cy,radius,OUT_BROWN);
}

void outdoorFinish(uint16_t ring=OUT_GOLD) {
  auto &d=M5.Display;
  // Mask decorative scenery to the emblem before adding its thick ink rim.
  // All identity text and complete QR quiet zones remain inside the inner rim.
  for(int y=0;y<468;y++) {
    int dy=y-234;
    if(abs(dy)>226) {d.drawFastHLine(0,y,468,OUT_BROWN);continue;}
    int inset=ceilf(234-sqrtf(float(226*226-dy*dy)));
    d.drawFastHLine(0,y,inset,OUT_BROWN);d.drawFastHLine(468-inset,y,inset,OUT_BROWN);
  }
  d.drawCircle(234,234,225,ring);d.drawCircle(234,234,224,ring);
  d.drawCircle(234,234,219,OUT_BROWN);d.drawCircle(234,234,220,OUT_BROWN);
  d.drawCircle(234,234,221,OUT_BROWN);
}

// Summit: a geometric mountain crown, with a small INIT maker's mark beneath
// the peak. The lower sand field presents a paired portrait and profile code.
void badgeOutdoorSummit() {
  auto &d=M5.Display;d.fillScreen(OUT_BROWN);d.fillCircle(234,234,226,OUT_SAND);
  d.setClipRect(0,0,468,192);
  d.fillRect(0,0,468,192,OUT_TEAL);
  d.fillCircle(305,94,46,OUT_BROWN);d.fillCircle(305,94,39,OUT_RUST);
  for(int ray=0;ray<9;ray++) {
    float angle=(-165+ray*20)*PI/180;
    outdoorStroke(305+int(54*cosf(angle)),94+int(54*sinf(angle)),305+int(110*cosf(angle)),94+int(110*sinf(angle)),4,OUT_BROWN);
  }
  outdoorPeak(9,192,126,92,254,OUT_OLIVE);
  outdoorPeak(217,192,350,112,467,OUT_RUST);
  outdoorPeak(76,202,234,38,393,OUT_SAND);
  d.fillTriangle(234,43,234,198,387,198,OUT_GOLD);
  outdoorStroke(234,42,234,198,5,OUT_BROWN);
  outdoorStroke(234,42,388,198,6,OUT_BROWN);
  outdoorPine(174,134,50,13,OUT_BROWN,3);
  d.clearClipRect();
  outdoorStroke(15,192,453,192,6,OUT_BROWN);
  d.fillRoundRect(185,166,98,30,14,OUT_BROWN);drawInitWordmark(193,171,82,OUT_SAND);
  badgeName(234,219,354,OUT_BROWN,OUT_SAND,&fonts::FreeSerifBold18pt7b);
  badgeIdentity(234,252,330,OUT_BROWN,OUT_SAND,&fonts::FreeSans9pt7b,17);
  outdoorPortrait(129,333,118);
  drawPaletteProfileQr(222,268,140,OUT_BROWN,OUT_SAND);
  badgeFooter(OUT_BROWN,OUT_SAND);outdoorFinish();
}

// Grove: concentric arch bands frame the portrait, tall line-drawn pines form
// the flanks, and the code is centered between two small forested slopes.
void badgeOutdoorGrove() {
  auto &d=M5.Display;d.fillScreen(OUT_BROWN);d.fillCircle(234,234,226,OUT_SAND);
  d.fillRoundRect(123,24,222,248,111,OUT_BROWN);
  d.fillRoundRect(130,31,208,246,104,OUT_RUST);
  d.fillRoundRect(140,41,188,238,94,OUT_BROWN);
  d.fillRoundRect(147,48,174,240,87,OUT_GOLD);
  d.fillRoundRect(157,58,154,222,77,OUT_BROWN);
  d.fillRoundRect(164,65,140,208,70,OUT_TEAL);
  outdoorPine(78,106,134,22,OUT_BROWN,5);outdoorPine(385,96,146,26,OUT_BROWN,5);
  outdoorPortrait(234,131,110);
  d.fillRoundRect(185,29,98,31,13,OUT_BROWN);drawInitWordmark(194,34,80,OUT_SAND);
  d.fillRect(0,194,468,100,OUT_SAND);
  badgeName(234,221,344,OUT_BROWN,OUT_SAND,&fonts::FreeSerifBold18pt7b);
  badgeIdentity(234,255,330,OUT_BROWN,OUT_SAND,&fonts::FreeSans9pt7b,17);
  d.fillTriangle(0,355,130,308,186,427,OUT_OLIVE);
  d.fillTriangle(280,427,356,300,468,353,OUT_OLIVE);
  outdoorStroke(0,355,130,308,5,OUT_BROWN);outdoorStroke(130,308,186,427,5,OUT_BROWN);
  outdoorStroke(280,427,356,300,5,OUT_BROWN);outdoorStroke(356,300,468,353,5,OUT_BROWN);
  outdoorPine(104,274,105,21,OUT_BROWN,4);outdoorPine(361,269,116,23,OUT_BROWN,4);
  drawPaletteProfileQr(164,269,140,OUT_BROWN,OUT_SAND);
  d.fillRect(0,414,468,7,OUT_RUST);d.fillRect(0,421,468,6,OUT_BROWN);
  d.fillRect(0,427,468,41,OUT_BROWN);
  badgeFooter(OUT_SAND,OUT_BROWN);outdoorFinish(OUT_SAND);
}

void outdoorSunRays(int cx,int cy,int innerRadius,int outerRadius) {
  auto &d=M5.Display;
  for(int wedge=0;wedge<10;wedge++) {
    float a=(-174+wedge*16.8f)*PI/180,b=(-174+(wedge+1)*16.8f)*PI/180;
    int ax=cx+innerRadius*cosf(a),ay=cy+innerRadius*sinf(a),bx=cx+innerRadius*cosf(b),by=cy+innerRadius*sinf(b);
    int ox=cx+outerRadius*cosf(a),oy=cy+outerRadius*sinf(a),px=cx+outerRadius*cosf(b),py=cy+outerRadius*sinf(b);
    uint16_t fill=wedge%2?OUT_TEAL:OUT_SAND;
    d.fillTriangle(ax,ay,ox,oy,px,py,fill);d.fillTriangle(ax,ay,px,py,bx,by,fill);
    outdoorStroke(ax,ay,ox,oy,4,OUT_BROWN);
  }
}

void outdoorRipple(int baseline,int amplitude,uint16_t color,int weight) {
  int lastX=0,lastY=baseline;
  for(int x=8;x<=472;x+=8) {
    int y=baseline+int(amplitude*sinf(x*2.0f*PI/126.0f));
    outdoorStroke(lastX,lastY,x,y,weight,color);lastX=x;lastY=y;
  }
}

// Tide: a radial sunset above a clean identity band, with a portrait medallion
// and profile window floating over broad, repeating teal-water ripples.
void badgeOutdoorTide() {
  auto &d=M5.Display;d.fillScreen(OUT_BROWN);d.fillCircle(234,234,226,OUT_SAND);
  d.setClipRect(0,0,468,175);
  outdoorSunRays(234,146,72,235);
  d.fillCircle(234,134,71,OUT_BROWN);d.fillCircle(234,134,64,OUT_RUST);
  drawInitWordmark(191,112,86,OUT_SAND);
  d.clearClipRect();
  d.fillRect(0,175,468,64,OUT_SAND);
  outdoorStroke(0,175,468,175,6,OUT_BROWN);
  badgeName(234,195,354,OUT_BROWN,OUT_SAND,&fonts::FreeSerifBold18pt7b);
  badgeIdentity(234,227,330,OUT_BROWN,OUT_SAND,&fonts::FreeSans9pt7b,17);
  d.fillRect(0,243,468,185,OUT_TEAL);d.fillRect(0,239,468,4,OUT_BROWN);
  outdoorRipple(356,10,OUT_BROWN,5);outdoorRipple(376,10,OUT_SAND,7);
  outdoorRipple(396,10,OUT_BROWN,5);outdoorRipple(416,10,OUT_SAND,7);
  outdoorPortrait(130,304,122);
  drawPaletteProfileQr(238,244,146,OUT_BROWN,OUT_SAND);
  d.fillRect(0,428,468,40,OUT_BROWN);
  badgeFooter(OUT_SAND,OUT_BROWN);outdoorFinish();
}
