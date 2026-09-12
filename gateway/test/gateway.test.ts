import { SELF, env, runInDurableObject, listDurableObjectIds, evictDurableObject } from 'cloudflare:test';
import { fetchMock } from './provider-mock';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { ownerName, type DeviceContext } from '../src/auth';
import { MAX_AUDIO_BYTES, wavDurationMs } from '../src/audio';
import { bytes, providerJson, readBounded, requestBody, response } from '../src/http';
import { eligible, validateReplyText } from '../src/x';

const CLIENT='client_01M24S76WQHXQ6QJJ7S83CMR3F';
const KEY='approved-reply-key-0001';
const TOKEN='valid-device-session';
const context = (token=TOKEN): DeviceContext => ({ userId: `user_${token.replace(/[^a-z0-9]/gi,'_')}`, workspaceId: 'org_personal', sessionId: 'session_current', clientId: CLIENT });
const expectedContext = (token=TOKEN) => ({ userId: context(token).userId, organizationId: context(token).workspaceId });
const input = { targetId: '456', text: 'Thanks for mentioning me!', senderId: '123', connectionId: 'connection_x' };
const post = { id: '456', author_id: '789', text: '@sample Tell me more.', created_at: '2026-09-11T20:00:00.000Z', entities: { mentions: [{ start: 0, end: 7, username: 'sample' }] } };
const jsonHeaders = { 'Content-Type':'application/json' };
const xPool = () => fetchMock.get('https://api.x.com');
function me(id='123') { xPool().intercept({ path:'/2/users/me', headers: { authorization:'Bearer test-credential-x' } }).reply(200,{data:{id,username:'sample'}},{headers:jsonHeaders}); }
function target(value=post) { xPool().intercept({ path:'/2/tweets/456?tweet.fields=author_id,entities' }).reply(200,{data:value},{headers:jsonHeaders}); }
function send(status=201, data: unknown={data:{id:'999',text:input.text}}) { xPool().intercept({ path:'/2/tweets',method:'POST', body:JSON.stringify({text:input.text,reply:{in_reply_to_tweet_id:'456'}}) }).reply(status,data,{headers:jsonHeaders}); }
const call = (path:string, init:RequestInit={}, token=TOKEN) => SELF.fetch(`https://devices.example${path}`,{...init,headers:{Authorization:`Bearer ${token}`,...init.headers}});
const submit = (body:unknown=input,key=KEY,token=TOKEN) => call('/v1/x/replies',{method:'POST',headers:{...jsonHeaders,'Idempotency-Key':key},body:JSON.stringify(body)},token);
const status = (key=KEY,token=TOKEN) => call('/v1/x/replies/status',{headers:{'Idempotency-Key':key}},token);
function wav(samples=16000): Uint8Array {
  const data=new Uint8Array(44+samples*2); const v=new DataView(data.buffer);
  for(const [at,text] of [[0,'RIFF'],[8,'WAVE'],[12,'fmt '],[36,'data']] as const)for(let i=0;i<text.length;i++)data[at+i]=text.charCodeAt(i);
  v.setUint32(4,data.length-8,true);v.setUint32(16,16,true);v.setUint16(20,1,true);v.setUint16(22,1,true);v.setUint32(24,16000,true);v.setUint32(28,32000,true);v.setUint16(32,2,true);v.setUint16(34,16,true);v.setUint32(40,samples*2,true);
  return data;
}
beforeEach(async()=>{
  // Current Cloudflare Vitest shares storage. Explicitly isolate every test.
  for (const id of await listDurableObjectIds(env.REPLIES)) await runInDurableObject(env.REPLIES.get(id),async(_instance,state)=>{
    for(const table of ['receipts','targets','limits','identity'])state.storage.sql.exec(`DELETE FROM ${table}`);
  });
  fetchMock.activate();fetchMock.disableNetConnect();
});
afterEach(()=>{try{fetchMock.assertNoPendingInterceptors();}finally{fetchMock.deactivate();}});

