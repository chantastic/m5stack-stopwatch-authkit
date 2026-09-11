#include "../firmware/devices_badge/badge_styles.h"
#include <assert.h>
#include <initializer_list>
#include <stdio.h>

int main() {
  constexpr int count=BADGE_STYLE_COUNT;
  for(int x=0;x<count;x++)for(int linkedin=0;linkedin<count;linkedin++)for(int github=0;github<count;github++) {
    BadgeStyles original;
    assert(original.select(0,x) && original.select(1,linkedin) && original.select(2,github));
    BadgeStyles restored;restored.restore(original.packed());
    for(uint8_t provider=0;provider<3;provider++) {
      assert(restored.style(provider)==original.style(provider));
      assert(restored.design(provider)==provider*count+original.style(provider));
      auto saved=restored.packed();
      // A full cycle returns to the exact per-account choice without affecting
      // either of the other accounts, in both directions.
      for(int direction:{1,-1}) {
        for(int i=0;i<count;i++)assert(restored.step(provider,direction));
        assert(restored.packed()==saved);
      }
      for(int design=-1;design<=3*count;design++) {
        BadgeStyles selected=original;
        bool allowed=design>=provider*count && design<provider*count+count;
        assert(selected.selectDesign(provider,design)==allowed);
        if(allowed)assert(selected.design(provider)==design);
        else assert(selected.packed()==original.packed());
        for(uint8_t other=0;other<3;other++)if(other!=provider)assert(selected.style(other)==original.style(other));
      }
    }
  }
  BadgeStyles corrupt;corrupt.restore(0x0106FF);
  assert(corrupt.style(0)==0 && corrupt.style(1)==0 && corrupt.style(2)==1);
  auto unchanged=corrupt.packed();
  assert(!corrupt.select(3,0) && !corrupt.select(0,-1) && !corrupt.select(0,count));
  assert(!corrupt.step(255,1) && !corrupt.step(0,0) && !corrupt.step(0,2));
  assert(!corrupt.selectDesign(255,0) && corrupt.packed()==unchanged);
  // Unsaved preview changes are held separately from the last explicitly
  // remembered choices, including another account waiting for debounce.
  BadgeStyles preview,saved;
  preview.select(0,2);saved.select(0,preview.style(0));
  preview.select(0,1);preview.select(1,2);
  BadgeStyles reboot;reboot.restore(saved.packed());
  assert(reboot.style(0)==2 && reboot.style(1)==0 && reboot.style(2)==0);
  for(int x=0;x<3;x++)for(int linkedin=0;linkedin<3;linkedin++)for(int github=0;github<3;github++) {
    BadgeStyles legacy;legacy.restore(x|(linkedin<<8)|(github<<16));
    assert(legacy.style(0)==x && legacy.style(1)==linkedin && legacy.style(2)==github);
  }
  printf("All %d per-account combinations, wraps, invalid selections, packed recovery, preview isolation and 27 existing preference combinations passed.\n",count*count*count);
}
