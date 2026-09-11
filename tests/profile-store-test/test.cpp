// Host filesystem/fault fixture for the actual profile_store.h implementation.
// No serial port, Wi-Fi, WorkOS, or physical flash is accessed.
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
using std::min;

class String {
  std::string value;
public:
  String()=default;
  String(const char *text):value(text?text:""){}
  String(const char *text,unsigned int length):value(text,length){}
  String(const std::string &text):value(text){}
  String(uint8_t number):value(std::to_string(number)){}
  size_t length()const{return value.size();}
  bool isEmpty()const{return value.empty();}
  const char *c_str()const{return value.c_str();}
  char operator[](size_t index)const{return value[index];}
  bool startsWith(const String &other)const{return value.rfind(other.value,0)==0;}
  bool endsWith(const String &other)const{return value.size()>=other.value.size() && value.compare(value.size()-other.value.size(),other.value.size(),other.value)==0;}
  String substring(size_t from)const{return value.substr(from);}
  String substring(size_t from,size_t to)const{return value.substr(from,to-from);}
  friend String operator+(const String&a,const String&b){return a.value+b.value;}
  friend bool operator==(const String&a,const String&b){return a.value==b.value;}
  friend bool operator!=(const String&a,const String&b){return !(a==b);}
};
struct Preferences {
  std::map<std::string,uint8_t> values;
  bool denyWrites=false;
  uint8_t getUChar(const char *key,uint8_t fallback=0){auto i=values.find(key);return i==values.end()?fallback:i->second;}
  size_t putUChar(const char *key,uint8_t value){if(denyWrites)return 0;values[key]=value;return 1;}
} settings;
using nvs_handle_t=int;
using esp_err_t=int;
static constexpr int ESP_OK=0,ESP_ERR_NVS_NOT_FOUND=1,ESP_ERR_NVS_TYPE_MISMATCH=2,NVS_READONLY=0;
static std::string corruptNvsKey;
int nvs_open(const char *space,int mode,nvs_handle_t *handle){assert(std::string(space)=="devices" && mode==NVS_READONLY);*handle=1;return ESP_OK;}
int nvs_get_u8(nvs_handle_t,const char *key,uint8_t *value) {
  if(corruptNvsKey==key)return ESP_ERR_NVS_TYPE_MISMATCH;
  auto item=settings.values.find(key);if(item==settings.values.end())return ESP_ERR_NVS_NOT_FOUND;
  *value=item->second;return ESP_OK;
}
void nvs_close(nvs_handle_t){}

static std::string testRoot;
static bool fsFormatted=false,mountFailure=false,renameFailure=false;
static unsigned formatCalls=0,renameCalls=0;
struct FakeLittleFS {
  bool begin(bool format,const char *base,uint8_t files,const char *label) {
    assert(!format && testRoot==base && files==4 && std::string(label)=="ffat");
    return fsFormatted&&!mountFailure;
  }
  bool format() {
    assert(settings.getUChar("pc_fs_init",0)==1);
    formatCalls++;std::filesystem::remove_all(testRoot);std::filesystem::create_directory(testRoot);fsFormatted=true;return true;
  }
  bool rename(const String &from,const String &to) {
    renameCalls++;if(renameFailure)return false;
    return std::rename((testRoot+from.c_str()).c_str(),(testRoot+to.c_str()).c_str())==0;
  }
} LittleFS;
static constexpr int ESP_PARTITION_TYPE_DATA=1,ESP_PARTITION_SUBTYPE_DATA_FAT=0x81;
struct esp_partition_t {uint32_t address=0x610000,size=0x9E0000;};
static esp_partition_t testPartition;
static bool partitionPresent=true;
const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label) {
  assert(type==ESP_PARTITION_TYPE_DATA && subtype==ESP_PARTITION_SUBTYPE_DATA_FAT && std::string(label)=="ffat");
  return partitionPresent?&testPartition:nullptr;
}

