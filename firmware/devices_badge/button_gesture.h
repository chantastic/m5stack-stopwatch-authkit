#pragma once
#include <stdint.h>

enum class BadgeButtonAction : uint8_t { NONE, YELLOW, BLUE, SETTINGS };

// One action per gesture. Give the second pusher a short chance to form a
// chord before committing a single, then require both pushers to release.
class BadgeButtonGesture {
 public:
  static constexpr uint32_t CHORD_GRACE_MS=125;
  void reset(bool yellow=false,bool blue=false) {pending_=false;suppressed_=yellow||blue;}
  BadgeButtonAction update(bool yellow,bool blue,uint32_t now) {
    const uint8_t held=(yellow?1:0)|(blue?2:0);
    if(suppressed_) {
      if(!held)suppressed_=false;
      return BadgeButtonAction::NONE;
    }
    if(!pending_) {
      if(!held)return BadgeButtonAction::NONE;
      first_=held;started_=now;pending_=true;
    }
    if(held==3) {
      pending_=false;suppressed_=true;
      return BadgeButtonAction::SETTINGS;
    }
    if(!(held&first_) || uint32_t(now-started_)>=CHORD_GRACE_MS) {
      pending_=false;suppressed_=held!=0;
      return first_==1?BadgeButtonAction::YELLOW:BadgeButtonAction::BLUE;
    }
    return BadgeButtonAction::NONE;
  }
 private:
  uint32_t started_=0;
  uint8_t first_=0;
  bool pending_=false,suppressed_=false;
};
