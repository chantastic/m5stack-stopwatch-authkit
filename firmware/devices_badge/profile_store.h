#pragma once
// Public badge cache only. The caller supplies the previously verified owner
// context and owns authentication; this file never reads or writes credentials.
#include <LittleFS.h>
#include <SHA2Builder.h>
#include <esp_partition.h>
#include <nvs.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <utility>

// A host-only filesystem fixture may override the mount point.
#ifndef BADGE_PROFILE_STORE_ROOT
#define BADGE_PROFILE_STORE_ROOT "/badgefs"
#endif

bool profileStoreReady=false;
uint32_t profileStoreLoads=0,profileStoreWrites=0,profileStoreSkipped=0,profileStoreErrors=0;
uint8_t profileStoreSavedMask=0;

namespace badge_profile_store {
static constexpr size_t HEADER_BYTES=64,METADATA_MAX=4096,AVATAR_BYTES=400*400*2;
static constexpr uint8_t ALL_PROVIDERS=7;
static constexpr char INIT_KEY[]="pc_fs_init",BLOCK_KEY[]="pc_block";
static constexpr uint8_t MAGIC[8]={'C','H','D','V','P','C','0','1'};
static bool mountAttempted=false;
static uint8_t blockedMask=ALL_PROVIDERS;

struct Metadata {
  String client,owner,workspace,remoteId,name,handle,url,avatarUrl;
  bool avatar=false;
};
struct Record {
  Metadata meta;
  uint8_t digest[32]={};
  uint8_t *pixels=nullptr;
  ~Record() {if(pixels)free(pixels);}
};
struct OpenFile {
  FILE *file=nullptr;
  explicit OpenFile(const String &path,const char *mode):file(fopen(path.c_str(),mode)){}
  ~OpenFile() {if(file)fclose(file);}
  bool close() {if(!file)return false;FILE *closing=file;file=nullptr;return fclose(closing)==0;}
};

bool fail() {profileStoreErrors++;return false;}
bool readFlag(const char *key,uint8_t &value,bool &missing) {
  // Preferences getters collapse absent keys and IO/type errors to one
  // default. Only an explicit NOT_FOUND is an uninitialized cache flag.
  nvs_handle_t handle;missing=false;
  if(nvs_open("devices",NVS_READONLY,&handle)!=ESP_OK)return false;
  esp_err_t result=nvs_get_u8(handle,key,&value);nvs_close(handle);
  if(result==ESP_ERR_NVS_NOT_FOUND) {missing=true;return true;}
  return result==ESP_OK;
}
bool writeFlag(const char *key,uint8_t value) {
  if(settings.putUChar(key,value)!=1)return false;
  uint8_t stored=0;bool missing=false;
  return readFlag(key,stored,missing) && !missing && stored==value;
}
bool providerValid(ProfileProvider provider) {return uint8_t(provider)<3;}
String path(ProfileProvider provider,bool temporary=false) {
  return String(BADGE_PROFILE_STORE_ROOT)+"/profile-"+String(uint8_t(provider))+(temporary?".tmp":".bin");
}
String relativePath(ProfileProvider provider,bool temporary=false) {
  return String("/profile-")+String(uint8_t(provider))+(temporary?".tmp":".bin");
}
uint16_t u16(const uint8_t *p) {return uint16_t(p[0])|(uint16_t(p[1])<<8);}
uint32_t u32(const uint8_t *p) {return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
void put16(uint8_t *p,uint16_t value) {p[0]=value;p[1]=value>>8;}
void put32(uint8_t *p,uint32_t value) {for(unsigned i=0;i<4;i++)p[i]=value>>(i*8);}
bool identity(const String &value,const char *prefix) {
  size_t start=strlen(prefix);
  if(!value.startsWith(prefix) || value.length()<=start || value.length()>80)return false;
  for(size_t i=start;i<value.length();i++)if(!profileAsciiAlnum(value[i]))return false;
  return true;
}
bool textValid(const String &value,size_t maximum,bool optional=false) {
  if((!optional && value.isEmpty()) || value.length()>maximum)return false;
  for(size_t i=0;i<value.length();i++)if(uint8_t(value[i])<32 || uint8_t(value[i])==127)return false;
  return true;
}
bool valid(ProfileProvider provider,const Metadata &m) {
  if(!providerValid(provider) || m.client!=CLIENT_ID || !identity(m.owner,"user_") || !identity(m.workspace,"org_") ||
      !textValid(m.remoteId,256) || !textValid(m.name,400) || !textValid(m.handle,100,true) ||
      !textValid(m.url,256,true) || !textValid(m.avatarUrl,2048,true) ||
      (m.avatar && m.avatarUrl.isEmpty()) || (!m.avatarUrl.isEmpty() && !allowedAvatarUrl(provider,m.avatarUrl)))return false;
  if(provider==PROFILE_X) {
    if(!m.handle.startsWith("@") || m.handle.length()<2 || m.handle.length()>16 || m.url!="https://x.com/"+m.handle.substring(1))return false;
    for(size_t i=1;i<m.handle.length();i++)if(!profileAsciiAlnum(m.handle[i]) && m.handle[i]!='_')return false;
  } else if(provider==PROFILE_LINKEDIN) {
    if(m.url.isEmpty())return m.handle.isEmpty();
    String slug;if(!validLinkedInProfileUrl(m.url,slug) || m.handle!=slug)return false;
  } else if(provider==PROFILE_GITHUB) {
    if(!validGitHubId(m.remoteId) || !m.handle.startsWith("@") || !validGitHubProfileUrl(m.url,m.handle.substring(1)) ||
        (!m.avatarUrl.isEmpty() && !validGitHubAvatarUrl(m.avatarUrl,m.remoteId)))return false;
  }
  return true;
}
// Fixed ordered fields have no unknown keys, optional trailing records, or
// arbitrary JSON blobs. Lengths count UTF-8 bytes, not characters.
size_t encode(const Metadata &m,uint8_t *bytes) {
  const String *fields[]={&m.client,&m.owner,&m.workspace,&m.remoteId,&m.name,&m.handle,&m.url,&m.avatarUrl};
  size_t length=0;
  for(const String *field:fields) {
    if(field->length()>METADATA_MAX || length+2+field->length()>METADATA_MAX)return 0;
    put16(bytes+length,field->length());length+=2;
    memcpy(bytes+length,field->c_str(),field->length());length+=field->length();
  }
  return length;
}
bool decode(const uint8_t *bytes,size_t length,Metadata &m) {
  String *fields[]={&m.client,&m.owner,&m.workspace,&m.remoteId,&m.name,&m.handle,&m.url,&m.avatarUrl};
  size_t offset=0;
  for(String *field:fields) {
    if(offset+2>length)return false;
    size_t count=u16(bytes+offset);offset+=2;
    if(count>length-offset || memchr(bytes+offset,0,count))return false;
    *field=String(reinterpret_cast<const char*>(bytes+offset),count);
    if(field->length()!=count)return false;
    offset+=count;
  }
  return offset==length;
}
void header(uint8_t *bytes,ProfileProvider provider,size_t metadataLength,bool avatar) {
  memset(bytes,0,HEADER_BYTES);memcpy(bytes,MAGIC,sizeof(MAGIC));
  put16(bytes+8,1);put16(bytes+10,HEADER_BYTES);bytes[12]=uint8_t(provider);bytes[13]=avatar?1:0;
  // Format1: native M5GFX 16-bit sprite bytes, rotation0, 400px row stride.
  // Bump the format/version before changing sprite layout or byte ordering.
  put16(bytes+14,1);put16(bytes+16,400);put16(bytes+18,400);
  put32(bytes+20,metadataLength);put32(bytes+24,avatar?AVATAR_BYTES:0);
}
bool readRecord(const String &filename,ProfileProvider provider,const String &owner,const String &workspace,
                Record &record,bool withPixels) {
  OpenFile input(filename,"rb");if(!input.file)return false;
  struct stat info;
  if(fstat(fileno(input.file),&info)!=0 || !S_ISREG(info.st_mode) || info.st_size<int64_t(HEADER_BYTES) ||
      info.st_size>int64_t(HEADER_BYTES+METADATA_MAX+AVATAR_BYTES))return false;
  uint8_t head[HEADER_BYTES],metadata[METADATA_MAX];
  if(fread(head,1,sizeof(head),input.file)!=sizeof(head) || memcmp(head,MAGIC,sizeof(MAGIC)) ||
      u16(head+8)!=1 || u16(head+10)!=HEADER_BYTES || head[12]!=uint8_t(provider) || head[13]>1 ||
      u16(head+14)!=1 || u16(head+16)!=400 || u16(head+18)!=400 || u32(head+28)!=0)return false;
  size_t metadataLength=u32(head+20),pixelLength=u32(head+24);
  if(!metadataLength || metadataLength>METADATA_MAX || pixelLength!=(head[13]?AVATAR_BYTES:0) ||
      uint64_t(info.st_size)!=HEADER_BYTES+metadataLength+pixelLength)return false;
  record.meta.avatar=head[13]!=0;
  if(fread(metadata,1,metadataLength,input.file)!=metadataLength || !decode(metadata,metadataLength,record.meta) ||
      !valid(provider,record.meta) || record.meta.owner!=owner || record.meta.workspace!=workspace)return false;
  SHA256Builder digest;digest.begin();digest.add(head,32);digest.add(metadata,metadataLength);
  if(withPixels && pixelLength) {record.pixels=(uint8_t*)ps_malloc(pixelLength);if(!record.pixels)return false;}
  uint8_t block[2048];
  for(size_t offset=0;offset<pixelLength;) {
    size_t count=min(sizeof(block),pixelLength-offset);
    uint8_t *destination=record.pixels?record.pixels+offset:block;
    if(fread(destination,1,count,input.file)!=count)return false;
    digest.add(destination,count);offset+=count;
  }
  if(ferror(input.file) || !input.close())return false;
  digest.calculate();digest.getBytes(record.digest);
  return !memcmp(record.digest,head+32,32);
}
bool setBlocked(uint8_t value) {
  // Consult NVS even before a successful mount; a RAM default is not proof of
  // a durable invalidation gate (notably after failed first initialization).
  uint8_t current=0;bool missing=false;
  bool readable=readFlag(BLOCK_KEY,current,missing);
  if(!readable || ((missing || current!=value) && !writeFlag(BLOCK_KEY,value))) {
    // Caller must invalidate its trusted offline context before revocation.
    // Do not use any file again in this boot if that durable gate failed.
    profileStoreReady=false;profileStoreSavedMask=0;return fail();
  }
  blockedMask=value;profileStoreSavedMask&=~value;return true;
}
bool removeFile(ProfileProvider provider,bool temporary) {
  String filename=path(provider,temporary);
  return unlink(filename.c_str())==0 || errno==ENOENT;
}
} // namespace badge_profile_store

bool mountProfileStore() {
  using namespace badge_profile_store;
  if(mountAttempted)return profileStoreReady;
  mountAttempted=true;
  // Frozen existing layout: never search a default partition or touch NVS,
  // either app slot, coredump, or any partition with unexpected dimensions.
  const esp_partition_t *partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_FAT,"ffat");
  if(!partition || partition->address!=0x610000 || partition->size!=0x9E0000)return fail();
  uint8_t initialized=0;bool initialMissing=false;
  if(!readFlag(INIT_KEY,initialized,initialMissing) || (!initialMissing && initialized!=1))return fail();
  bool mounted=LittleFS.begin(false,BADGE_PROFILE_STORE_ROOT,4,"ffat");
  if(!mounted) {
    // Record the attempt before formatting. Interrupted initialization may
    // disable the disposable cache, but never triggers repeated erasure.
    if(!initialMissing || !writeFlag(INIT_KEY,1))return fail();
    if(!LittleFS.format() || !LittleFS.begin(false,BADGE_PROFILE_STORE_ROOT,4,"ffat"))return fail();
  } else if(initialMissing && !writeFlag(INIT_KEY,1))return fail();
  bool blockMissing=false;
  if(!readFlag(BLOCK_KEY,blockedMask,blockMissing))return fail();
  if(blockMissing)blockedMask=ALL_PROVIDERS;
  if(blockedMask&~ALL_PROVIDERS)return fail();
  profileStoreReady=true;return true;
}

