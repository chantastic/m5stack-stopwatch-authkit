import twitterText from 'twitter-text';
import type { ProviderAccess } from './auth';
import { bytes, GatewayError, object, providerJson, remaining } from './http';

export const MAX_AUDIO_BYTES = 960_044;
export function wavDurationMs(audio: Uint8Array): number {
  if (audio.byteLength < 46 || audio.byteLength > MAX_AUDIO_BYTES || (audio.byteLength - 44) % 2) throw invalidAudio();
  const view = new DataView(audio.buffer, audio.byteOffset, audio.byteLength);
  if (view.getUint32(0, true) !== 0x46464952 || view.getUint32(4, true) !== audio.byteLength - 8 ||
    view.getUint32(8, true) !== 0x45564157 || view.getUint32(12, true) !== 0x20746d66 ||
    view.getUint32(16, true) !== 16 || view.getUint16(20, true) !== 1 || view.getUint16(22, true) !== 1 ||
    view.getUint32(24, true) !== 16000 || view.getUint32(28, true) !== 32000 || view.getUint16(32, true) !== 2 ||
    view.getUint16(34, true) !== 16 || view.getUint32(36, true) !== 0x61746164 || view.getUint32(40, true) !== audio.byteLength - 44) throw invalidAudio();
  return Math.round((audio.byteLength - 44) / 32);
}
function invalidAudio() { return new GatewayError('invalid_audio', 'invalid_audio', 422, 'Record a new reply using the device microphone.'); }
export async function transcribe(audio: Uint8Array, access: ProviderAccess, until = Date.now()+50_000) {
  const durationMs = wavDurationMs(audio);
  const provider = access.provider;
  if (provider !== 'xai' && provider !== 'deepgram') throw new GatewayError('invalid_request', 'invalid_transcription_provider', 400);
  const payload = new Uint8Array(audio);
  let url = 'https://api.deepgram.com/v1/listen?model=nova-3&smart_format=true';
  let body: BodyInit = payload;
  let headers: Record<string, string> = { Authorization: `Token ${access.credential}`, 'Content-Type': 'audio/wav' };
  if (provider === 'xai') {
    url = 'https://api.x.ai/v1/stt';
    headers = { Authorization: `Bearer ${access.credential}` };
    const form = new FormData();
    form.append('format', 'true'); form.append('language', 'en');
    // The documented xAI batch API requires the file to be the final form field.
    form.append('file', new Blob([payload], { type: 'audio/wav' }), 'reply.wav');
    body = form;
  }
  const result = await providerJson(url, { method: 'POST', headers, body }, 65536, remaining(until, 25_000));
  if (result.status === 401) throw new GatewayError('connect_required', `${provider}_reconnect_required`, 409, `Reconnect ${provider} at auth.chan.dev/connections.`);
  if (result.status === 402) throw new GatewayError('credits_required', `${provider}_credits_required`, 402, `Add API credits to your ${provider} account.`);
  if (result.status === 403) throw new GatewayError('access_denied', `${provider}_access_denied`, 403, `Check your ${provider} connection permissions.`);
  if (result.status === 429) throw new GatewayError('rate_limited', `${provider}_rate_limited`, 429, `${provider} is limiting requests. Try again later.`);
  if (result.status !== 200) throw new GatewayError('unavailable', `${provider}_unavailable`, 503, `${provider} could not transcribe this recording.`);
  let text: unknown = result.data.text;
  if (provider === 'deepgram') {
    const channels = object(result.data.results).channels;
    if (!Array.isArray(channels) || channels.length !== 1) throw new GatewayError('unavailable', 'invalid_transcription');
    const alternatives = object(channels[0]).alternatives;
    text = Array.isArray(alternatives) ? object(alternatives[0]).transcript : undefined;
  }
  if (typeof text !== 'string' || bytes(text) > 3000 || text.includes(access.credential) ||
    /[\u0000-\u0008\u000b-\u001f\u007f]/u.test(text) || !text.isWellFormed()) {
    throw new GatewayError('unavailable', 'transcript_unreviewable', 502, 'Record a shorter reply. The full transcription could not be shown safely.');
  }
  const normalized = text.trim();
  if (!normalized) throw new GatewayError('no_speech', 'no_speech', 422, 'No speech was detected. Hold blue and try again.');
  const parsed = twitterText.parseTweet(normalized);
  return { state: 'transcribed' as const, provider, text: normalized, durationMs, replyValid: parsed.valid, weightedLength: parsed.weightedLength };
}