static bool allocationFailure=false,shortWrite=false,flushFailure=false,syncFailure=false,closeFailure=false,unlinkFailure=false;
void *ps_malloc(size_t bytes){return allocationFailure?nullptr:malloc(bytes);}
size_t checkedFwrite(const void *data,size_t size,size_t count,FILE *file) {
  if(shortWrite){shortWrite=false;return std::fwrite(data,size,count?count-1:0,file);}
  return std::fwrite(data,size,count,file);
}
int checkedFflush(FILE *file){return flushFailure?EOF:std::fflush(file);}
int checkedFsync(int fd){return syncFailure?-1:fsync(fd);}
int checkedFclose(FILE *file){int result=std::fclose(file);return closeFailure?EOF:result;}
int checkedUnlink(const char *path){if(unlinkFailure){errno=EIO;return -1;}return unlink(path);}

enum ProfileProvider:uint8_t {PROFILE_X=0,PROFILE_LINKEDIN=1,PROFILE_GITHUB=2};
static constexpr char CLIENT_ID[]="client_testDevices";
static constexpr uint16_t TFT_BLACK=0;
struct M5Canvas {
  std::vector<uint8_t> pixels=std::vector<uint8_t>(320000);
  void *getBuffer()const{return const_cast<uint8_t*>(pixels.data());}
  int width()const{return 400;}
  int height()const{return 400;}
  int getColorDepth()const{return 16;}
  void fillScreen(uint16_t color){assert(color==0);std::fill(pixels.begin(),pixels.end(),0);}
};
struct CachedProfile {
  String name,handle,url,avatarUrl,remoteId,owner,workspace,state="unknown";
  bool connected=false,avatarReady=false,saved=false,refreshed=false;
  M5Canvas avatar;
  // Additional fields must never become part of the serialized schema.
  String unrelatedPrivateField="do-not-store-this-test-value";
};

#define BADGE_PROFILE_STORE_ROOT testRoot.c_str()
#define fwrite checkedFwrite
#define fflush checkedFflush
#define fsync checkedFsync
#define fclose checkedFclose
#define unlink checkedUnlink
#include "../../firmware/devices_badge/profile_urls.h"
#include "../../firmware/devices_badge/profile_store.h"
#undef fwrite
#undef fflush
#undef fsync
#undef fclose
#undef unlink
namespace store=badge_profile_store;