describe('public route and private identity boundary',()=>{
  it('does not accept browser cookies or an anonymous session',async()=>{
    const anonymous=await SELF.fetch('https://devices.example/v1/x/replies',{headers:{Cookie:'shared=anything'}});
    expect(anonymous.status).toBe(401);
    for(const token of ['invalid-session-0000','missing-workspace-00','wrong-client-000000'])expect((await call('/v1/x/replies',{},token)).status).toBeGreaterThanOrEqual(400);
  });
  it('rejects alternate paths, arbitrary queries and methods without provider access',async()=>{
    for(const path of ['/v1/x/replies/status?key=x','/v1/proxy','/v1/transcriptions?provider=deepgram'])expect((await call(path)).status).toBe(404);
    expect((await call('/v1/x/replies',{method:'PUT'})).status).toBe(404);
  });
  it('keeps X credits failures honest and preserves verified dictation readiness',async()=>{
    xPool().intercept({path:'/2/users/me'}).reply(402,{title:'CreditsDepleted'},{headers:jsonHeaders});
    const result=await call('/v1/x/replies');expect(result.status).toBe(402);
    expect(await result.json()).toMatchObject({state:'credits_required',context:expectedContext(),transcription:{xai:true,deepgram:true}});
    expect(result.headers.get('Cache-Control')).toBe('private, no-store');
  });
  it('returns only bounded, eligible, complete mentions and the current sender',async()=>{
    me(); xPool().intercept({path:'/2/users/123/mentions?max_results=5&tweet.fields=author_id,created_at,entities&expansions=author_id&user.fields=name,username'}).reply(200,{data:[post,{...post,id:'457',text:'Not an explicit mention',entities:{}},{...post,id:'458',text:'x'.repeat(1001)}],includes:{users:[{id:'789',username:'writer',name:'A Writer'}]}},{headers:jsonHeaders});
    const res=await call('/v1/x/replies');const data=await res.json<Record<string,unknown>>();
    expect(data).toMatchObject({state:'ready',sender:{id:'123',username:'sample',connectionId:'connection_x'},context:expectedContext(),items:[{id:'456',text:post.text}],canSend:true});
    expect(JSON.stringify(data)).not.toContain('test-credential');expect(bytes(JSON.stringify(data))).toBeLessThanOrEqual(8192);
  });
});

