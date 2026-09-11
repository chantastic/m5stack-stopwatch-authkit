#include "../firmware/devices_badge/badge_styles.h"
#include <assert.h>
#include <initializer_list>
#include <stdio.h>

int main() {
  static_assert(BADGE_STYLE_COUNT==1,"Only the init() layout should be selectable");
  static_assert(BADGE_STYLE_PROVIDER_COUNT==3,"Keep all three account providers");
  BadgeStyles styles;
  for(uint8_t provider=0;provider<BADGE_STYLE_PROVIDER_COUNT;provider++) {
    assert(styles.style(provider)==0);
    assert(styles.design(provider)==provider);
    assert(styles.select(provider,0));
    for(int direction:{1,-1}) {
      assert(styles.step(provider,direction));
      assert(styles.packed()==0);
      assert(styles.design(provider)==provider);
    }
    for(int design=-1;design<=BADGE_STYLE_PROVIDER_COUNT;design++) {
      BadgeStyles selected;
      assert(selected.selectDesign(provider,design)==(design==provider));
      assert(selected.packed()==0);
      for(uint8_t other=0;other<BADGE_STYLE_PROVIDER_COUNT;other++) {
        assert(selected.style(other)==0);
        assert(selected.design(other)==other);
      }
    }
  }

  // Every choice previously available on each account must recover to init(),
  // including accounts not selected when this firmware first starts.
  int migrated=0;
  for(int x=0;x<6;x++)for(int linkedin=0;linkedin<6;linkedin++)for(int github=0;github<6;github++) {
    BadgeStyles restored;restored.restore(x|(linkedin<<8)|(github<<16));
    assert(restored.packed()==0);
    for(uint8_t provider=0;provider<BADGE_STYLE_PROVIDER_COUNT;provider++) {
      assert(restored.style(provider)==0);
      assert(restored.design(provider)==provider);
    }
    BadgeStyles reboot;reboot.restore(restored.packed());
    assert(reboot.packed()==restored.packed());
    migrated++;
  }
  assert(migrated==216);

  // Unknown bytes also normalize, and no out-of-range selection can recreate
  // a retired layout or change another provider's design.
  for(uint8_t provider=0;provider<BADGE_STYLE_PROVIDER_COUNT;provider++) {
    for(uint32_t value=0;value<=255;value++) {
      BadgeStyles corrupt;corrupt.restore(value<<(provider*8));
      assert(corrupt.packed()==0);
    }
    for(int choice=-1;choice<=6;choice++) {
      assert(styles.select(provider,choice)==(choice==0));
      assert(styles.packed()==0);
    }
  }
  styles.restore(0xFFFFFFFF);
  assert(styles.packed()==0);
  assert(!styles.select(3,0) && !styles.select(255,0));
  assert(!styles.step(255,1) && !styles.step(0,0) && !styles.step(0,2));
  assert(!styles.selectDesign(255,0));
  assert(styles.packed()==0);
  printf("Three init() layouts, reserved style paging, invalid selections, and all %d prior preference combinations passed.\n",migrated);
}
