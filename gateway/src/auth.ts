import type { WorkerEntrypoint } from 'cloudflare:workers';
import { bytes, deadline, GatewayError, object, remaining } from './http';

export type DeviceContext = { userId: string; workspaceId: string; sessionId: string; clientId: string };
export type Provider = 'x' | 'xai' | 'deepgram';
export type Operation = 'x:read' | 'x:reply' | 'xai:transcribe' | 'deepgram:transcribe';
export type ProviderAccess = {
  context: DeviceContext; provider: Provider; authMethod: 'oauth' | 'api_key';
  credential: string; connectionId: string; scopes: string[];
};
// The external Worker cannot be imported into this source repo. This declaration
// describes its narrowed RPC entrypoint; every result is validated below.
export declare class DevicesIdentity extends WorkerEntrypoint {
  resolveDevicesContext(bearer: string): Promise<unknown>;
  getDevicesProviderAccess(bearer: string, operation: Operation): Promise<unknown>;
}
export type IdentityService = Pick<DevicesIdentity, 'resolveDevicesContext' | 'getDevicesProviderAccess'>;
export const publicContext = (context: DeviceContext) => ({ userId: context.userId, organizationId: context.workspaceId });
export const sameContext = (a: DeviceContext, b: DeviceContext) =>
  a.userId === b.userId && a.workspaceId === b.workspaceId && a.clientId === b.clientId && a.sessionId === b.sessionId;
export function ownerName(context: DeviceContext): string { return JSON.stringify([context.clientId, context.userId, context.workspaceId]); }
export function bearerFrom(request: Request): string {
  const header = request.headers.get('Authorization') ?? '';
  const match = /^Bearer ([A-Za-z0-9._~-]{16,16384})$/.exec(header);
  if (!match) throw new GatewayError('sign_in_required', 'devices_session_required', 401, 'Reconnect this device to AuthKit.');
  return match[1];
}
function validateContext(value: unknown, client: string): DeviceContext {
  const data = object(value);
  for (const name of ['userId', 'workspaceId', 'sessionId', 'clientId']) {
    if (typeof data[name] !== 'string' || !/^[A-Za-z0-9_-]{1,128}$/.test(data[name])) throw new GatewayError('unavailable', 'invalid_identity_response');
  }
  if (data.clientId !== client) throw new GatewayError('sign_in_required', 'wrong_devices_client', 401, 'Reconnect this device to AuthKit.');
  return { userId: String(data.userId), workspaceId: String(data.workspaceId), sessionId: String(data.sessionId), clientId: String(data.clientId) };
}
function identityFailure(value: Record<string, unknown>): never {
  switch (value.state) {
    case 'sign_in_required':throw new GatewayError('sign_in_required', 'devices_session_required', 401, 'Reconnect this device to AuthKit.');
    case 'workspace_required':throw new GatewayError('workspace_required', 'personal_workspace_required', 403, 'Connect your personal workspace first.');
    case 'connect_required':throw new GatewayError('connect_required', 'provider_connection_required', 409, 'Connect this provider at auth.chan.dev/connections.');
    case 'scope_required':throw new GatewayError('scope_required', 'x_write_scope_required', 403, 'Reconnect X to allow replies.');
    default:throw new GatewayError('unavailable', 'identity_unavailable');
  }
}
export async function resolveContext(service: IdentityService, bearer: string, client: string): Promise<{ context: DeviceContext; transcription: { xai: boolean; deepgram: boolean } }> {
  const value = object(await deadline(service.resolveDevicesContext(bearer), 12_000));
  if (value.state !== 'ready') identityFailure(value);
  const transcription = object(value.transcription);
  if (typeof transcription.xai !== 'boolean' || typeof transcription.deepgram !== 'boolean') throw new GatewayError('unavailable', 'invalid_identity_availability');
  return { context: validateContext(value.context, client), transcription: { xai: transcription.xai, deepgram: transcription.deepgram } };
}
export async function providerAccess(service: IdentityService, bearer: string, operation: Operation, expected: DeviceContext, until = Date.now()+50_000): Promise<ProviderAccess> {
  const timeout = remaining(until, 12_000);
  const value = object(await deadline(service.getDevicesProviderAccess(bearer, operation), timeout));
  if (value.state !== 'ready') identityFailure(value);
  const context = validateContext(value.context, expected.clientId);
  if (!sameContext(expected, context)) throw new GatewayError('sign_in_required', 'session_changed', 401, 'Your session changed. Reopen the replies app.');
  const provider = operation.split(':')[0] as Provider;
  const authMethod = provider === 'x' ? 'oauth' : 'api_key';
  if (value.provider !== provider || value.authMethod !== authMethod || typeof value.credential !== 'string' ||
    !/^[\x21-\x7e]{1,8192}$/.test(value.credential) || typeof value.connectionId !== 'string' ||
    !/^[A-Za-z0-9_-]{1,128}$/.test(value.connectionId) || !Array.isArray(value.scopes) || value.scopes.length > 32 ||
    !value.scopes.every((scope: unknown) => typeof scope === 'string' && /^[a-zA-Z0-9:._-]{1,128}$/.test(scope))) {
    throw new GatewayError('unavailable', 'invalid_provider_access');
  }
  if (operation === 'x:reply' && !value.scopes.includes('tweet.write')) throw new GatewayError('scope_required', 'x_write_scope_required', 403, 'Reconnect X to allow replies.');
  if (bytes(value.credential) > 8192) throw new GatewayError('unavailable', 'invalid_provider_access');
  return { context, provider, authMethod, credential: value.credential, connectionId: value.connectionId, scopes: value.scopes as string[] };
}