void restartRuntime() {
  profileStoreReady=false;profileStoreSavedMask=0;store::mountAttempted=false;store::blockedMask=store::ALL_PROVIDERS;
}
CachedProfile example(ProfileProvider provider) {
  CachedProfile value;value.owner="user_A";value.workspace="org_A";value.name="Public Name";
  value.connected=true;value.avatarReady=true;value.remoteId="12345";
  if(provider==PROFILE_X) {value.handle="@example";value.url="https://x.com/example";value.avatarUrl="https://pbs.twimg.com/profile_images/12345/avatar.jpg";}
  else if(provider==PROFILE_LINKEDIN) {value.handle="example";value.url="https://www.linkedin.com/in/example/";value.avatarUrl="https://media.licdn.com/dms/image/v2/avatar.jpg";}
  else {value.handle="@example";value.url="https://github.com/example";value.avatarUrl="https://avatars.githubusercontent.com/u/12345?s=400&v=4";}
  for(size_t i=0;i<value.avatar.pixels.size();i++)value.avatar.pixels[i]=uint8_t(i*31+provider);
  return value;
}
std::vector<uint8_t> readFile(const String &path) {
  std::ifstream input(path.c_str(),std::ios::binary);return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void writeFile(const String &path,const std::vector<uint8_t> &bytes) {
  std::ofstream output(path.c_str(),std::ios::binary|std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());output.close();assert(output.good());
}
std::string publicFields(const CachedProfile &value) {
  std::string output;
  for(const String *field:{&value.name,&value.handle,&value.url,&value.avatarUrl,&value.remoteId,&value.owner,&value.workspace,&value.state}) {
    output.append(field->c_str(),field->length());output+='\0';
  }
  for(bool flag:{value.connected,value.avatarReady,value.saved,value.refreshed})output+=flag?'1':'0';
  return output;
}
void rejectWithoutMutation(ProfileProvider provider,const String &owner="user_A",const String &org="org_A") {
  CachedProfile target=example(PROFILE_LINKEDIN);target.name="Existing RAM target";target.saved=true;target.refreshed=true;
  std::string fields=publicFields(target);auto pixels=target.avatar.pixels;
  assert(!loadStoredProfile(provider,owner,org,target));
  assert(publicFields(target)==fields && pixels==target.avatar.pixels);
}
store::Metadata metadata(const CachedProfile &value) {
  store::Metadata m;m.client=CLIENT_ID;m.owner=value.owner;m.workspace=value.workspace;m.remoteId=value.remoteId;
  m.name=value.name;m.handle=value.handle;m.url=value.url;m.avatarUrl=value.avatarUrl;m.avatar=value.avatarReady;return m;
}
std::vector<uint8_t> makeRecord(ProfileProvider provider,const store::Metadata &m,const std::vector<uint8_t> &pixels,bool trailingMetadata=false) {
  uint8_t data[store::METADATA_MAX];size_t count=store::encode(m,data);assert(count);
  if(trailingMetadata)data[count++]=0;
  std::vector<uint8_t> result(store::HEADER_BYTES+count+(m.avatar?store::AVATAR_BYTES:0));
  store::header(result.data(),provider,count,m.avatar);memcpy(result.data()+64,data,count);
  if(m.avatar)memcpy(result.data()+64+count,pixels.data(),pixels.size());
  SHA256Builder digest;digest.begin();digest.add(result.data(),32);digest.add(result.data()+64,result.size()-64);
  digest.calculate();digest.getBytes(result.data()+32);return result;
}

int main() {
  char directory[]="/tmp/chan-profile-store-test-XXXXXX";char *created=mkdtemp(directory);assert(created);testRoot=created;
  // Initialization checks: no wrong-partition writes, marker precedes format,
  // and a previously attempted mount never causes a second format.
  testPartition.address++;assert(!mountProfileStore() && formatCalls==0);testPartition.address--;restartRuntime();
  settings.denyWrites=true;assert(!mountProfileStore() && formatCalls==0);settings.denyWrites=false;restartRuntime();
  assert(mountProfileStore() && formatCalls==1 && settings.getUChar("pc_fs_init")==1);
  mountFailure=true;restartRuntime();assert(!mountProfileStore() && formatCalls==1);mountFailure=false;restartRuntime();assert(mountProfileStore());

  for(uint8_t p=0;p<3;p++) {
    auto provider=ProfileProvider(p);auto source=example(provider);assert(saveStoredProfile(provider,source));
    auto bytes=readFile(store::path(provider));assert(bytes.size()<324200 && bytes.size()>320000);
    assert(std::string(bytes.begin(),bytes.end()).find(source.unrelatedPrivateField.c_str())==std::string::npos);
    CachedProfile target;assert(loadStoredProfile(provider,"user_A","org_A",target));
    assert(target.name==source.name && target.handle==source.handle && target.url==source.url && target.remoteId==source.remoteId);
    assert(target.avatar.pixels==source.avatar.pixels && target.saved && !target.refreshed && target.connected && target.avatarReady);
    rejectWithoutMutation(provider,"user_B","org_A");rejectWithoutMutation(provider,"user_A","org_B");
  }
  // Missing revocation state cannot make existing entries usable. Type/read
  // errors are distinct from absent keys and never authorize another format.
  settings.values.erase("pc_block");restartRuntime();assert(mountProfileStore());
  for(uint8_t p=0;p<3;p++)rejectWithoutMutation(ProfileProvider(p));
  assert(saveStoredProfile(PROFILE_X,example(PROFILE_X)) && settings.getUChar("pc_block")==6);
  settings.values["pc_block"]=0;restartRuntime();assert(mountProfileStore());
  corruptNvsKey="pc_fs_init";restartRuntime();assert(!mountProfileStore() && formatCalls==1);
  corruptNvsKey="pc_block";restartRuntime();assert(!mountProfileStore() && formatCalls==1);
  corruptNvsKey="";restartRuntime();assert(mountProfileStore());
  auto source=example(PROFILE_X);auto good=readFile(store::path(PROFILE_X));
  auto writes=profileStoreWrites;auto renames=renameCalls;
  auto skips=profileStoreSkipped;
  assert(saveStoredProfile(PROFILE_X,source) && profileStoreWrites==writes && renameCalls==renames && profileStoreSkipped==skips+1);

  // Corruption and header bounds never partially mutate the live sprite.
  for(size_t offset:{size_t(0),size_t(8),size_t(10),size_t(12),size_t(13),size_t(14),size_t(16),size_t(18),size_t(20),size_t(24),size_t(28),size_t(32),size_t(75),good.size()-1}) {
    auto broken=good;broken[offset]^=0x80;writeFile(store::path(PROFILE_X),broken);rejectWithoutMutation(PROFILE_X);
  }
  for(size_t size:{size_t(0),size_t(63),good.size()-1,good.size()+1}) {
    auto broken=good;broken.resize(size);writeFile(store::path(PROFILE_X),broken);rejectWithoutMutation(PROFILE_X);
  }
  writeFile(store::path(PROFILE_X),good);
  allocationFailure=true;rejectWithoutMutation(PROFILE_X);allocationFailure=false;

  // Signed/checksummed structure is still subject to semantic validation.
  std::vector<store::Metadata> invalid;
  auto m=metadata(source);m.client="client_different";invalid.push_back(m);
  m=metadata(source);m.name=String(std::string(401,'x'));invalid.push_back(m);
  m=metadata(source);m.name=String("bad\0name",8);invalid.push_back(m);
  m=metadata(source);m.name="bad\nname";invalid.push_back(m);
  m=metadata(source);m.url="https://x.com/different";invalid.push_back(m);
  m=metadata(source);m.avatarUrl="https://pbs.twimg.com.attacker.invalid/profile_images/a";invalid.push_back(m);
  m=metadata(source);m.avatarUrl="";invalid.push_back(m);
  m=metadata(source);m.owner="user_bad/context";invalid.push_back(m);
  for(const auto &bad:invalid) {writeFile(store::path(PROFILE_X),makeRecord(PROFILE_X,bad,source.avatar.pixels));rejectWithoutMutation(PROFILE_X);}
  writeFile(store::path(PROFILE_X),makeRecord(PROFILE_X,metadata(source),source.avatar.pixels,true));rejectWithoutMutation(PROFILE_X);
  writeFile(store::path(PROFILE_X),good);
  auto github=example(PROFILE_GITHUB);m=metadata(github);m.avatarUrl="https://avatars.githubusercontent.com/u/999?s=400&v=4";
  writeFile(store::path(PROFILE_GITHUB),makeRecord(PROFILE_GITHUB,m,github.avatar.pixels));rejectWithoutMutation(PROFILE_GITHUB);
  assert(saveStoredProfile(PROFILE_GITHUB,github));

  // Temp-only partial writes and each checked IO failure retain the old file.
  auto changed=source;changed.name="Updated Public Name";
  for(bool *failure:{&shortWrite,&flushFailure,&syncFailure,&closeFailure,&renameFailure}) {
    *failure=true;assert(!saveStoredProfile(PROFILE_X,changed));*failure=false;
    assert(readFile(store::path(PROFILE_X))==good);
    CachedProfile target;assert(loadStoredProfile(PROFILE_X,"user_A","org_A",target) && target.name==source.name);
  }
  assert(saveStoredProfile(PROFILE_X,changed));good=readFile(store::path(PROFILE_X));
  // A stale temp is ignored. Missing/corrupt current files do not deduplicate.
  writeFile(store::path(PROFILE_X,true),{1,2,3});CachedProfile existing;assert(loadStoredProfile(PROFILE_X,"user_A","org_A",existing));
  std::filesystem::remove(store::path(PROFILE_X).c_str());writes=profileStoreWrites;
  assert(saveStoredProfile(PROFILE_X,changed) && profileStoreWrites==writes+1);
  writeFile(store::path(PROFILE_X),{1,2,3});writes=profileStoreWrites;
  assert(saveStoredProfile(PROFILE_X,changed) && profileStoreWrites==writes+1);

  // Transient same-identity image failure cannot replace a usable old photo.
  auto missingPhoto=changed;missingPhoto.avatarReady=false;good=readFile(store::path(PROFILE_X));
  assert(!saveStoredProfile(PROFILE_X,missingPhoto) && readFile(store::path(PROFILE_X))==good);
  // A newly linked identity is blocked before its failed replacement attempt.
  auto replacement=changed;replacement.remoteId="67890";replacement.avatarUrl="https://pbs.twimg.com/profile_images/67890/new.jpg";replacement.avatarReady=false;
  renameFailure=true;assert(!saveStoredProfile(PROFILE_X,replacement));renameFailure=false;
  assert(settings.getUChar("pc_block")&1);restartRuntime();assert(mountProfileStore());rejectWithoutMutation(PROFILE_X);
  assert(saveStoredProfile(PROFILE_X,replacement));CachedProfile noPhoto;assert(loadStoredProfile(PROFILE_X,"user_A","org_A",noPhoto));
  assert(noPhoto.remoteId==replacement.remoteId && !noPhoto.avatarReady && std::all_of(noPhoto.avatar.pixels.begin(),noPhoto.avatar.pixels.end(),[](auto pixel){return pixel==0;}));

  // Explicit provider revocation survives failed deletes and another boot.
  unlinkFailure=true;assert(!removeStoredProfile(PROFILE_GITHUB));unlinkFailure=false;
  assert(settings.getUChar("pc_block")&4);restartRuntime();assert(mountProfileStore());rejectWithoutMutation(PROFILE_GITHUB);
  assert(saveStoredProfile(PROFILE_GITHUB,github) && !(settings.getUChar("pc_block")&4));
  unlinkFailure=true;assert(!clearStoredProfiles());unlinkFailure=false;restartRuntime();assert(mountProfileStore());
  for(uint8_t p=0;p<3;p++)rejectWithoutMutation(ProfileProvider(p));
  assert(saveStoredProfile(PROFILE_X,replacement));
  settings.denyWrites=true;assert(!removeStoredProfile(PROFILE_X) && !profileStoreReady);settings.denyWrites=false;
  // Revocation before a mounted store must still commit a real durable gate.
  settings.values["pc_block"]=0;restartRuntime();assert(!clearStoredProfiles() && settings.getUChar("pc_block")==7);
  restartRuntime();assert(mountProfileStore());assert(clearStoredProfiles());
  auto linkedIn=example(PROFILE_LINKEDIN);linkedIn.url="";linkedIn.handle="";linkedIn.avatarReady=false;linkedIn.avatarUrl="";
  assert(saveStoredProfile(PROFILE_LINKEDIN,linkedIn));CachedProfile setup;assert(loadStoredProfile(PROFILE_LINKEDIN,"user_A","org_A",setup) && setup.url.isEmpty());
  std::filesystem::remove_all(testRoot);
  std::cout<<"Profile store tests passed: ownership, bounded schema, corruption, atomic IO faults, unchanged writes, image replacement, durable revocation, and one-time format. No device access.\n";
}
