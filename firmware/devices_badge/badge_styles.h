#pragma once
#include <stdint.h>

static constexpr uint8_t BADGE_STYLE_COUNT=6;
static constexpr uint8_t BADGE_STYLE_PROVIDER_COUNT=3;

// Storage order follows ProfileProvider: X, LinkedIn, GitHub. A versioned NVS
// key keeps the former nine-design carousel preference separate from this set.
// Append styles so existing per-account values 0–2 retain their meaning.
class BadgeStyles {
  uint8_t choices[BADGE_STYLE_PROVIDER_COUNT]={};
public:
  uint8_t style(uint8_t provider) const {
    return provider<BADGE_STYLE_PROVIDER_COUNT?choices[provider]:0;
  }
  uint8_t design(uint8_t provider) const {
    return provider<BADGE_STYLE_PROVIDER_COUNT?provider*BADGE_STYLE_COUNT+style(provider):0;
  }
  bool select(uint8_t provider,int choice) {
    if(provider>=BADGE_STYLE_PROVIDER_COUNT || choice<0 || choice>=BADGE_STYLE_COUNT)return false;
    choices[provider]=uint8_t(choice);return true;
  }
  bool step(uint8_t provider,int direction) {
    if(provider>=BADGE_STYLE_PROVIDER_COUNT || (direction!=1 && direction!=-1))return false;
    return select(provider,(int(style(provider))+direction+BADGE_STYLE_COUNT)%BADGE_STYLE_COUNT);
  }
  bool selectDesign(uint8_t provider,int index) {
    if(provider>=BADGE_STYLE_PROVIDER_COUNT || index<0 || index>=BADGE_STYLE_PROVIDER_COUNT*BADGE_STYLE_COUNT ||
       index/BADGE_STYLE_COUNT!=provider)return false;
    return select(provider,index%BADGE_STYLE_COUNT);
  }
  uint32_t packed() const {
    return uint32_t(choices[0])|(uint32_t(choices[1])<<8)|(uint32_t(choices[2])<<16);
  }
  void restore(uint32_t saved) {
    for(uint8_t provider=0;provider<BADGE_STYLE_PROVIDER_COUNT;provider++) {
      uint8_t value=(saved>>(provider*8))&0xFF;
      choices[provider]=value<BADGE_STYLE_COUNT?value:0;
    }
  }
};
