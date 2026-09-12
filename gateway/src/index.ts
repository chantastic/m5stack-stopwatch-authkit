import { bearerFrom, ownerName, publicContext, providerAccess, resolveContext, type DevicesIdentity, type DeviceContext } from './auth';
import { MAX_AUDIO_BYTES, transcribe, wavDurationMs } from './audio';
import { failure, GatewayError, parseJson, requestBody, response, remaining, ROUTE_DEADLINE_MS } from './http';
import { validKey, type SendOutcome } from './ledger';
import { currentSender, mentions, sendInput } from './x';
export { ReplyLedger } from './ledger';

const ROUTES = new Set(['GET /v1/x/replies', 'POST /v1/x/replies', 'GET /v1/x/replies/status', 'POST /v1/transcriptions', 'POST /v1/transcriptions/deepgram']);
function idempotency(request: Request): string {
  const key = request.headers.get('Idempotency-Key');
  if (!validKey(key)) throw new GatewayError('invalid_request', 'invalid_idempotency_key', 400, 'A valid send key is required.');
  return key;
}
function mediaType(request: Request, allowed: string) {
  if (request.headers.get('Content-Type')?.split(';', 1)[0].trim().toLowerCase() !== allowed || request.headers.has('Content-Encoding')) {
    throw new GatewayError('invalid_request', 'unsupported_media_type', 415, 'This request uses an unsupported format.');
  }
}
function receiptResponse(receipt: SendOutcome, context: DeviceContext) {
  return response({ ...receipt, context: publicContext(context) }, receipt.state === 'rate_limited' ? 429 : receipt.state === 'idempotency_conflict' ? 409 : receipt.state === 'pending' || receipt.state === 'unknown' ? 202 : 200);
}
export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const until = Date.now()+ROUTE_DEADLINE_MS;
    const url = new URL(request.url);
    if (url.pathname === '/health' && request.method === 'GET' && !url.search) return response({ state: 'ready', service: 'chan-devices' });
    if (url.search || !ROUTES.has(`${request.method} ${url.pathname}`)) return response({ state: 'not_found', error: 'not_found', retryable: false }, 404);
    let context: DeviceContext | undefined;
    let transcription: { xai: boolean; deepgram: boolean } | undefined;
    try {
      const bearer = bearerFrom(request);
      const service = env.DEVICES_IDENTITY as Service<DevicesIdentity>;
      const resolved = await resolveContext(service, bearer, env.DEVICES_CLIENT_ID);
      context = resolved.context; transcription = resolved.transcription;
      const ledger = env.REPLIES.getByName(ownerName(context));
      if (request.method === 'GET' && url.pathname.endsWith('/status')) {
        if (!await ledger.permit(context, 'status')) throw new GatewayError('rate_limited', 'status_rate_limited', 429, 'Please wait before checking again.');
        const receipt = await ledger.status(context, idempotency(request));
        // Absence is not evidence that X did not receive a request. It never
        // creates a failed receipt or permits a client to resend uncertain work.
        return receipt ? receiptResponse(receipt, context) : response({ state: 'not_found', error: 'receipt_not_found', message: 'The send status cannot be confirmed. Do not resend automatically.', retryable: false, context: publicContext(context) }, 404);
      }
      if (request.method === 'POST' && url.pathname === '/v1/x/replies') {
        const key = idempotency(request);
        mediaType(request, 'application/json');
        const input = sendInput(parseJson(await requestBody(request, 4096, remaining(until, 10_000))));
        return receiptResponse(await ledger.send(context, bearer, input, key, until), context);
      }
      if (url.pathname.startsWith('/v1/transcriptions')) {
        mediaType(request, 'audio/wav');
        if (!await ledger.permit(context, 'audio')) throw new GatewayError('rate_limited', 'transcription_rate_limited', 429, 'Please wait before recording another reply.');
        const audio = await requestBody(request, MAX_AUDIO_BYTES, remaining(until, 30_000));
        wavDurationMs(audio);
        const selected = url.pathname.endsWith('/deepgram') ? 'deepgram' : 'xai';
        const access = await providerAccess(service, bearer, `${selected}:transcribe`, context, until);
        const result = await transcribe(audio, access, until);
        return response({ ...result, context: publicContext(context) });
      }
      if (!await ledger.permit(context, 'inbox')) throw new GatewayError('rate_limited', 'inbox_rate_limited', 429, 'Please wait before refreshing your mentions.');
      const access = await providerAccess(service, bearer, 'x:read', context, until);
      const sender = await currentSender(access, until);
      const items = await mentions(access, sender, until);
      return response({ state: 'ready', context: publicContext(context), sender, items, transcription, canSend: access.scopes.includes('tweet.write'), ...(!access.scopes.includes('tweet.write') ? { message: 'Reconnect X to allow replies.' } : {}) });
    } catch (error) {
      const detail = failure(error);
      return response({ ...detail, ...(context ? { context: publicContext(context) } : {}), ...(transcription ? { transcription } : {}) }, error instanceof GatewayError ? error.status : 503);
    }
  },
} satisfies ExportedHandler<Env>;