describe('durable explicit-send receipts',()=>{
  it('posts exactly once, returns the same receipt and status, and blocks a fresh key for the same target',async()=>{
    me();target();send();
    const first=await submit();expect(first.status).toBe(200);expect(await first.json()).toMatchObject({state:'sent',key:KEY,reply:{id:'999',url:'https://x.com/i/status/999'}});
    expect(await (await submit()).json()).toMatchObject({state:'sent',reply:{id:'999'}});
    expect(await (await status()).json()).toMatchObject({state:'sent'});
    expect(await (await submit(input,'new-key-same-target-01')).json()).toMatchObject({state:'failed',error:'target_already_handled'});
  });
  it('does not expose a receipt to another user or another personal workspace',async()=>{
    me();target();send();await submit();
    expect((await status(KEY,'another-session-0000')).status).toBe(404);
    const other=env.REPLIES.getByName(ownerName({...context(),workspaceId:'org_other'}));
    expect(await other.status({...context(),workspaceId:'org_other'},KEY)).toBe(null);
  });
  it('keeps network failure permanently unknown and never reposts under either the same or a new key',async()=>{
    me();target();xPool().intercept({path:'/2/tweets',method:'POST'}).replyWithError(new Error('transport closed'));
    expect(await (await submit()).json()).toMatchObject({state:'unknown',key:KEY});
    expect(await (await submit()).json()).toMatchObject({state:'unknown'});
    expect(await (await submit(input,'new-key-after-error01')).json()).toMatchObject({state:'failed',error:'target_already_handled'});
    expect(await (await status()).json()).toMatchObject({state:'unknown'});
  });
  it('treats malformed successes, redirects, 5xx and timeout-like errors as unknown',async()=>{
    for(const [code,data] of [[201,{data:{}}],[502,{}],[408,{}],[409,{}],[302,{}]] as const){
      const token=`test-case-session-${code}`; me();target();send(code,data);
      expect(await (await submit(input,KEY,token)).json()).toMatchObject({state:'unknown'});
    }
  });
  it('persists definitive provider rejection as failed and permits a later explicit fresh attempt',async()=>{
    me();target();send(403,{title:'Forbidden'});
    expect(await (await submit()).json()).toMatchObject({state:'failed',error:'x_access_denied'});
    expect(await (await status()).json()).toMatchObject({state:'failed'});
    me();target();send();
    expect(await (await submit(input,'fresh-key-after-denial')).json()).toMatchObject({state:'sent'});
  });
  it('writes authenticated failed receipts for missing scope, changed connection, invalid text and revoked preparation',async()=>{
    for(const token of ['missing-scope-00000','changed-connection-0','revoked-session-000','session-switched-00']){
      expect(await (await submit(input,KEY,token)).json()).toMatchObject({state:'failed',key:KEY});
      expect(await (await status(KEY,token)).json()).toMatchObject({state:'failed'});
    }
    expect(await (await submit({...input,text:'x'.repeat(281)})).json()).toMatchObject({state:'failed',error:'reply_too_long_or_invalid'});
  });
  it('checks current sender identity and fresh target eligibility before sending',async()=>{
    me('321');expect(await (await submit()).json()).toMatchObject({state:'failed',error:'x_sender_changed'});
    me();target({...post,entities:{mentions:[]}});
    expect(await (await submit(input,'key-ineligible-00001')).json()).toMatchObject({state:'failed',error:'reply_not_eligible'});
  });
  it('rejects reusing the same key for different approved text without altering its original receipt',async()=>{
    me();target();send();await submit();
    expect((await submit({...input,text:'A different message'})).status).toBeGreaterThanOrEqual(400);
    expect(await (await status()).json()).toMatchObject({state:'sent',reply:{id:'999'}});
  });
  it('coalesces concurrent sends and has already persisted the target guard before provider dispatch',async()=>{
    me();target();
    let observedPending=false;
    xPool().intercept({path:'/2/tweets',method:'POST'}).replyWith(async()=>{
      await runInDurableObject(env.REPLIES.getByName(ownerName(context())),async(_instance,state)=>{
        const rows=state.storage.sql.exec<{state:string;receipt:string}>('SELECT state,receipt FROM receipts').toArray();
        expect(rows).toHaveLength(1);expect(rows[0].state).toBe('pending');
        expect(state.storage.sql.exec('SELECT * FROM targets').toArray()).toHaveLength(1);
        expect(rows[0].receipt).not.toContain('test-credential');expect(rows[0].receipt).not.toContain(input.text);
        observedPending=true;
      });
      return Response.json({data:{id:'999'}} ,{status:201});
    });
    const results=await Promise.all([submit(),submit()]);
    expect(observedPending).toBe(true);
    const states=await Promise.all(results.map(async r=>(await r.json<{state:string}>()).state));
    expect(states).toContain('sent');expect(states.every(s=>s==='sent'||s==='pending')).toBe(true);
    const ledger=env.REPLIES.getByName(ownerName(context()));
    await runInDurableObject(ledger,async(_instance,state)=>{
      expect(state.storage.sql.exec('SELECT * FROM targets').toArray()).toHaveLength(1);
      expect(state.storage.sql.exec('SELECT * FROM receipts').toArray()).toHaveLength(1);
    });
  });
  it('permits only one provider POST for two simultaneous fresh keys on one target',async()=>{
    me();target();send();
    const replies=await Promise.all([submit(),submit(input,'different-key-concurrent')]);
    const states=await Promise.all(replies.map(async res=>(await res.json<{state:string}>()).state));
    expect(states.sort()).toEqual(['failed','sent']);
  });
  it('preserves receipts and target guards through real Durable Object eviction',async()=>{
    me();target();send();await submit();
    await evictDurableObject(env.REPLIES.getByName(ownerName(context())));
    expect(await (await submit()).json()).toMatchObject({state:'sent'});
    expect(await (await submit(input,'fresh-key-after-evict')).json()).toMatchObject({state:'failed',error:'target_already_handled'});
  });
  it('limits fresh receipt admission without adding rows or blocking existing status',async()=>{
    me();target();send();await submit();
    const ledger=env.REPLIES.getByName(ownerName(context()));
    await runInDurableObject(ledger,async(_instance,state)=>{state.storage.sql.exec("UPDATE limits SET period=?,count=60 WHERE bucket='admission'",Math.floor(Date.now()/3_600_000));});
    expect((await submit({...input,targetId:'777'},'excess-key-admission01')).status).toBe(429);
    expect(await (await status()).json()).toMatchObject({state:'sent'});
    await runInDurableObject(ledger,async(_instance,state)=>{expect(state.storage.sql.exec('SELECT * FROM receipts').toArray()).toHaveLength(1);});
  });
  it('persists expired pre-dispatch preparation as failed without any provider request',async()=>{
    const ledger=env.REPLIES.getByName(ownerName(context()));
    expect(await ledger.send(context(),TOKEN,input,KEY,Date.now()-1)).toMatchObject({state:'failed',error:'request_deadline'});
    expect(await (await status()).json()).toMatchObject({state:'failed'});
  });
  it('limits daily transcription attempts without storing audio or resetting sender guards',async()=>{
    const ledger=env.REPLIES.getByName(ownerName(context()));
    expect(await ledger.permit(context(),'audio')).toBe(true);
    await runInDurableObject(ledger,async(_instance,state)=>{state.storage.sql.exec("UPDATE limits SET count=40 WHERE bucket='audio_daily'");});
    expect(await ledger.permit(context(),'audio')).toBe(false);
  });
  it('makes abandoned pending work unknown without deleting its guard',async()=>{
    me();target();send();await submit();const ledger=env.REPLIES.getByName(ownerName(context()));
    await runInDurableObject(ledger,async(_instance,state)=>{state.storage.sql.exec("UPDATE receipts SET state='pending',updated=?,receipt=? WHERE key=?",Date.now()-100_000,JSON.stringify({state:'pending',key:KEY,targetId:'456',senderId:'123',retryable:false}),KEY);});
    expect(await (await status()).json()).toMatchObject({state:'unknown'});
    expect(await (await submit(input,'new-key-after-crash01')).json()).toMatchObject({state:'failed',error:'target_already_handled'});
  });
});

