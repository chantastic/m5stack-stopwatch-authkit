import { WorkerEntrypoint } from 'cloudflare:workers';
const clientId = 'client_01M24S76WQHXQ6QJJ7S83CMR3F';
const context = (bearer) => ({ userId: 'user_' + bearer.replace(/[^a-z0-9]/gi, '_'), workspaceId: 'org_personal', sessionId: 'session_current', clientId });
export class DevicesIdentity extends WorkerEntrypoint {
  async resolveDevicesContext(bearer) {
    if (bearer === 'invalid-session-0000') return { state: 'sign_in_required' };
    if (bearer === 'missing-workspace-00') return { state: 'workspace_required' };
    if (bearer === 'wrong-client-000000') return { state: 'ready', context: { ...context(bearer), clientId: 'client_other' }, transcription: { xai: true, deepgram: true } };
    return { state: 'ready', context: context(bearer), transcription: { xai: true, deepgram: true } };
  }
  async getDevicesProviderAccess(bearer, operation) {
    if (bearer === 'revoked-session-000') return { state: 'sign_in_required' };
    if (bearer === 'missing-scope-00000' && operation === 'x:reply') return { state: 'scope_required', provider: 'x' };
    if (bearer === 'missing-provider-00') return { state: 'connect_required', provider: operation.split(':')[0] };
    const provider = operation.split(':')[0];
    return { state: 'ready', context: bearer === 'session-switched-00' ? { ...context(bearer), userId: 'user_other' } : context(bearer), provider,
      authMethod: provider === 'x' ? 'oauth' : 'api_key', credential: `test-credential-${provider}`,
      connectionId: bearer === 'changed-connection-0' ? 'connection_replaced' : `connection_${provider}`,
      scopes: provider === 'x' ? ['users.read', 'tweet.read', 'tweet.write', 'offline.access'] : [] };
  }
}
export default { fetch() { return new Response('No public identity endpoint', { status: 404 }); } };