bool loadStoredProfile(ProfileProvider provider,const String &owner,const String &workspace,CachedProfile &target) {
  using namespace badge_profile_store;
  if(!profileStoreReady || !providerValid(provider))return false;
  uint8_t bit=1U<<uint8_t(provider);profileStoreSavedMask&=~bit;
  if((blockedMask&bit) || !identity(owner,"user_") || !identity(workspace,"org_"))return false;
  Record record;
  if(!readRecord(path(provider),provider,owner,workspace,record,true))return false;
  if(!target.avatar.getBuffer() || target.avatar.width()!=400 || target.avatar.height()!=400 || target.avatar.getColorDepth()!=16)return fail();
  String connectedState="connected";if(connectedState.length()!=9)return fail();
  // No target field or pixel changes until the entire record is validated.
  if(record.meta.avatar)memcpy(target.avatar.getBuffer(),record.pixels,AVATAR_BYTES);
  else target.avatar.fillScreen(TFT_BLACK);
  auto &m=record.meta;
  target.name=std::move(m.name);target.handle=std::move(m.handle);target.url=std::move(m.url);
  target.avatarUrl=std::move(m.avatarUrl);target.remoteId=std::move(m.remoteId);
  target.owner=std::move(m.owner);target.workspace=std::move(m.workspace);
  target.avatarReady=m.avatar;target.connected=true;target.state=std::move(connectedState);
  target.saved=true;target.refreshed=false;
  profileStoreSavedMask|=bit;profileStoreLoads++;return true;
}

