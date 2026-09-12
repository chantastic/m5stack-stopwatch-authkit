#define VOICE_REPLY_UI_HOST_TEST
#include "../firmware/devices_badge/voice_reply_ui.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using namespace voice_reply_ui;
using Action=VoiceReplyAction;
using Stage=VoiceReplyStage;

static std::string withoutSpaces(const std::string &text) {
  std::string result;
  for(char ch:text)if(ch!=' ' && ch!='\n')result+=ch;
  return result;
}
static std::string collect(const String &text) {
  std::string result;
  auto first=paginate(text,0);
  for(unsigned p=0;p<first.pages;++p) {
    auto page=paginate(text,int(p));
    assert(page.page==p && page.pages==first.pages);
    for(const auto &line:page.lines) {
      assert(strlen(line)<=COLUMNS);
      result+=line;
    }
  }
  return result;
}
static Action hit(const VoiceReplyView &view,Rect rect) {
  return voiceReplyHit(view,rect.x+rect.w/2,rect.y+rect.h/2);
}
int main() {
  assert(paginate("",0).pages==1);
  assert(paginate("",-100).page==0);
  assert(paginate("hello",999).page==0);
  for(unsigned n=1;n<8200;n+=17) {
    String text(std::string(n,'x'));
    auto page=paginate(text,0);
    if(n<=MAX_TEXT_BYTES) {
      assert(page.complete);
      assert(page.pages==(n+COLUMNS*LINES-1)/(COLUMNS*LINES));
      assert(collect(text)==text);
    } else assert(!page.complete);
  }
  String paragraphs;
  for(unsigned i=0;i<200;++i)paragraphs+="A complete sentence stays intact.\n";
  assert(withoutSpaces(collect(paragraphs))==withoutSpaces(paragraphs));
  assert(paginate(paragraphs,99999).page==paginate(paragraphs,0).pages-1);
  assert(collect("caf\xC3\xA9 \xF0\x9F\x8E\xA4!")=="caf[U+00E9] [U+1F3A4]!");
  assert(collect("left\r\nright")=="leftright");
  assert(collect("\xE0\x80\x80")=="[0xE0][0x80][0x80]");
  assert(collect("\xED\xA0\x80")=="[0xED][0xA0][0x80]");
  assert(collect("\xF4\x90\x80\x80")=="[0xF4][0x90][0x80][0x80]");
  assert(collect("\xF0\x9F")=="[0xF0][0x9F]");
  assert(collect("\t\x1B")=="[0x09][0x1B]");
  assert(paginate("emoji \xF0\x9F\x8E\xA4",0).escaped);
  assert(!paginate("plain text",0).escaped);
  // Arbitrary malformed byte strings must still terminate and stay bounded.
  for(unsigned byte=0;byte<256;++byte) {
    String raw; raw+=char(byte);
    auto page=paginate(raw,0);
    assert(page.pages==1 && page.complete);
  }

  VoiceReplyView view;
  for(auto stage:{Stage::LOADING,Stage::INBOX,Stage::RECORDING,Stage::TRANSCRIBING,Stage::REVIEW,Stage::SENDING,Stage::SENT,Stage::ERROR}) {
    view.stage=stage;
    assert(hit(view,BACK)==Action::BACK);
    assert(voiceReplyHit(view,-1,0)==Action::NONE);
    assert(voiceReplyHit(view,467,467)==Action::NONE);
  }
  view.stage=Stage::INBOX;
  assert(hit(view,CENTER)==Action::REFRESH);
  view.count=1;
  assert(hit(view,LEFT)==Action::NONE && hit(view,RIGHT)==Action::NONE);
  view.count=2;
  assert(hit(view,LEFT)==Action::PREVIOUS && hit(view,RIGHT)==Action::NEXT);
  view.mention=std::string(400,'a');
  assert(voiceReplyPageCount(view)==3 && hit(view,PAGER)==Action::MORE);
  view.stage=Stage::RECORDING;
  assert(voiceReplyPageCount(view)==1 && hit(view,PAGER)==Action::NONE);
  assert(hit(view,CENTER)==Action::CANCEL);
  view.stage=Stage::TRANSCRIBING;
  assert(hit(view,CENTER)==Action::CANCEL);
  view.stage=Stage::REVIEW;
  view.transcript="I approved this exact reply.";
  assert(hit(view,RIGHT)==Action::NONE && hit(view,LEFT)==Action::NONE);
  view.canRecord=true;
  assert(hit(view,LEFT)==Action::RECORD_AGAIN);
  view.canSend=true;
  assert(hit(view,RIGHT)==Action::SEND);
  view.transcript="";
  assert(hit(view,RIGHT)==Action::NONE);
  view.transcript=std::string(MAX_TEXT_BYTES+1,'a');
  assert(hit(view,RIGHT)==Action::NONE);
  view.stage=Stage::ERROR; view.uncertain=true;
  assert(hit(view,CENTER)==Action::CHECK_STATUS);
  view.uncertain=false;
  assert(hit(view,CENTER)==Action::REFRESH);
  view.stage=Stage::SENDING;
  assert(hit(view,LEFT)==Action::NONE && hit(view,RIGHT)==Action::NONE && hit(view,CENTER)==Action::NONE);
  view.stage=Stage::SENT;
  assert(hit(view,CENTER)==Action::REFRESH);
  for(Rect rect:{BACK,PAGER,LEFT,RIGHT,CENTER}) {
    for(int x:{rect.x,rect.x+rect.w-1})for(int y:{rect.y,rect.y+rect.h-1}) {
      const int dx=x-234,dy=y-234;
      assert(dx*dx+dy*dy<232*232);
      assert(contains(rect,x,y));
    }
    assert(!contains(rect,rect.x+rect.w,rect.y));
    assert(!contains(rect,rect.x,rect.y+rect.h));
  }
  puts("PASS voice reply UTF-8 review pagination, limits, safe actions, and circular button geometry");
}
