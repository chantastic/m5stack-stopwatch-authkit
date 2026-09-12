#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Presentation only: the controller owns recording, authorization, page-review
// coverage, request cancellation, and the single deliberate Send action.
enum class VoiceReplyStage : uint8_t {
  LOADING, INBOX, RECORDING, TRANSCRIBING, REVIEW, SENDING, SENT, ERROR
};
struct VoiceReplyView {
  String author, mention, transcript, message, progress;
  uint8_t index=0, count=0;
  uint32_t recordingMs=0;
  int textPage=0;
  bool canRecord=false, canSend=false, uncertain=false;
  VoiceReplyStage stage=VoiceReplyStage::LOADING;
};
enum class VoiceReplyAction : uint8_t {
  NONE, BACK, PREVIOUS, NEXT, MORE, RECORD_AGAIN, CANCEL, SEND, REFRESH, CHECK_STATUS
};

namespace voice_reply_ui {
constexpr unsigned COLUMNS=30, LINES=6, MAX_TEXT_BYTES=8192;
struct TextPage {
  char lines[LINES][COLUMNS+1]{};
  unsigned pages=1, page=0;
  bool escaped=false, complete=true;
};

// Existing FreeMono fonts contain ASCII only. Show unsupported code points
// explicitly rather than silently dropping an emoji, accent, or control byte
// while the user reviews the actual text that will be sent. This does not alter
// the controller's original UTF-8 transcript. Invalid bytes remain visible too.
inline unsigned nextToken(const String &text, size_t &offset, char token[16], bool &escaped) {
  const size_t start=offset;
  const uint8_t lead=uint8_t(text[offset++]);
  if((lead>=32 && lead<=126) || lead=='\n') {
    token[0]=char(lead); token[1]=0; return 1;
  }
  if(lead=='\r' && offset<text.length() && text[offset]=='\n') {
    ++offset; token[0]='\n'; token[1]=0; return 1;
  }
  unsigned length=lead>=0xC2 && lead<=0xDF?2:lead>=0xE0 && lead<=0xEF?3:lead>=0xF0 && lead<=0xF4?4:0;
  uint32_t point=length?lead & ((1u<<(7-length))-1):lead;
  bool valid=length && start+length<=text.length();
  for(unsigned i=1;valid && i<length;++i) {
    uint8_t byte=uint8_t(text[start+i]);
    if((byte&0xC0)!=0x80)valid=false;
    else point=(point<<6)|(byte&0x3F);
  }
  if(valid && ((length==3 && point<0x800) || (length==4 && point<0x10000) ||
    point>0x10FFFF || (point>=0xD800 && point<=0xDFFF))) valid=false;
  escaped=true;
  if(valid) {
    offset=start+length;
    return unsigned(snprintf(token,16,"[U+%04lX]",static_cast<unsigned long>(point)));
  }
  return unsigned(snprintf(token,16,"[0x%02X]",unsigned(lead)));
}

inline TextPage scanText(const String &text, unsigned requestedPage) {
  TextPage result; result.page=requestedPage;
  char line[COLUMNS+1]{};
  unsigned used=0, lineCount=0;
  auto emit=[&](unsigned count) {
    if(lineCount/LINES==requestedPage) {
      memcpy(result.lines[lineCount%LINES],line,count);
      result.lines[lineCount%LINES][count]=0;
    }
    ++lineCount;
  };
  size_t offset=0;
  while(offset<text.length()) {
    char token[16]{};
    unsigned tokenLength=nextToken(text,offset,token,result.escaped);
    for(unsigned i=0;i<tokenLength;++i) {
      char ch=token[i];
      if(ch=='\n') { emit(used); used=0; continue; }
      if(used==COLUMNS) {
        int space=-1;
        // Break at a word when possible, retaining every non-space character.
        for(unsigned j=0;j<used;++j)if(line[j]==' ')space=int(j);
        if(space>0) {
          emit(unsigned(space));
          unsigned rest=used-unsigned(space)-1;
          memmove(line,line+space+1,rest); used=rest;
        } else { emit(used); used=0; }
        if(ch==' ' && !used)continue;
      }
      line[used++]=ch;
    }
  }
  if(used || !lineCount)emit(used);
  result.pages=(lineCount+LINES-1)/LINES;
  return result;
}

inline TextPage paginate(const String &text, int requestedPage) {
  const bool complete=text.length()<=MAX_TEXT_BYTES;
  const String overflow="This text exceeds the device's review limit. Record a shorter reply.";
  const String &source=complete?text:overflow;
  unsigned page=requestedPage>0?unsigned(requestedPage):0;
  TextPage result=scanText(source,page);
  if(page>=result.pages)result=scanText(source,result.pages-1);
  result.complete=complete;
  return result;
}

inline String body(const VoiceReplyView &view) {
  if(view.stage==VoiceReplyStage::REVIEW)return view.transcript;
  if(view.stage==VoiceReplyStage::INBOX && view.count)return view.mention;
  if(!view.message.isEmpty())return view.message;
  switch(view.stage) {
    case VoiceReplyStage::LOADING:return "Checking your connected X account for mentions...";
    case VoiceReplyStage::INBOX:return "No mentions to reply to yet. Check again when someone mentions your account.";
    case VoiceReplyStage::TRANSCRIBING:return "Turning your recording into text. You can review it before choosing Send.";
    case VoiceReplyStage::SENDING:return "Sending your approved reply. Please wait for its status.";
    case VoiceReplyStage::SENT:return "Your reply was sent.";
    case VoiceReplyStage::ERROR:return view.uncertain?"The reply may have been sent. Check its status before doing anything else.":"This action could not finish. Check your connection and try again.";
    default:return "";
  }
}

struct Rect { int x,y,w,h; };
constexpr Rect BACK{80,66,76,36}, PAGER{252,323,140,38};
constexpr Rect LEFT{104,375,124,42}, RIGHT{240,375,124,42}, CENTER{139,375,190,42};
inline bool contains(Rect r,int x,int y) {return x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h;}
inline bool hasPages(const VoiceReplyView &v) {return v.stage!=VoiceReplyStage::RECORDING;}
}