describe('audio and intentional transcription provider choice',()=>{
  it('accepts only exact bounded PCM16LE mono 16k WAV',()=>{
    expect(wavDurationMs(wav())).toBe(1000);expect(wav(MAX_AUDIO_BYTES/2-22).byteLength).toBe(MAX_AUDIO_BYTES);
    expect(wavDurationMs(wav(480000))).toBe(30000);
    for(const offset of [0,4,8,12,16,20,22,24,28,32,34,36,40]){const broken=wav();broken[offset]^=1;expect(()=>wavDurationMs(broken)).toThrow();}
    expect(()=>wavDurationMs(wav(480001))).toThrow();expect(()=>wavDurationMs(new Uint8Array(45))).toThrow();
  });
  it('uses xAI by default with file last and returns full text independently of an X inbox',async()=>{
    fetchMock.get('https://api.x.ai').intercept({path:'/v1/stt',method:'POST',headers:{authorization:'Bearer test-credential-xai'}}).reply(200,{text:'A complete spoken reply.'},{headers:jsonHeaders});
    const result=await call('/v1/transcriptions',{method:'POST',headers:{'Content-Type':'audio/wav'},body:wav()});
    expect(await result.json()).toMatchObject({state:'transcribed',provider:'xai',text:'A complete spoken reply.',durationMs:1000,context:expectedContext()});
  });
  it('does not silently fall back after provider failure, but supports an explicit Deepgram request',async()=>{
    fetchMock.get('https://api.x.ai').intercept({path:'/v1/stt',method:'POST'}).reply(402,{}, {headers:jsonHeaders});
    expect(await (await call('/v1/transcriptions',{method:'POST',headers:{'Content-Type':'audio/wav'},body:wav()})).json()).toMatchObject({state:'credits_required'});
    fetchMock.get('https://api.deepgram.com').intercept({path:'/v1/listen?model=nova-3&smart_format=true',method:'POST',headers:{authorization:'Token test-credential-deepgram','content-type':'audio/wav'}}).reply(200,{results:{channels:[{alternatives:[{transcript:'Explicit fallback reply.'}]}]}},{headers:jsonHeaders});
    expect(await (await call('/v1/transcriptions/deepgram',{method:'POST',headers:{'Content-Type':'audio/wav'},body:wav()})).json()).toMatchObject({state:'transcribed',provider:'deepgram',text:'Explicit fallback reply.'});
  });
  it('does not return credential echoes, unbounded transcripts or empty speech',async()=>{
    for(const text of ['test-credential-xai','x'.repeat(3001),'']){
      fetchMock.get('https://api.x.ai').intercept({path:'/v1/stt',method:'POST'}).reply(200,{text},{headers:jsonHeaders});
      const result=await call('/v1/transcriptions',{method:'POST',headers:{'Content-Type':'audio/wav'},body:wav()});
      expect(result.status).toBeGreaterThanOrEqual(400);expect(await result.text()).not.toContain('test-credential');
    }
  });
});

