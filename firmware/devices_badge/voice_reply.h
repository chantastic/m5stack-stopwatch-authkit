#pragma once

// Included after profile.h: this controller is main-task owned. Audio and HTTPS
// have their own workers; no credentials, transcripts or PCM are logged here.
static constexpr char VOICE_ORIGIN[]="https://devices.chan.dev";
static constexpr char VOICE_INBOX_URL[]="https://devices.chan.dev/v1/x/replies";
static constexpr char VOICE_TRANSCRIPTION_URL[]="https://devices.chan.dev/v1/transcriptions";
static constexpr char VOICE_DEEPGRAM_URL[]="https://devices.chan.dev/v1/transcriptions/deepgram";
static constexpr char VOICE_SEND_URL[]="https://devices.chan.dev/v1/x/replies";
static constexpr char VOICE_STATUS_URL[]="https://devices.chan.dev/v1/x/replies/status";
VoiceRecorder voiceRecorder;
VoiceHoldGesture voiceHold;
VoiceSendGate voiceSendGate;
VoiceReplyView voiceView;
struct VoiceMention {String id,text,author;};
VoiceMention voiceMentions[5];
String voiceOwner,voiceWorkspace,voiceSender,voiceConnection;
String voiceKey,voicePendingOwner,voicePendingWorkspace,voicePendingTarget,voicePendingSender,voicePendingConnection;
String voiceRequestBearer,voiceRequestBody;
BadgeHttpKind voiceRequest=BadgeHttpKind::NONE;
bool voiceRequestStarted=false,voiceConfigured=false,voiceXai=false,voiceDeepgram=false,voiceUseDeepgram=false;
bool voiceFallbackOffered=false,voiceStorageBlocked=false,voiceCaptureExpected=false;
bool voicePostingAllowed=false;
bool voiceDraftValid=false;
uint8_t *voiceWav=nullptr;
size_t voiceWavBytes=0;
uint32_t voiceRecordRender=0,voiceReviewedThrough=0,voiceDiagnosticStop=0;
bool voiceDiagnosticLocal=false;
bool voiceSendTouchArmed=false;