inline unsigned voiceReplyPageCount(const VoiceReplyView &view) {
  return voice_reply_ui::hasPages(view)?voice_reply_ui::paginate(voice_reply_ui::body(view),view.textPage).pages:1;
}
inline VoiceReplyAction voiceReplyHit(const VoiceReplyView &view,int x,int y) {
  using namespace voice_reply_ui;
  if(contains(BACK,x,y))return VoiceReplyAction::BACK;
  const TextPage page=paginate(body(view),view.textPage);
  if(hasPages(view) && page.pages>1 && contains(PAGER,x,y))return VoiceReplyAction::MORE;
  switch(view.stage) {
    case VoiceReplyStage::INBOX:
      if(!view.count && contains(CENTER,x,y))return VoiceReplyAction::REFRESH;
      if(view.count>1 && contains(LEFT,x,y))return VoiceReplyAction::PREVIOUS;
      if(view.count>1 && contains(RIGHT,x,y))return VoiceReplyAction::NEXT;
      break;
    case VoiceReplyStage::RECORDING:
    case VoiceReplyStage::TRANSCRIBING:
      if(contains(CENTER,x,y))return VoiceReplyAction::CANCEL;
      break;
    case VoiceReplyStage::REVIEW:
      if(view.canRecord && contains(LEFT,x,y))return VoiceReplyAction::RECORD_AGAIN;
      if(view.canSend && page.complete && !view.transcript.isEmpty() && contains(RIGHT,x,y))return VoiceReplyAction::SEND;
      break;
    case VoiceReplyStage::ERROR:
      if(contains(CENTER,x,y))return view.uncertain?VoiceReplyAction::CHECK_STATUS:VoiceReplyAction::REFRESH;
      break;
    case VoiceReplyStage::SENT:
      if(contains(CENTER,x,y))return VoiceReplyAction::REFRESH;
      break;
    default:break; // Loading/sending never offers an action that can duplicate a post.
  }
  return VoiceReplyAction::NONE;
}

#ifndef VOICE_REPLY_UI_HOST_TEST
namespace voice_reply_ui {
constexpr uint16_t BG=0x0841, FG=0xF79D, MUTED=0x9493, PANEL=0x18C3, LINE=0x39C7;
constexpr uint16_t BLUE=0x7D9F, VOICE_RED=0xF30C, VOICE_GREEN=0xA715;
inline void label(const String &text,int x,int y,uint16_t color=FG,bool bold=false) {
  auto &d=M5.Display; d.setTextSize(1);
  d.setFont(bold?&fonts::FreeSansBold9pt7b:&fonts::FreeSans9pt7b);
  d.setTextDatum(middle_center); d.setTextColor(color); d.drawString(text,x,y);
}
inline void button(Rect r,const char *text,bool enabled=true,bool primary=false) {
  auto &d=M5.Display;
  uint16_t fill=enabled && primary?FG:PANEL;
  d.fillRoundRect(r.x,r.y,r.w,r.h,9,fill);
  d.drawRoundRect(r.x,r.y,r.w,r.h,9,enabled?MUTED:LINE);
  label(text,r.x+r.w/2,r.y+r.h/2,!enabled?MUTED:primary?BG:FG,true);
}
inline void tiny(const String &text,int x,int y,uint16_t color=MUTED) {
  auto &d=M5.Display; d.setFont(&fonts::Font0); d.setTextSize(1);
  d.setTextDatum(middle_center); d.setTextColor(color); d.drawString(text,x,y);
}
inline void pageText(const TextPage &page) {
  auto &d=M5.Display; d.setFont(&fonts::FreeMono9pt7b); d.setTextSize(1);
  d.setTextDatum(top_left); d.setTextColor(FG);
  for(unsigned i=0;i<LINES;++i)d.drawString(page.lines[i],69,168+24*i);
  char count[32]; snprintf(count,sizeof(count),"PAGE %u / %u",page.page+1,page.pages);
  tiny(count,page.pages>1?153:234,342);
  if(page.pages>1)button(PAGER,page.page+1<page.pages?"Next page":"Page 1");
}
}

