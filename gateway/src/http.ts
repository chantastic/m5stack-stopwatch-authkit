export const MAX_RESPONSE_BYTES = 8192;
export const ROUTE_DEADLINE_MS = 50_000;
export function remaining(until: number, maximum: number): number {
  const left = Math.min(maximum, until - Date.now());
  if (left <= 0) throw new GatewayError('unavailable', 'request_deadline', 503, 'This request took too long. Please try again.');
  return left;
}
export const utf8 = new TextEncoder();
export const bytes = (text: string) => utf8.encode(text).byteLength;
export const object = (value: unknown): Record<string, unknown> =>
  value !== null && typeof value === 'object' && !Array.isArray(value) ? value as Record<string, unknown> : {};

export class GatewayError extends Error {
  constructor(readonly state: string, readonly code: string, readonly status = 503,
    message = 'This request could not finish. Please try again.', readonly retryable = false) { super(message); }
}
export function response(value: unknown, status = 200): Response {
  let body = JSON.stringify(value);
  if (bytes(body) > MAX_RESPONSE_BYTES) {
    status = 502;
    body = JSON.stringify({ state: 'unavailable', error: 'response_too_large', message: 'The service returned too much data.', retryable: false });
  }
  return new Response(body, { status, headers: {
    'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'private, no-store',
    'Vary': 'Authorization', 'Referrer-Policy': 'no-referrer', 'X-Content-Type-Options': 'nosniff',
  } });
}
export function failure(error: unknown): { state: string; error: string; message: string; retryable: boolean } {
  const known = error instanceof GatewayError ? error : new GatewayError('unavailable', 'service_unavailable');
  return { state: known.state, error: known.code, message: known.message, retryable: known.retryable };
}
export async function deadline<T>(operation: Promise<T>, ms: number): Promise<T> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  try { return await Promise.race([operation, new Promise<never>((_, reject) => {
    timer = setTimeout(() => reject(new GatewayError('unavailable', 'identity_timeout')), ms);
  })]); } finally { clearTimeout(timer); }
}
export async function readBounded(body: ReadableStream<Uint8Array> | null, max: number, signal?: AbortSignal): Promise<Uint8Array> {
  if (!body) return new Uint8Array();
  const reader = body.getReader();
  const parts: Uint8Array[] = [];
  let length = 0;
  const abort = () => { void reader.cancel().catch(() => {}); };
  signal?.addEventListener('abort', abort, { once: true });
  try {
    while (true) {
      if (signal?.aborted) throw new GatewayError('unavailable', 'request_interrupted');
      const { done, value } = await reader.read();
      if (signal?.aborted) throw new GatewayError('unavailable', 'request_interrupted');
      if (done) break;
      if (value.byteLength > max - length) throw new GatewayError('invalid_request', 'body_too_large', 413, 'This request is too large.');
      parts.push(value); length += value.byteLength;
    }
    const joined = new Uint8Array(length);
    let offset = 0;
    for (const part of parts) { joined.set(part, offset); offset += part.byteLength; }
    return joined;
  } finally { signal?.removeEventListener('abort', abort); void reader.cancel().catch(() => {}); reader.releaseLock(); }
}
export async function requestBody(request: Request, max: number, timeout = 30_000): Promise<Uint8Array> {
  const length = request.headers.get('Content-Length');
  if (length && (!/^\d+$/.test(length) || Number(length) > max)) throw new GatewayError('invalid_request', 'body_too_large', 413, 'This request is too large.');
  const controller = new AbortController();
  const abort = () => controller.abort();
  request.signal.addEventListener('abort', abort, { once: true });
  let timer: ReturnType<typeof setTimeout> | undefined;
  const interrupted = new Promise<never>((_, reject) => {
    timer = setTimeout(() => { controller.abort(); reject(new GatewayError('unavailable', 'upload_interrupted')); }, timeout);
  });
  try {
    if (request.signal.aborted) controller.abort();
    return await Promise.race([readBounded(request.body, max, controller.signal), interrupted]);
  } finally { clearTimeout(timer); request.signal.removeEventListener('abort', abort); }
}
export function parseJson(raw: Uint8Array): unknown {
  try { return JSON.parse(new TextDecoder('utf-8', { fatal: true, ignoreBOM: false }).decode(raw)) as unknown; }
  catch { throw new GatewayError('invalid_request', 'invalid_json', 400, 'The request is not valid JSON.'); }
}
export type ProviderResponse = { status: number; data: Record<string, unknown> };
// Fixed callers choose the endpoint. No redirect may carry any credential to a
// new origin. A single deadline covers both response headers and bounded body.
export async function providerJson(url: string, init: RequestInit, max = 65536, timeout = 15_000): Promise<ProviderResponse> {
  const controller = new AbortController();
  let reader: ReadableStreamDefaultReader<Uint8Array> | undefined;
  let timer: ReturnType<typeof setTimeout> | undefined;
  const interrupted = new Promise<never>((_, reject) => { timer = setTimeout(() => {
    controller.abort(); if (reader) void reader.cancel().catch(() => {});
    reject(new GatewayError('unavailable', 'provider_timeout'));
  }, timeout); });
  const operation = (async () => {
    const result = await fetch(url, { ...init, redirect: 'manual', signal: controller.signal });
    if (result.status >= 300 && result.status < 400) {
      void result.body?.cancel().catch(() => {});
      throw new GatewayError('unavailable', 'provider_redirect');
    }
    let data: Record<string, unknown> = {};
    if (result.body && /^application\/json(?:\s*;|$)/i.test(result.headers.get('Content-Type') ?? '')) {
      reader = result.body.getReader();
      const chunks: Uint8Array[] = []; let length = 0;
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        if (value.byteLength > max - length) throw new GatewayError('unavailable', 'provider_response_too_large');
        chunks.push(value); length += value.byteLength;
      }
      const raw = new Uint8Array(length); let offset = 0;
      for (const part of chunks) { raw.set(part, offset); offset += part.byteLength; }
      try { data = object(parseJson(raw)); } catch { throw new GatewayError('unavailable', 'invalid_provider_response', 502); }
    } else void result.body?.cancel().catch(() => {});
    return { status: result.status, data };
  })();
  try { return await Promise.race([operation, interrupted]); }
  finally { clearTimeout(timer); controller.abort(); if (reader) void reader.cancel().catch(() => {}); }
}
