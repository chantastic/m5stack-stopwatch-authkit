#pragma once
#include <stdint.h>
#include <string.h>

// Provider bits match the public firmware enum: X=0, LinkedIn=1, GitHub=2.
// Page order remains independent of enum/storage order.
static constexpr uint8_t ACCOUNT_PAGE_ORDER[]={1,0,2};
static constexpr uint8_t ACCOUNT_PROVIDER_COUNT=sizeof(ACCOUNT_PAGE_ORDER);
static constexpr uint8_t NO_ACCOUNT_PAGE=255;
inline uint8_t accountPageCount(uint8_t connectedMask) {
  uint8_t count=0;
  for(uint8_t provider:ACCOUNT_PAGE_ORDER)if(connectedMask&(1U<<provider))++count;
  return count;
}
inline uint8_t accountPagePosition(uint8_t provider,uint8_t connectedMask) {
  uint8_t page=0;
  for(uint8_t candidate:ACCOUNT_PAGE_ORDER)if(connectedMask&(1U<<candidate)) {
    ++page;if(candidate==provider)return page;
  }
  return 0;
}
inline uint8_t nextAccountPage(uint8_t current,int step,uint8_t connectedMask) {
  uint8_t available[ACCOUNT_PROVIDER_COUNT],count=0;
  for(uint8_t provider:ACCOUNT_PAGE_ORDER)if(connectedMask&(1U<<provider))available[count++]=provider;
  if(!count)return NO_ACCOUNT_PAGE;
  for(uint8_t i=0;i<count;i++)if(available[i]==current)return available[(int(i)+step+count)%count];
  return available[0];
}
inline bool sameAccountContext(const char *cachedUser,const char *cachedOrg,const char *user,const char *org) {
  return cachedUser && cachedOrg && user && org && user[0] && cachedUser[0] &&
    !strcmp(cachedUser,user) && !strcmp(cachedOrg,org);
}