inline void drawVoiceReply(const VoiceReplyView &view) {
  using namespace voice_reply_ui;
  auto &d=M5.Display; d.startWrite(); d.fillScreen(BG);
  drawInitWordmark(186,29,96,FG);
  button(BACK,"Back");
  d.setFont(&fonts::FreeMonoBold12pt7b); d.setTextSize(1);
  d.setTextDatum(middle_center); d.setTextColor(FG); d.drawString("X REPLIES",278,84);
  d.drawFastHLine(76,113,316,LINE);

  if(view.stage==VoiceReplyStage::RECORDING) {
    label("Recording",234,138,VOICE_RED,true);
    d.drawCircle(234,229,66,LINE); d.fillCircle(234,190,7,VOICE_RED);
    char duration[16]; snprintf(duration,sizeof(duration),"%lu:%02lu",static_cast<unsigned long>(view.recordingMs/60000),static_cast<unsigned long>((view.recordingMs/1000)%60));
    d.setFont(&fonts::FreeMonoBold24pt7b); d.setTextDatum(middle_center); d.setTextColor(FG); d.drawString(duration,234,239);
    d.fillRoundRect(99,310,270,5,2,LINE);
    unsigned elapsed=view.recordingMs>30000?30000:view.recordingMs;
    if(elapsed)d.fillRoundRect(99,310,int(elapsed*270/30000),5,2,VOICE_RED);
    label("Release blue to transcribe",234,343,FG);
    button(CENTER,"Cancel recording");
    tiny("30 SECOND LIMIT",234,436);
    d.endWrite(); return;
  }

  const TextPage page=paginate(body(view),view.textPage);
  String title;
  uint16_t titleColor=FG;
  switch(view.stage) {
    case VoiceReplyStage::LOADING:title="Checking mentions";break;
    case VoiceReplyStage::INBOX:title=view.count?view.author:"Your mentions";break;
    case VoiceReplyStage::TRANSCRIBING:title="Transcribing...";break;
    case VoiceReplyStage::REVIEW:title="Review your reply";break;
    case VoiceReplyStage::SENDING:title="Sending...";break;
    case VoiceReplyStage::SENT:title="Reply sent";titleColor=VOICE_GREEN;break;
    case VoiceReplyStage::ERROR:title=view.uncertain?"Send status unknown":"Unable to continue";titleColor=VOICE_RED;break;
    default:break;
  }
  d.setFont(&fonts::FreeSansBold9pt7b); d.setTextSize(1);
  // Only the heading is abbreviated. The full body is always paged, never fit.
  label(badgeFitText(title,322),234,139,titleColor,true);
  if(view.stage==VoiceReplyStage::REVIEW && !view.author.isEmpty()) {
    d.setFont(&fonts::Font0);
    tiny(badgeFitText(String("TO ")+view.author,264),234,154);
  }
  pageText(page);
  String footer=view.progress;
  switch(view.stage) {
    case VoiceReplyStage::INBOX:
      if(view.count>1) {button(LEFT,"< Previous");button(RIGHT,"Next >");}
      else if(!view.count)button(CENTER,"Refresh");
      if(view.count && footer.isEmpty())footer=view.canRecord?"HOLD BLUE TO REPLY":"RECORDING UNAVAILABLE";
      break;
    case VoiceReplyStage::TRANSCRIBING:button(CENTER,"Cancel");break;
    case VoiceReplyStage::REVIEW:
      button(LEFT,"Re-record",view.canRecord);
      button(RIGHT,"Send",view.canSend && page.complete && !view.transcript.isEmpty(),true);
      if(footer.isEmpty())footer=view.canSend?"SEND ONLY WHEN READY":"REVIEW EVERY PAGE";
      break;
    case VoiceReplyStage::ERROR:button(CENTER,view.uncertain?"Check status":"Try again");break;
    case VoiceReplyStage::SENT:button(CENTER,"Back to inbox");break;
    default:break;
  }
  if(page.escaped && footer.isEmpty())footer="UNICODE SHOWN AS [U+....]";
  d.setFont(&fonts::Font0); d.setTextSize(1);
  tiny(badgeFitText(footer,198),234,436);
  if(view.stage==VoiceReplyStage::INBOX && view.count) {
    char position[20]; snprintf(position,sizeof(position),"%u / %u",unsigned(view.index)+1,unsigned(view.count));
    tiny(position,234,153);
  }
  d.setTextSize(1); d.setTextDatum(middle_center); d.endWrite();
}
#endif
