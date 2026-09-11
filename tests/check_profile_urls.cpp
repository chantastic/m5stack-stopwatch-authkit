#include <string>
#include <cstdio>
#include <cstdint>
#include <initializer_list>
class String : public std::string {
public:
  using std::string::string;
  String(const std::string &s):std::string(s){}
  bool startsWith(const std::string &prefix) const {return rfind(prefix,0)==0;}
  bool endsWith(const std::string &suffix) const {return size()>=suffix.size() && compare(size()-suffix.size(),suffix.size(),suffix)==0;}
  String substring(size_t from,size_t to=std::string::npos) const {return substr(from,to-from);}
};
enum ProfileProvider : uint8_t { PROFILE_X, PROFILE_LINKEDIN, PROFILE_GITHUB };
ProfileProvider selectedProvider=PROFILE_LINKEDIN;
#include "../firmware/devices_badge/profile_urls.h"

int main() {
  int checks=0;
  auto url=[&](String value,bool expected) {String slug;bool actual=validLinkedInProfileUrl(value,slug);++checks;if(actual!=expected){std::printf("FAIL profile URL case %d\n",checks);return false;}return true;};
  bool okay=true;
  okay &= url("https://www.linkedin.com/in/a/",true);
  okay &= url("https://www.linkedin.com/in/AbC_123-example/",true);
  okay &= url("https://www.linkedin.com/in/"+std::string(100,'a')+"/",true);
  okay &= url("https://www.linkedin.com/in/"+std::string(101,'a')+"/",false);
  const char *bad[]={"", "https://www.linkedin.com/in/", "http://www.linkedin.com/in/a/", "https://linkedin.com/in/a/", "https://www.linkedin.com/in/a", "https://www.linkedin.com/in/a/?q=1", "https://www.linkedin.com/in/a/#x", "https://www.linkedin.com/in/a/b/", "https://www.linkedin.com/in/../", "https://www.linkedin.com/in/a.b/", "https://www.linkedin.com/in/a%2Fb/", "https://www.linkedin.com:443/in/a/", "https://user@www.linkedin.com/in/a/", "https://www.linkedin.com.evil/in/a/", "https://www.linkedin.com/in/a b/", "https://www.linkedin.com/in/é/"};
  for(auto value:bad)okay &= url(value,false);
  auto avatar=[&](String value,bool expected) {++checks;if(allowedAvatarUrl(selectedProvider,value)!=expected){std::printf("FAIL avatar URL case %d\n",checks);return false;}return true;};
  okay &= avatar("https://media.licdn.com/dms/image/v2/test.jpg?e=1&v=beta&t=token",true);
  okay &= avatar("https://media.licdn-ei.com/dms/image/v2/test.png",true);
  const char *badMedia[]={"https://media.licdn.com.evil/image", "https://media.licdn.com:443/image", "https://media.licdn.com@evil/image", "https://evil@media.licdn.com/image", "http://media.licdn.com/image", "https://media.licdn.com/image#fragment", "https://media.licdn.com/ima ge", "https://media.licdn.com/image\\x", "https://pbs.twimg.com/profile_images/test.jpg"};
  for(auto value:badMedia)okay &= avatar(value,false);
  okay &= avatar("https://media.licdn.com/"+std::string(2048,'a'),false);
  selectedProvider=PROFILE_X;
  okay &= avatar("https://pbs.twimg.com/profile_images/test.jpg",true);
  okay &= avatar("https://pbs.twimg.com/profile_images/test.jpg?name=400x400",true);
  okay &= avatar("https://pbs.twimg.com.evil/profile_images/test.jpg",false);
  okay &= avatar("https://media.licdn.com/image.jpg",false);
  auto github=[&](const char *label,bool actual,bool expected) {++checks;if(actual!=expected){std::printf("FAIL GitHub %s case %d\n",label,checks);return false;}return true;};
  for(String value:std::initializer_list<String>{"a","A-Valid-Name","123",std::string(39,'a')})okay &= github("login",validGitHubLogin(value),true);
  for(String value:std::initializer_list<String>{"","-a","a-","a--b","a_b","a.b","a b","é",std::string(40,'a')})okay &= github("login",validGitHubLogin(value),false);
  for(String value:std::initializer_list<String>{"1","123456",std::string(20,'9')})okay &= github("ID",validGitHubId(value),true);
  for(String value:std::initializer_list<String>{"","0","01","-1","1.0","1e3","12/3",std::string(21,'1')})okay &= github("ID",validGitHubId(value),false);
  okay &= github("profile",validGitHubProfileUrl("https://github.com/valid-login","valid-login"),true);
  for(String value:std::initializer_list<String>{"http://github.com/a","https://github.com/a/","https://github.com/a?x=1","https://github.com/a#x","https://github.com/b","https://github.com:443/a","https://user@github.com/a","https://github.com.evil/a"})okay &= github("profile",validGitHubProfileUrl(value,"a"),false);
  selectedProvider=PROFILE_GITHUB;
  okay &= avatar("https://avatars.githubusercontent.com/u/123?s=400&v=4",true);
  for(String value:std::initializer_list<String>{"http://avatars.githubusercontent.com/u/123?s=400&v=4","https://avatars.githubusercontent.com.evil/u/123?s=400&v=4","https://avatars.githubusercontent.com:443/u/123?s=400&v=4","https://user@avatars.githubusercontent.com/u/123?s=400&v=4","https://avatars.githubusercontent.com/u/123","https://avatars.githubusercontent.com/u/0?s=400&v=4","https://avatars.githubusercontent.com/u/01?s=400&v=4","https://avatars.githubusercontent.com/u/123/?s=400&v=4","https://avatars.githubusercontent.com/u/123?s=400&v=4#x","https://avatars.githubusercontent.com/u/123?s=400&v=4&x=1","https://media.licdn.com/image.jpg","https://pbs.twimg.com/profile_images/test.jpg"})okay &= avatar(value,false);
  okay &= github("avatar identity",validGitHubAvatarUrl("https://avatars.githubusercontent.com/u/123?s=400&v=4","124"),false);
  okay &= github("avatar identity",validGitHubAvatarUrl("https://avatars.githubusercontent.com/u/123?s=400&v=4","123"),true);
  okay &= github("unknown provider",allowedAvatarUrl(ProfileProvider(255),"https://media.licdn.com/image.jpg"),false);
  std::printf("%s %d profile/media URL validation cases\n",okay?"PASS":"FAIL",checks);
  return okay?0:1;
}
