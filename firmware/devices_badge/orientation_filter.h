#pragma once
#include <cmath>
#include <cstdint>

// Input is acceleration in g along the unrotated display's right/down axes.
// At rest this vector points up (the support force), opposite gravity.
class OrientationFilter {
 public:
  uint8_t rotation() const { return rotation_; }

  void invalidate() { haveSample_=false;candidate_=-1; }

  bool update(float x,float y,float z,uint32_t now,bool touching) {
    const float norm=x*x+y*y+z*z;
    if(!std::isfinite(norm) || norm<0.49f || norm>1.69f) {
      invalidate();return false;
    }
    // A network operation or missing sensor samples cannot count as settling.
    if(!haveSample_ || uint32_t(now-lastSample_)>250) {
      filteredX_=x;filteredY_=y;candidate_=-1;haveSample_=true;
    } else {
      filteredX_+=0.25f*(x-filteredX_);
      filteredY_+=0.25f*(y-filteredY_);
    }
    lastSample_=now;
    // Keep the last orientation near flat or between two cardinal directions.
    int next=direction(filteredX_,filteredY_);
    // A fresh flat/ambiguous reading cancels immediately, even while the low
    // pass filter still remembers the earlier pose.
    if(touching || next<0 || direction(x,y)!=next || next==rotation_) {
      candidate_=-1;return false;
    }
    if(candidate_!=next) {candidate_=next;candidateSince_=now;return false;}
    if(uint32_t(now-candidateSince_)<700)return false;
    rotation_=uint8_t(next);candidate_=-1;return true;
  }

 private:
  static int direction(float x,float y) {
    const float ax=std::fabs(x),ay=std::fabs(y);
    if(ay>=0.65f && ay>ax*1.35f)return y<0?0:2;
    if(ax>=0.65f && ax>ay*1.35f)return x>0?1:3;
    return -1;
  }
  uint8_t rotation_=0;
  int candidate_=-1;
  bool haveSample_=false;
  float filteredX_=0,filteredY_=0;
  uint32_t lastSample_=0,candidateSince_=0;
};