bool saveStoredProfile(ProfileProvider provider,const CachedProfile &source) {
  using namespace badge_profile_store;
  if(!profileStoreReady || !providerValid(provider))return false;
  Metadata m;
  m.client=CLIENT_ID;m.owner=source.owner;m.workspace=source.workspace;m.remoteId=source.remoteId;
  m.name=source.name;m.handle=source.handle;m.url=source.url;m.avatarUrl=source.avatarUrl;m.avatar=source.avatarReady;
  if(!source.connected || !valid(provider,m) || (m.avatar && (!source.avatar.getBuffer() ||
      source.avatar.width()!=400 || source.avatar.height()!=400 || source.avatar.getColorDepth()!=16)))return fail();
  uint8_t head[HEADER_BYTES],metadata[METADATA_MAX];size_t metadataLength=encode(m,metadata);
  if(!metadataLength)return fail();
  header(head,provider,metadataLength,m.avatar);
  SHA256Builder digest;digest.begin();digest.add(head,32);digest.add(metadata,metadataLength);
  if(m.avatar)digest.add((const uint8_t*)source.avatar.getBuffer(),AVATAR_BYTES);
  digest.calculate();digest.getBytes(head+32);
  uint8_t bit=1U<<uint8_t(provider);
  Record previous;
  bool previousValid=readRecord(path(provider),provider,m.owner,m.workspace,previous,false);
  if(previousValid && previous.meta.remoteId!=m.remoteId && !setBlocked(blockedMask|bit))return false;
  // A failed image refresh must not erase the last image of the same identity.
  // A changed remote ID or avatar URL may legitimately commit without pixels.
  if(previousValid && previous.meta.remoteId==m.remoteId && previous.meta.avatarUrl==m.avatarUrl &&
      previous.meta.avatar && !m.avatar)return fail();
  if(previousValid && !memcmp(previous.digest,head+32,32)) {
    if(!setBlocked(blockedMask&~bit))return false;
    profileStoreSavedMask|=bit;profileStoreSkipped++;return true;
  }
  OpenFile output(path(provider,true),"wb");if(!output.file)return fail();
  bool written=fwrite(head,1,sizeof(head),output.file)==sizeof(head) &&
    fwrite(metadata,1,metadataLength,output.file)==metadataLength;
  if(written && m.avatar)written=fwrite(source.avatar.getBuffer(),1,AVATAR_BYTES,output.file)==AVATAR_BYTES;
  if(written)written=!ferror(output.file) && fflush(output.file)==0 && fsync(fileno(output.file))==0;
  bool closed=output.close();
  Record verified;
  if(!written || !closed || !readRecord(path(provider,true),provider,m.owner,m.workspace,verified,false) ||
      memcmp(verified.digest,head+32,32)) {removeFile(provider,true);return fail();}
  // LittleFS rename atomically replaces the old file. Never delete it first.
  if(!LittleFS.rename(relativePath(provider,true),relativePath(provider))) {removeFile(provider,true);return fail();}
  if(!setBlocked(blockedMask&~bit))return false;
  profileStoreSavedMask|=bit;profileStoreWrites++;return true;
}

bool removeStoredProfile(ProfileProvider provider) {
  using namespace badge_profile_store;
  if(!providerValid(provider))return false;
  uint8_t bit=1U<<uint8_t(provider);profileStoreSavedMask&=~bit;
  // The durable block precedes deletion, including when mounting failed.
  // A later authenticated save is the only way to make this provider usable.
  if(!setBlocked(blockedMask|bit))return false;
  if(!profileStoreReady)return false;
  bool current=removeFile(provider,false),temporary=removeFile(provider,true);
  return current&&temporary?true:fail();
}
bool clearStoredProfiles() {
  using namespace badge_profile_store;
  profileStoreSavedMask=0;
  if(!setBlocked(ALL_PROVIDERS))return false;
  if(!profileStoreReady)return false;
  bool okay=true;
  for(uint8_t provider=0;provider<3;provider++) {
    if(!removeFile(ProfileProvider(provider),false))okay=false;
    if(!removeFile(ProfileProvider(provider),true))okay=false;
  }
  return okay?true:fail();
}
