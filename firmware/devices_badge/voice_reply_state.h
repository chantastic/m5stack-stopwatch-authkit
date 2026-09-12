#pragma once
#include <stdint.h>

// Reply mode owns its hold gesture separately from the badge's page/chord
// recognizer. Entering a mode with a held button cannot begin a recording.
enum class VoiceHoldAction : uint8_t { NONE, START, STOP, SETTINGS };
class VoiceHoldGesture {
 public:
  static constexpr uint32_t HOLD_MS=250;
  void reset(bool blue=false,bool yellow=false) {
    pending_=recording_=false;suppressed_=blue||yellow;
  }
  VoiceHoldAction update(bool blue,bool yellow,bool eligible,uint32_t now) {
    if(suppressed_) {
      if(!blue&&!yellow)suppressed_=false;
      return VoiceHoldAction::NONE;
    }
    if(blue&&yellow) {
      pending_=recording_=false;suppressed_=true;
      return VoiceHoldAction::SETTINGS;
    }
    if(recording_) {
      if(!blue) {recording_=false;return VoiceHoldAction::STOP;}
      return VoiceHoldAction::NONE;
    }
    if(!eligible || yellow) {
      pending_=false;suppressed_=blue||yellow;
      return VoiceHoldAction::NONE;
    }
    if(!blue) {pending_=false;return VoiceHoldAction::NONE;}
    if(!pending_) {pending_=true;began_=now;}
    if(uint32_t(now-began_)<HOLD_MS)return VoiceHoldAction::NONE;
    pending_=false;recording_=true;return VoiceHoldAction::START;
  }
 private:
  uint32_t began_=0;
  bool pending_=false,recording_=false,suppressed_=false;
};

enum class VoiceSendState : uint8_t { EMPTY, DRAFT, IN_FLIGHT, UNCERTAIN, SENT, FAILED };
enum class VoiceReceipt : uint8_t { PENDING, SENT, UNKNOWN, FAILED, NOT_FOUND };

// This gate does not send HTTP. Only an explicit UI Send action may call
// beginSend. The controller also binds the draft/receipt to the authenticated
// owner, workspace and X sender, and persists the same receipt key before send.
class VoiceSendGate {
 public:
  VoiceSendState state() const {return state_;}
  bool setDraft() {
    if(state_==VoiceSendState::IN_FLIGHT || state_==VoiceSendState::UNCERTAIN)return false;
    state_=VoiceSendState::DRAFT;reviewed_=false;return true;
  }
  void reviewedAllPages() {if(state_==VoiceSendState::DRAFT)reviewed_=true;}
  bool canSend(bool liveAuthorized,bool eligible) const {
    return state_==VoiceSendState::DRAFT && reviewed_ && liveAuthorized && eligible;
  }
  bool beginSend(bool liveAuthorized,bool eligible) {
    if(!canSend(liveAuthorized,eligible))return false;
    state_=VoiceSendState::IN_FLIGHT;return true;
  }
  void transportUncertain() {
    if(state_==VoiceSendState::IN_FLIGHT)state_=VoiceSendState::UNCERTAIN;
  }
  void restorePending() {state_=VoiceSendState::UNCERTAIN;reviewed_=false;}
  void receipt(VoiceReceipt value) {
    if(state_!=VoiceSendState::IN_FLIGHT && state_!=VoiceSendState::UNCERTAIN)return;
    if(value==VoiceReceipt::SENT)state_=VoiceSendState::SENT;
    else if(value==VoiceReceipt::FAILED)state_=VoiceSendState::FAILED;
    else state_=VoiceSendState::UNCERTAIN;
  }
 private:
  VoiceSendState state_=VoiceSendState::EMPTY;
  bool reviewed_=false;
};
