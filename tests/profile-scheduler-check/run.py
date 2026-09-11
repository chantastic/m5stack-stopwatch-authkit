from pathlib import Path
import hashlib,json,subprocess,os
root=Path(__file__).resolve().parents[2]
fixtures=Path(__file__).resolve().parent
out=root/'.build/profile-scheduler-check'
out.mkdir(parents=True,exist_ok=True)
arduinojson=Path(os.environ.get('ARDUINOJSON_INCLUDE',str(Path.home()/'Documents/Arduino/libraries/ArduinoJson/src')))
if not (arduinojson/'ArduinoJson.h').is_file():
 raise SystemExit('Set ARDUINOJSON_INCLUDE to the ArduinoJson 7.4.3 src directory.')
ino=(root/'firmware/devices_badge/devices_badge.ino').read_text()
profile=(root/'firmware/devices_badge/profile.h').read_text()
def extract(source,name):
 import re
 match=re.search(r'^(?:void|bool|int|uint8_t|BadgeHttpKind) '+name+r'\([^\n]*\)\s*\{',source,re.M)
 if not match:raise RuntimeError(f'Missing function {name}')
 start=source.index('{',match.start());depth=1;i=start+1;quote=None;comment=None
 while depth:
  c=source[i];n=source[i+1:i+2]
  if comment=='line':
   if c=='\n':comment=None
  elif comment=='block':
   if c=='*' and n=='/':comment=None;i+=1
  elif quote:
   if c=='\\':i+=1
   elif c==quote:quote=None
  elif c=='/' and n=='/':comment='line';i+=1
  elif c=='/' and n=='*':comment='block';i+=1
  elif c in ('"',"'"):quote=c
  elif c=='{':depth+=1
  elif c=='}':depth-=1
  i+=1
 return source[match.start():i]
functions=[]
for name in ['resetProfileRefresh','validSavedId','validSavedUser','validOrg','cachedProfileIsReady','availableProfileMask','availableProfileCount','clearActiveProfile','clearProfile','activateCachedProfile','showBestCachedProfile','restoreProfileStore']:
 functions.append((name,extract(profile,name)))
for name in ['acceptSession','beginPairing','pollPairing','renewSession','handleAuthentication']:
 functions.append((name,extract(ino,name)))
for name in ['selectProfileWorkspace','profileRequestKind','refreshCachedProfile','handleProfile']:
 functions.append((name,extract(profile,name)))
setup=extract(ino,'setup')
start=setup.index('  JsonDocument saved;')
end=setup.index('  renderStatus();',start)
boot=setup[start:end]
assert 'restoreProfileStore();' in boot
functions.append(('restoreBootIdentity','void restoreBootIdentity() {\n'+boot+'}\n'))
assert 'authenticated=false' in ino.split('void clearProfile')[0]
generated=(fixtures/'fixture-prefix.cpp').read_text()+'\n\n'.join(body for _,body in functions)+'\n'+(fixtures/'fixture-tests.cpp').read_text()
(out/'generated.cpp').write_text(generated)
metadata={'sources':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in ['firmware/devices_badge/devices_badge.ino','firmware/devices_badge/profile.h','firmware/devices_badge/profile_urls.h','firmware/devices_badge/account_paging.h']},'functions':{name:hashlib.sha256(body.encode()).hexdigest() for name,body in functions}}
(out/'source-manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')
subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17','-fsanitize=address,undefined','-g','-I'+str(root/'tests/background-http-host'),'-I'+str(arduinojson),str(out/'generated.cpp'),'-o',str(out/'check')],check=True)
result=subprocess.run([str(out/'check')],check=True,text=True,capture_output=True)
(out/'verification.txt').write_text(result.stdout)
print(result.stdout,end='')
