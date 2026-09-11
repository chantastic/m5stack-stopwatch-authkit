#pragma once
// Shared with host tests; String and ProfileProvider are supplied by the caller.
bool profileAsciiAlnum(char c) {
  return (c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9');
}
bool validLinkedInProfileUrl(const String &url,String &slug) {
  static constexpr char prefix[]="https://www.linkedin.com/in/";
  slug="";
  const size_t start=sizeof(prefix)-1;
  if(!url.startsWith(prefix) || !url.endsWith("/") || url.length()<start+2 || url.length()>start+101)return false;
  String candidate=url.substring(start,url.length()-1);
  for(size_t i=0;i<candidate.length();i++)if(!profileAsciiAlnum(candidate[i]) && candidate[i]!='_' && candidate[i]!='-')return false;
  slug=candidate;return true;
}
bool validGitHubLogin(const String &login) {
  if(login.length()<1 || login.length()>39 || login[0]=='-' || login[login.length()-1]=='-')return false;
  for(size_t i=0;i<login.length();i++) {
    if(!profileAsciiAlnum(login[i]) && login[i]!='-')return false;
    if(i && login[i]=='-' && login[i-1]=='-')return false;
  }
  return true;
}
bool validGitHubId(const String &id) {
  if(id.length()<1 || id.length()>20 || id[0]=='0')return false;
  for(size_t i=0;i<id.length();i++)if(id[i]<'0' || id[i]>'9')return false;
  return true;
}
bool validGitHubProfileUrl(const String &url,const String &login) {
  return validGitHubLogin(login) && url=="https://github.com/"+login;
}
bool validGitHubAvatarUrl(const String &url,const String &id) {
  return validGitHubId(id) && url=="https://avatars.githubusercontent.com/u/"+id+"?s=400&v=4";
}
bool allowedAvatarUrl(ProfileProvider provider,const String &url) {
  if(url.length()>2048)return false;
  for(size_t i=0;i<url.length();i++)if(uint8_t(url[i])<=32 || uint8_t(url[i])>=127 || url[i]=='\\' || url[i]=='#')return false;
  if(provider==PROFILE_X)return url.startsWith("https://pbs.twimg.com/profile_images/");
  // Exact HTTPS origins reject credentials, ports and similarly named hosts.
  if(provider==PROFILE_LINKEDIN)return url.startsWith("https://media.licdn.com/") || url.startsWith("https://media.licdn-ei.com/");
  if(provider==PROFILE_GITHUB) {
    static constexpr char prefix[]="https://avatars.githubusercontent.com/u/",suffix[]="?s=400&v=4";
    const size_t start=sizeof(prefix)-1,end=sizeof(suffix)-1;
    if(!url.startsWith(prefix) || !url.endsWith(suffix) || url.length()<=start+end)return false;
    return validGitHubAvatarUrl(url,url.substring(start,url.length()-end));
  }
  return false;
}
