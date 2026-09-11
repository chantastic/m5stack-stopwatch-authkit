#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../firmware/devices_badge/account_paging.h"
int main() {
  // No accounts, one account of either type, then both in LinkedIn/X order.
  assert(accountPageCount(0)==0 && nextAccountPage(0,1,0)==NO_ACCOUNT_PAGE);
  assert(accountPagePosition(0,0)==0);
  for(uint8_t mask:{uint8_t(1),uint8_t(2),uint8_t(4)}) {
    uint8_t only=mask==1?0:mask==2?1:2;
    assert(accountPageCount(mask)==1 && accountPagePosition(only,mask)==1);
    assert(nextAccountPage(only,1,mask)==only && nextAccountPage(only,-1,mask)==only);
    assert(nextAccountPage(NO_ACCOUNT_PAGE,1,mask)==only);
  }
  assert(accountPageCount(3)==2 && accountPagePosition(1,3)==1 && accountPagePosition(0,3)==2);
  assert(nextAccountPage(NO_ACCOUNT_PAGE,1,3)==1);
  assert(nextAccountPage(1,1,3)==0 && nextAccountPage(0,1,3)==1);
  assert(nextAccountPage(1,-1,3)==0 && nextAccountPage(0,-1,3)==1);
  // A disconnected page is skipped even when it was selected before refresh.
  assert(nextAccountPage(1,-1,1)==0 && nextAccountPage(0,1,2)==1);
  // All three in LinkedIn, X, GitHub order; all three two-provider subsets.
  assert(accountPageCount(7)==3 && accountPagePosition(1,7)==1 && accountPagePosition(0,7)==2 && accountPagePosition(2,7)==3);
  assert(nextAccountPage(1,1,7)==0 && nextAccountPage(0,1,7)==2 && nextAccountPage(2,1,7)==1);
  assert(nextAccountPage(1,-1,7)==2 && nextAccountPage(2,-1,7)==0 && nextAccountPage(0,-1,7)==1);
  for(uint8_t mask:{uint8_t(3),uint8_t(5),uint8_t(6)}) {
    assert(accountPageCount(mask)==2);
    uint8_t first=nextAccountPage(NO_ACCOUNT_PAGE,1,mask),second=nextAccountPage(first,1,mask);
    assert(first!=second && nextAccountPage(second,1,mask)==first);
    assert(nextAccountPage(first,-1,mask)==second && nextAccountPage(second,-1,mask)==first);
  }
  assert(accountPageCount(0xF8)==0 && nextAccountPage(2,1,0xF8)==NO_ACCOUNT_PAGE);
  assert(sameAccountContext("user_a","org_a","user_a","org_a"));
  assert(sameAccountContext("user_a","","user_a",""));
  assert(!sameAccountContext("user_a","org_a","user_b","org_a"));
  assert(!sameAccountContext("user_a","org_a","user_a","org_b"));
  assert(!sameAccountContext("","org_a","","org_a"));
  assert(!sameAccountContext(nullptr,"org_a","user_a","org_a"));
  std::puts("PASS zero/one/two/three-account paging, both wraps, disconnected skip, and cache context boundaries");
}
