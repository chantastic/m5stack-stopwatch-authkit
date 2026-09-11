#pragma once
#include <lgfx/utility/lgfx_qrcode.h>

// M5GFX 0.2.26 stores the grid MSB-first. Normalize its public module buffer
// explicitly: the C getter returns a byte mask through a bool declaration,
// which some C++ ABIs assume is already 0 or1.
bool paletteQrDark(const QRCode &code,int column,int row) {
  const int offset=row*int(code.size)+column;
  return ((code.modules[offset>>3]>>(7-(offset&7)))&1U)!=0;
}

// Use the same encoder as M5GFX, with colors that belong to the outdoor badge.
// A fixed buffer bounds stack use; each new version reuses the same bytes.
// Four clear modules surround every edge, with integer-size square modules.
void drawPaletteProfileQr(int left,int top,int size,uint16_t ink,uint16_t paper) {
  if(profileUrl.isEmpty())return;
  if(size<64 || size>468 || profileUrl.length()>240) {drawProfileQr(left,top,size);return;}
  uint8_t modules[512];
  for(uint8_t version=1;version<=10;version++) {
    if(lgfx_qrcode_getBufferSize(version)>sizeof(modules))break;
    QRCode code;
    if(lgfx_qrcode_initText(&code,modules,version,ECC_LOW,profileUrl.c_str())!=0)continue;
    const int scale=size/(int(code.size)+8);
    if(scale<1)break;
    const int offset=(size-int(code.size)*scale)/2;
    auto &d=M5.Display;d.fillRect(left,top,size,size,paper);
    for(int row=0;row<code.size;row++) {
      for(int column=0;column<code.size;) {
        if(!paletteQrDark(code,column,row)) {column++;continue;}
        int start=column;
        while(column<code.size && paletteQrDark(code,column,row))column++;
        d.fillRect(left+offset+start*scale,top+offset+row*scale,(column-start)*scale,scale,ink);
      }
    }
    return;
  }
  drawProfileQr(left,top,size);
}