describe('stream bounds and exact text validation',()=>{
  it('counts X weighted characters and emoji/URLs rather than UTF-16 length',()=>{
    expect(()=>validateReplyText('x'.repeat(280))).not.toThrow();expect(()=>validateReplyText('x'.repeat(281))).toThrow();
    expect(()=>validateReplyText('🙂'.repeat(140))).not.toThrow();expect(()=>validateReplyText('🙂'.repeat(141))).toThrow();
    expect(()=>validateReplyText('https://example.com/'+ 'a'.repeat(400))).not.toThrow();
  });
  it('requires a genuine matching mention entity and rejects self replies',()=>{
    const sender={id:'123',username:'sample',connectionId:'connection_x'};
    expect(eligible(post,sender)).toBe(true);expect(eligible({...post,author_id:'123'},sender)).toBe(false);
    expect(eligible({...post,text:'x@sample'},sender)).toBe(false);
    expect(eligible({...post,entities:{mentions:[{start:0,end:7,username:'sample',id:'other'}]}},sender)).toBe(false);
  });
  it('enforces request and response caps even without Content-Length',async()=>{
    const body=new ReadableStream<Uint8Array>({start(c){c.enqueue(new Uint8Array(10));c.enqueue(new Uint8Array(10));c.close();}});
    await expect(readBounded(body,19)).rejects.toThrow();
    const req=new Request('https://test',{method:'POST',body:new Uint8Array(50)});
    await expect(requestBody(req,49)).rejects.toThrow();
    expect(response({data:'x'.repeat(8192)}).status).toBe(502);
  });
  it('cancels the owned body reader on deadline instead of leaving a locked stream',async()=>{
    let cancelled=false;const body=new ReadableStream<Uint8Array>({cancel(){cancelled=true;}});
    const req=new Request('https://test',{method:'POST',body});
    await expect(requestBody(req,100,5)).rejects.toThrow();await new Promise(r=>setTimeout(r,1));expect(cancelled).toBe(true);
  });
  it('cancels provider waits and refuses oversized or redirected provider responses',async()=>{
    fetchMock.get('https://api.x.ai').intercept({path:'/v1/stt'}).reply(200,'x'.repeat(101),{headers:jsonHeaders});
    await expect(providerJson('https://api.x.ai/v1/stt',{},100)).rejects.toThrow();
    fetchMock.get('https://api.x.ai').intercept({path:'/v1/stt'}).reply(302,'',{headers:{Location:'https://evil.example'}});
    await expect(providerJson('https://api.x.ai/v1/stt',{})).rejects.toThrow();
  });
});