void voiceWipe(String &value) {for(size_t i=0;i<value.length();++i)value.setCharAt(i,0);value="";}
void voiceFreeClip() {
  if(voiceWav) {volatile uint8_t *p=voiceWav;for(size_t i=0;i<voiceWavBytes;++i)p[i]=0;free(voiceWav);}
  voiceWav=nullptr;voiceWavBytes=0;
}
bool voiceLive() {
  return authenticated && !accessToken.isEmpty() && time(nullptr)<accessExpires &&
    WiFi.status()==WL_CONNECTED && !portalActive && !currentUserId.isEmpty() && !currentOrgId.isEmpty();
}
bool voiceSameContext() {return voiceOwner==currentUserId && voiceWorkspace==currentOrgId && !voiceOwner.isEmpty();}
bool voicePendingMatches() {
  return !voiceKey.isEmpty() && voicePendingOwner==currentUserId && voicePendingWorkspace==currentOrgId;
}
bool voiceUnresolved() {
  return voiceStorageBlocked || !voiceKey.isEmpty() || voiceSendGate.state()==VoiceSendState::IN_FLIGHT ||
    voiceSendGate.state()==VoiceSendState::UNCERTAIN;
}
bool voiceCanRecord() {
  return voiceConfigured && voiceLive() && voiceSameContext() && !voiceUnresolved() &&
    (voiceUseDeepgram?voiceDeepgram:voiceXai) && !voiceRecorder.busy() && !badgeHttp.busy() &&
    voiceRequest==BadgeHttpKind::NONE && !profileRefreshActive;
}
void voiceStage(VoiceReplyStage stage,const String &message="") {
  voiceSendTouchArmed=false;
  voiceView.stage=stage;voiceView.message=message;voiceView.textPage=0;voiceView.progress="";
  voiceView.canSend=false;voiceView.uncertain=false;redraw=true;
}
void voiceError(const String &message,bool uncertain=false) {
  voiceStage(VoiceReplyStage::ERROR,message);voiceView.uncertain=uncertain;
}
void voiceClearInbox() {
  for(auto &item:voiceMentions) {voiceWipe(item.id);voiceWipe(item.text);voiceWipe(item.author);}
  voiceView.count=voiceView.index=0;voiceView.author="";voiceWipe(voiceView.mention);
  voiceSender="";voiceConnection="";
  voicePostingAllowed=false;
}
void voiceShowMention() {
  voiceWipe(voiceView.transcript);voiceFallbackOffered=false;voiceDraftValid=false;
  voiceStage(VoiceReplyStage::INBOX);
  if(voiceView.count) {
    const auto &item=voiceMentions[voiceView.index];voiceView.author=item.author;voiceView.mention=item.text;
  } else {voiceView.author="";voiceView.mention="";}
}
bool voicePersistPending() {
  JsonDocument record;record["v"]=1;record["client"]=CLIENT_ID;record["key"]=voiceKey;
  record["owner"]=voicePendingOwner;record["workspace"]=voicePendingWorkspace;
  record["target"]=voicePendingTarget;record["sender"]=voicePendingSender;record["connection"]=voicePendingConnection;
  const size_t expected=measureJson(record);
  String json;
  if(record.overflowed() || !expected || expected>2048 || serializeJson(record,json)!=expected || json.length()!=expected)return false;
  return settings.putString("voice_pending",json)==json.length() && settings.getString("voice_pending","")==json;
}
bool voiceForgetPending() {
  if(settings.isKey("voice_pending") && !settings.remove("voice_pending")) {
    voiceStorageBlocked=true;return false;
  }
  voiceKey="";voicePendingOwner="";voicePendingWorkspace="";voicePendingTarget="";
  voicePendingSender="";voicePendingConnection="";return true;
}
bool voiceId(const String &value,size_t limit=128) {
  if(value.isEmpty() || value.length()>limit)return false;
  for(size_t i=0;i<value.length();++i)if(!isalnum(uint8_t(value[i])) && value[i]!='_' && value[i]!='-')return false;
  return true;
}
void setupVoiceReply() {
  BadgeVoiceEndpointConfig config;
  config.origin=VOICE_ORIGIN;config.inboxUrl=VOICE_INBOX_URL;config.transcriptionUrl=VOICE_TRANSCRIPTION_URL;
  config.replyUrl=VOICE_SEND_URL;config.ca=ROOT_CA;config.statusUrl=VOICE_STATUS_URL;
  config.deepgramTranscriptionUrl=VOICE_DEEPGRAM_URL;
  voiceConfigured=badgeHttp.configureVoiceEndpoints(config) && voiceRecorder.begin();
  if(settings.isKey("voice_pending")) {
    JsonDocument saved;String record=settings.getString("voice_pending","");
    voiceStorageBlocked=record.length()>2048 || deserializeJson(saved,record) || (saved["v"]|0)!=1 ||
      (saved["client"]|String(""))!=CLIENT_ID;
    if(!voiceStorageBlocked) {
      voiceKey=saved["key"]|"";voicePendingOwner=saved["owner"]|"";voicePendingWorkspace=saved["workspace"]|"";
      voicePendingTarget=saved["target"]|"";voicePendingSender=saved["sender"]|"";voicePendingConnection=saved["connection"]|"";
      voiceStorageBlocked=!voiceId(voiceKey) || !voiceId(voicePendingOwner) || !voiceId(voicePendingWorkspace) ||
        !voiceId(voicePendingTarget,32) || !voiceId(voicePendingSender,32) || !voiceId(voicePendingConnection);
    }
    voiceSendGate.restorePending();
  }
}
void voiceQueue(BadgeHttpKind kind) {
  voiceRequest=kind;voiceRequestStarted=false;voiceWipe(voiceRequestBearer);
}
void voiceRefreshInbox() {
  if(voiceUnresolved()) {voiceError("A previous send needs its status checked before another reply.",true);return;}
  if(!voiceConfigured) {voiceError("The recording service could not start. Return to the badge and restart the device.");return;}
  if(!voiceLive()) {voiceError("Connect Wi-Fi and AuthKit in Settings, then return here.");return;}
  voiceClearInbox();voiceOwner=currentUserId;voiceWorkspace=currentOrgId;
  voiceXai=voiceDeepgram=false;voiceUseDeepgram=false;
  voiceStage(VoiceReplyStage::LOADING);voiceQueue(BadgeHttpKind::REPLY_INBOX);
}
void enterVoiceReplies() {
  stopPortal();screen=REPLIES;showBadge=false;expanded=false;
  voiceHold.reset(M5.BtnB.isPressed(),M5.BtnA.isPressed());
  if(voiceStorageBlocked)voiceError("The saved send receipt needs recovery. No new reply will be sent.",true);
  else if(voiceUnresolved())voiceError(voicePendingMatches()?"A previous reply may have been sent. Check its status.":
    "A previous reply belongs to another sign-in. Reconnect that account to check its status.",true);
  else voiceRefreshInbox();
}
void voiceCancelRequest() {
  if(voiceRequestStarted && badgeHttp.kind()==voiceRequest)badgeHttp.cancel();
  if(voiceRequest==BadgeHttpKind::REPLY_SEND) {
    if(voiceRequestStarted)voiceSendGate.transportUncertain();
    else {voiceSendGate.receipt(VoiceReceipt::FAILED);voiceForgetPending();}
  }
  voiceRequest=BadgeHttpKind::NONE;voiceRequestStarted=false;voiceWipe(voiceRequestBearer);voiceWipe(voiceRequestBody);
  voiceFreeClip();
}
void leaveVoiceReplies(bool toSettings) {
  voiceCaptureExpected=false;voiceRecorder.cancel();voiceCancelRequest();
  voiceDiagnosticStop=0;voiceDiagnosticLocal=false;voiceWipe(voiceView.transcript);voiceClearInbox();
  voiceOwner="";voiceWorkspace="";voiceHold.reset(true,true);
  buttonGesture.reset(M5.BtnA.isPressed(),M5.BtnB.isPressed());
  screen=toSettings?SETTINGS:BADGE;showBadge=!toSettings;expanded=false;redraw=true;
}
bool voiceBlocksOrientation() {
  return screen==REPLIES && (M5.BtnA.isPressed() || M5.BtnB.isPressed() ||
    voiceView.stage==VoiceReplyStage::RECORDING || voiceView.stage==VoiceReplyStage::REVIEW ||
    voiceView.stage==VoiceReplyStage::SENDING);
}
void voiceStartRecording() {
  if(!voiceCanRecord())return;
  voiceWipe(voiceView.transcript);voiceReviewedThrough=0;voiceDraftValid=false;
  if(!voiceRecorder.start()) {voiceError("Microphone is unavailable. Please try again.");return;}
  voiceCaptureExpected=true;voiceStage(VoiceReplyStage::RECORDING);voiceRecordRender=0;
}
void handleVoiceButtons() {
  bool eligible=(voiceView.stage==VoiceReplyStage::INBOX || voiceView.stage==VoiceReplyStage::REVIEW) && voiceCanRecord();
  VoiceHoldAction action=voiceHold.update(M5.BtnB.isPressed(),M5.BtnA.isPressed(),eligible,millis());
  if(action==VoiceHoldAction::SETTINGS)leaveVoiceReplies(true);
  else if(action==VoiceHoldAction::START)voiceStartRecording();
  else if(action==VoiceHoldAction::STOP)voiceRecorder.stop();
}
void voiceBeginSend() {
  bool eligible=voicePostingAllowed && voiceDraftValid && voiceView.count && voiceId(voiceSender,32) && voiceId(voiceConnection) &&
    voiceSameContext() && !voiceUnresolved();
  if(!voiceSendGate.canSend(voiceLive(),eligible))return;
  // Bound the actual serialized bytes before recording a pending intent. JSON
  // escapes can make a valid-size transcript much larger than its raw text.
  JsonDocument body;body["targetId"]=voiceMentions[voiceView.index].id;body["text"]=voiceView.transcript;
  body["senderId"]=voiceSender;body["connectionId"]=voiceConnection;
  const size_t expected=measureJson(body);String encoded;
  if(body.overflowed() || !expected || expected>4096 || serializeJson(body,encoded)!=expected || encoded.length()!=expected) {
    voiceError("This reply is too large to submit. Make a shorter recording.");return;
  }
  voiceWipe(voiceRequestBody);voiceRequestBody=encoded;
  if(voiceRequestBody!=encoded) {voiceWipe(voiceRequestBody);voiceError("Not enough memory to prepare this reply. Please try again.");return;}
  uint8_t random[16];esp_fill_random(random,sizeof(random));char key[33];
  for(size_t i=0;i<16;i++)snprintf(key+i*2,3,"%02x",random[i]);
  voiceKey=key;voicePendingOwner=voiceOwner;voicePendingWorkspace=voiceWorkspace;
  voicePendingTarget=voiceMentions[voiceView.index].id;voicePendingSender=voiceSender;voicePendingConnection=voiceConnection;
  if(!voicePersistPending()) {voiceStorageBlocked=true;voiceSendGate.restorePending();voiceError("Could not save the send receipt. Nothing was submitted. Device storage needs recovery.",true);return;}
  if(!voiceSendGate.beginSend(voiceLive(),eligible)) {voiceError("Sign-in changed before sending. Check the saved receipt.",true);return;}
  voiceStage(VoiceReplyStage::SENDING);voiceQueue(BadgeHttpKind::REPLY_SEND);
}
void beginVoiceTouch(int x,int y) {
  // A release that began while recording/transcribing must never activate a
  // newly appeared Send button. Require a fresh contact on an enabled Send.
  voiceSendTouchArmed=voiceView.stage==VoiceReplyStage::REVIEW && voiceView.canSend &&
    voiceReplyHit(voiceView,x,y)==VoiceReplyAction::SEND;
}
void handleVoiceTap(int x,int y) {
  VoiceReplyAction action=voiceReplyHit(voiceView,x,y);
  const bool freshSend=voiceSendTouchArmed;voiceSendTouchArmed=false;
  if(action==VoiceReplyAction::BACK) {leaveVoiceReplies(false);return;}
  if(action==VoiceReplyAction::MORE) {
    voiceView.textPage=(voiceView.textPage+1)%voiceReplyPageCount(voiceView);redraw=true;return;
  }
  if(action==VoiceReplyAction::CANCEL) {
    voiceCaptureExpected=false;voiceRecorder.cancel();voiceCancelRequest();voiceShowMention();return;
  }
  if(action==VoiceReplyAction::SEND) {if(freshSend)voiceBeginSend();return;}
  if(action==VoiceReplyAction::CHECK_STATUS) {
    if(voiceStorageBlocked || !voicePendingMatches() || !voiceLive()) {
      voiceError("Reconnect the original account and Wi-Fi to check this receipt. Saved send data is retained.",true);return;
    }
    voiceOwner=currentUserId;voiceWorkspace=currentOrgId;
    voiceStage(VoiceReplyStage::SENDING,"Checking the existing receipt. This does not send another reply.");
    voiceQueue(BadgeHttpKind::REPLY_STATUS);return;
  }
  if(voiceRequest!=BadgeHttpKind::NONE || voiceRecorder.busy() || voiceUnresolved())return;
  if(action==VoiceReplyAction::PREVIOUS || action==VoiceReplyAction::NEXT) {
    if(voiceView.count)voiceView.index=(voiceView.index+voiceView.count+(action==VoiceReplyAction::NEXT?1:-1))%voiceView.count;
    voiceShowMention();
  } else if(action==VoiceReplyAction::RECORD_AGAIN)voiceShowMention();
  else if(action==VoiceReplyAction::REFRESH) {
    if(voiceFallbackOffered) {voiceUseDeepgram=true;voiceShowMention();voiceView.progress="HOLD BLUE: DEEPGRAM";}
    else voiceRefreshInbox();
  }
}
bool voiceResponseContext(const JsonDocument &r) {
  return (r["context"]["userId"]|String(""))==voiceOwner &&
    (r["context"]["organizationId"]|String(""))==voiceWorkspace && voiceSameContext();
}
void voiceReceiptResult(int code,const JsonDocument &r) {
  // Only a matching receipt from the trusted gateway can unlock another send.
  bool matches=(r["key"]|String(""))==voiceKey && (r["targetId"]|String(""))==voicePendingTarget &&
    (r["senderId"]|String(""))==voicePendingSender && voicePendingMatches();
  String state=r["state"]|"";
  if(matches && state=="sent" && code>=200 && code<300) {
    voiceSendGate.receipt(VoiceReceipt::SENT);voiceForgetPending();voiceWipe(voiceView.transcript);
    voiceStage(VoiceReplyStage::SENT,"Your reply was sent. Return to the inbox when ready.");
  } else if(matches && state=="failed" && code>0) {
    voiceSendGate.receipt(VoiceReceipt::FAILED);voiceForgetPending();
    voiceError(r["message"]|String("The reply was not sent. Return to the inbox and try again."));
  } else {
    voiceSendGate.transportUncertain();voiceSendGate.receipt(VoiceReceipt::UNKNOWN);
    voiceError(code==401?"Sign in again to check this receipt. The reply may have been sent.":
      "The reply's result is unresolved. Check status; no automatic resend will occur.",true);
  }
}
void voiceConsume(BadgeHttpKind kind,int code,const JsonDocument &r) {
  if(kind==BadgeHttpKind::REPLY_SEND || kind==BadgeHttpKind::REPLY_STATUS) {voiceReceiptResult(code,r);return;}
  if(!voiceResponseContext(r)) {
    // A transport/proxy failure has no authenticated JSON context. It can offer
    // a fresh, explicitly chosen recording, but can never supply a transcript.
    if(kind==BadgeHttpKind::TRANSCRIPTION && (code<0 || code>=500) && voiceSameContext() && !voiceUseDeepgram && voiceDeepgram) {
      voiceFallbackOffered=true;
      voiceError("Transcription did not finish. Tap Try again, then hold blue to make a new recording with Deepgram.");return;
    }
    voiceError(code==401?"Your sign-in needs renewal. Return to Settings, then try again.":"The service response could not be verified. Please refresh.");return;
  }
  if(kind==BadgeHttpKind::REPLY_INBOX) {
    voicePostingAllowed=r["canSend"].is<bool>() && r["canSend"].as<bool>();
    voiceXai=r["transcription"]["xai"]|false;voiceDeepgram=r["transcription"]["deepgram"]|false;
    voiceUseDeepgram=!voiceXai && voiceDeepgram;
    if(code!=200 || (r["state"]|String(""))!="ready") {
      voiceShowMention();voiceView.message=r["message"]|String("X mentions are unavailable. Check your X connection.");
      if(voiceXai || voiceDeepgram)voiceView.message+=" You can still hold blue to try dictation; it cannot be sent without a mention.";
      return;
    }
    voiceSender=r["sender"]["id"]|"";voiceConnection=r["sender"]["connectionId"]|"";
    JsonArrayConst items=r["items"].as<JsonArrayConst>();
    if(!voiceId(voiceSender,32) || !voiceId(voiceConnection) || items.isNull() || items.size()>5) {
      voiceClearInbox();voiceError("The inbox response was not valid. Please refresh.");return;
    }
    for(JsonObjectConst item:items) {
      String id=item["id"]|"",text=item["text"]|"",handle=item["author"]["username"]|"";
      if(!voiceId(id,32) || text.isEmpty() || text.length()>1000 || !voiceId(handle,32)) {
        voiceClearInbox();voiceError("A mention exceeded the device's limits. Please refresh.");return;
      }
      auto &dest=voiceMentions[voiceView.count++];dest.id=id;dest.text=text;dest.author="@"+handle;
    }
    voiceShowMention();
  } else if(kind==BadgeHttpKind::TRANSCRIPTION) {
    String transcript=r["text"]|"";
    if(code==200 && (r["state"]|String(""))=="transcribed" && !transcript.isEmpty() && transcript.length()<=3000) {
      if(!voiceSendGate.setDraft()) {voiceError("A previous send must be resolved first.",true);return;}
      voiceView.transcript=transcript;voiceReviewedThrough=0;
      voiceDraftValid=r["replyValid"].is<bool>() && r["replyValid"].as<bool>();
      voiceStage(VoiceReplyStage::REVIEW);
      if(!voiceView.count)voiceView.author="Dictation only";
    } else {
      voiceFallbackOffered=!voiceUseDeepgram && voiceDeepgram;
      voiceError(voiceFallbackOffered?"xAI could not transcribe this recording. Tap Try again, then hold blue to make a new recording with Deepgram.":
        (r["message"]|String("Transcription did not finish. Make a shorter recording and try again.")));
    }
  }
}
void handleVoiceReply() {
  if(screen!=REPLIES)return;
  const bool canRecord=voiceCanRecord();
  const bool canSend=voiceView.stage==VoiceReplyStage::REVIEW &&
    voiceSendGate.canSend(voiceLive() && voiceSameContext(),voicePostingAllowed && voiceDraftValid && voiceView.count && !voiceUnresolved());
  if(canRecord!=voiceView.canRecord || canSend!=voiceView.canSend)redraw=true;
  if(!voiceSameContext() && (!voiceOwner.isEmpty() || voiceCaptureExpected || voiceRequest!=BadgeHttpKind::NONE)) {
    voiceCaptureExpected=false;voiceRecorder.cancel();voiceCancelRequest();voiceClearInbox();voiceWipe(voiceView.transcript);
    voiceOwner="";voiceWorkspace="";voiceError("Your sign-in changed. Return to Settings and reconnect.",voiceUnresolved());return;
  }
  if(voiceCaptureExpected) {
    if(!voiceDiagnosticLocal && !voiceLive()) {
      voiceCaptureExpected=false;voiceRecorder.cancel();voiceError("Wi-Fi or sign-in changed. Recording cancelled; reconnect before trying again.");return;
    }
    if(voiceDiagnosticStop && due(voiceDiagnosticStop)) {voiceRecorder.stop();voiceDiagnosticStop=0;}
    if(millis()-voiceRecordRender>=250) {voiceRecordRender=millis();voiceView.recordingMs=voiceRecorder.durationMs();redraw=true;}
    uint32_t duration=0;
    if(voiceRecorder.take(voiceWav,voiceWavBytes,duration)) {
      voiceCaptureExpected=false;
      Serial.printf("VOICE_CAPTURE duration_ms=%u samples=%u bytes=%u peak=%u mean_abs=%u worker_stack_free=%u\n",
        unsigned(duration),unsigned(voiceRecorder.sampleCount()),unsigned(voiceWavBytes),unsigned(voiceRecorder.peak()),
        unsigned(voiceRecorder.meanAbs()),unsigned(voiceRecorder.workerStackFree()));
      if(voiceDiagnosticLocal) {voiceDiagnosticLocal=false;voiceFreeClip();voiceShowMention();return;}
      if(duration<350) {voiceFreeClip();voiceError("That recording was too short. Hold blue and speak for at least a second.");return;}
      voiceStage(VoiceReplyStage::TRANSCRIBING);voiceQueue(BadgeHttpKind::TRANSCRIPTION);
    } else if(!voiceRecorder.busy() && voiceRecorder.error()!=VoiceRecorder::OK) {
      voiceCaptureExpected=false;voiceError("The microphone could not capture audio. Please try again.");
    }
  }
  if(voiceRequest==BadgeHttpKind::NONE)return;
  const BadgeHttpKind kind=voiceRequest;
  if(!voiceRequestStarted) {
    if(!voiceLive()) {voiceCancelRequest();voiceError("Wi-Fi or sign-in is unavailable. Reconnect and try again.",voiceUnresolved());return;}
    if(badgeHttp.busy() || profileRefreshActive)return;
    voiceRequestBearer=accessToken;
  } else if(badgeHttp.kind()!=kind) {
    voiceCancelRequest();voiceError("The request was interrupted. Check your connection.",voiceUnresolved());return;
  }
  JsonDocument response;int code=-40;
  if(kind==BadgeHttpKind::REPLY_INBOX)code=badgeHttp.getJson(kind,VOICE_INBOX_URL,voiceRequestBearer,response);
  else if(kind==BadgeHttpKind::TRANSCRIPTION)code=badgeHttp.postWav(voiceUseDeepgram?VOICE_DEEPGRAM_URL:VOICE_TRANSCRIPTION_URL,voiceRequestBearer,voiceWav,voiceWavBytes,response);
  else if(kind==BadgeHttpKind::REPLY_SEND)code=badgeHttp.postDeviceJson(kind,VOICE_SEND_URL,voiceRequestBody,voiceRequestBearer,voiceKey,response);
  else if(kind==BadgeHttpKind::REPLY_STATUS)code=badgeHttp.getReplyStatus(VOICE_STATUS_URL,voiceRequestBearer,voiceKey,response);
  if(code==BADGE_HTTP_PENDING) {if(badgeHttp.kind()==kind)voiceRequestStarted=true;return;}
  if(kind==BadgeHttpKind::REPLY_SEND && !voiceRequestStarted && code<0) {
    voiceCancelRequest();voiceError("Nothing was submitted. The request could not start; return to the inbox and try again.",voiceUnresolved());return;
  }
  voiceRequest=BadgeHttpKind::NONE;voiceRequestStarted=false;voiceWipe(voiceRequestBearer);voiceWipe(voiceRequestBody);voiceFreeClip();
  voiceConsume(kind,code,response);
}
void renderVoiceReply() {
  voiceView.canRecord=voiceCanRecord();
  if(voiceView.stage==VoiceReplyStage::REVIEW) {
    // More only advances one page. Mark coverage only when it has been drawn.
    if(uint32_t(voiceView.textPage)<=voiceReviewedThrough)voiceReviewedThrough=max(voiceReviewedThrough,uint32_t(voiceView.textPage)+uint32_t(1));
    if(voiceReviewedThrough>=voiceReplyPageCount(voiceView))voiceSendGate.reviewedAllPages();
    voiceView.canSend=voiceSendGate.canSend(voiceLive() && voiceSameContext(),voicePostingAllowed && voiceDraftValid && voiceView.count && !voiceUnresolved());
    voiceView.progress=!voiceView.count?"DICTATION ONLY / NO RECIPIENT":!voicePostingAllowed?"RECONNECT X FOR REPLY PERMISSION":
      !voiceDraftValid?"RECORD A SHORTER REPLY":voiceView.canSend?"TAP SEND WHEN READY":"REVIEW EVERY PAGE";
  } else if(voiceView.stage==VoiceReplyStage::INBOX) {
    voiceView.progress=voiceView.count && !voicePostingAllowed?"RECONNECT X FOR REPLY PERMISSION":
      voiceView.canRecord?(voiceUseDeepgram?"HOLD BLUE: DEEPGRAM":"HOLD BLUE FOR DICTATION"):"CONNECT SPEECH IN CHAN.DEV";
  }
  drawVoiceReply(voiceView);M5.Display.display();
}
bool voiceNormalizeAscii(const String &input,String &output) {
  output="";bool space=false;
  for(size_t i=0;i<input.length();++i) {
    const uint8_t c=uint8_t(input[i]);
    if(c==' ' || c=='\t' || c=='\r' || c=='\n') {space=!output.isEmpty();continue;}
    if(c<32 || c>126)return false;
    if(space)output+=' ';
    output+=char(c>='A' && c<='Z'?c+('a'-'A'):c);space=false;
  }
  return !output.isEmpty();
}
void voiceDiagnostic(const JsonDocument &cmd) {
  String action=cmd["action"]|"";
  if(action=="open")enterVoiceReplies();
  else if(action=="status") {
    Serial.printf("VOICE_STATUS screen=%s stage=%u configured=%u recording=%u busy=%u duration_ms=%u error=%d inbox_count=%u can_record=%u can_send=%u pending_receipt=%u network_kind=%u\n",
      currentScreenName(),unsigned(voiceView.stage),unsigned(voiceConfigured),unsigned(voiceRecorder.recording()),
      unsigned(voiceRecorder.busy()),unsigned(voiceRecorder.durationMs()),voiceRecorder.error(),unsigned(voiceView.count),
      unsigned(voiceCanRecord()),unsigned(voiceView.canSend),unsigned(voiceUnresolved()),unsigned(badgeHttp.kind()));
  } else if(action=="record_test" && screen==REPLIES && voiceCanRecord() &&
      (voiceView.stage==VoiceReplyStage::INBOX || voiceView.stage==VoiceReplyStage::REVIEW)) {
    unsigned duration=cmd["duration_ms"]|2000u;
    if(duration<500 || duration>5000) {Serial.println("COMMAND_REJECTED");return;}
    voiceStartRecording();
    if(voiceCaptureExpected) {voiceDiagnosticLocal=false;voiceDiagnosticStop=millis()+duration;}
  } else if(action=="verify_transcript" && screen==REPLIES && voiceView.stage==VoiceReplyStage::REVIEW) {
    String expected=cmd["expected"]|"",normalizedExpected,normalizedActual;
    if(expected.isEmpty() || expected.length()>160 || !voiceNormalizeAscii(expected,normalizedExpected)) {Serial.println("COMMAND_REJECTED");return;}
    const bool match=voiceNormalizeAscii(voiceView.transcript,normalizedActual) && normalizedActual==normalizedExpected;
    Serial.printf("VOICE_TRANSCRIPT match=%u length=%u expected_length=%u\n",unsigned(match),unsigned(voiceView.transcript.length()),unsigned(expected.length()));
    voiceWipe(normalizedExpected);voiceWipe(normalizedActual);voiceWipe(expected);
  } else if(action=="mic_test" && screen==REPLIES && !voiceUnresolved() && !voiceRecorder.busy() && voiceRequest==BadgeHttpKind::NONE) {
    unsigned duration=cmd["duration_ms"]|2000u;
    if(duration<500 || duration>5000) {Serial.println("COMMAND_REJECTED");return;}
    if(voiceRecorder.start()) {
      voiceDiagnosticLocal=true;voiceDiagnosticStop=millis()+duration;voiceCaptureExpected=true;
      voiceStage(VoiceReplyStage::RECORDING,"Local microphone check");
    }
  } else if(action=="cancel" && screen==REPLIES)leaveVoiceReplies(false);
  else Serial.println("COMMAND_REJECTED");
}
