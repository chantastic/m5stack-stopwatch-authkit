#include "../firmware/devices_badge/voice_reply_state.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main() {
  VoiceHoldGesture hold;
  assert(hold.update(true,false,true,100)==VoiceHoldAction::NONE);
  assert(hold.update(false,false,true,200)==VoiceHoldAction::NONE);
  assert(hold.update(true,false,true,300)==VoiceHoldAction::NONE);
  assert(hold.update(true,false,true,549)==VoiceHoldAction::NONE);
  assert(hold.update(true,false,true,550)==VoiceHoldAction::START);
  assert(hold.update(true,false,true,700)==VoiceHoldAction::NONE);
  assert(hold.update(false,false,true,701)==VoiceHoldAction::STOP);
  assert(hold.update(false,false,true,702)==VoiceHoldAction::NONE);
  hold.reset(true,false);
  assert(hold.update(true,false,true,1000)==VoiceHoldAction::NONE);
  assert(hold.update(false,false,true,1001)==VoiceHoldAction::NONE);
  assert(hold.update(true,true,true,1002)==VoiceHoldAction::SETTINGS);
  assert(hold.update(true,true,true,1500)==VoiceHoldAction::NONE);
  assert(hold.update(false,false,true,1501)==VoiceHoldAction::NONE);
  assert(hold.update(true,false,false,1600)==VoiceHoldAction::NONE);
  assert(hold.update(true,false,true,1900)==VoiceHoldAction::NONE);
  hold.reset();
  assert(hold.update(true,false,true,UINT32_MAX-100)==VoiceHoldAction::NONE);
  assert(hold.update(true,false,true,150)==VoiceHoldAction::START);
  assert(hold.update(true,true,true,151)==VoiceHoldAction::SETTINGS);

  VoiceSendGate send;
  assert(!send.beginSend(true,true));
  assert(send.setDraft());
  assert(!send.beginSend(true,true));
  send.reviewedAllPages();
  assert(!send.beginSend(false,true) && !send.beginSend(true,false));
  assert(send.beginSend(true,true));
  assert(!send.beginSend(true,true) && !send.setDraft());
  send.transportUncertain();
  for(auto receipt:{VoiceReceipt::PENDING,VoiceReceipt::UNKNOWN,VoiceReceipt::NOT_FOUND}) {
    send.receipt(receipt);
    assert(send.state()==VoiceSendState::UNCERTAIN);
    assert(!send.beginSend(true,true) && !send.setDraft());
  }
  send.receipt(VoiceReceipt::SENT);
  assert(send.state()==VoiceSendState::SENT && !send.beginSend(true,true));
  assert(send.setDraft());
  assert(!send.beginSend(true,true));
  send.reviewedAllPages();assert(send.beginSend(true,true));
  send.receipt(VoiceReceipt::FAILED);
  assert(send.state()==VoiceSendState::FAILED && !send.beginSend(true,true));
  assert(send.setDraft());send.restorePending();
  assert(!send.canSend(true,true) && !send.setDraft());
  send.receipt(VoiceReceipt::NOT_FOUND);
  assert(send.state()==VoiceSendState::UNCERTAIN);
  std::puts("Voice hold/chord isolation, deliberate reviewed send, and no replay after uncertain receipt passed.");
}
