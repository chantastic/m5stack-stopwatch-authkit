import twitterText from 'twitter-text';
import { GatewayError, bytes, object, providerJson, remaining, type ProviderResponse } from './http';
import type { ProviderAccess } from './auth';

export type Sender = { id: string; username: string; connectionId: string };
export type Mention = { id: string; text: string; author: { id: string; username: string; name: string }; createdAt: string };
export type SendInput = { targetId: string; text: string; senderId: string; connectionId: string };
export const isXId = (value: unknown): value is string => typeof value === 'string' && /^[1-9][0-9]{0,24}$/.test(value);
export const isUsername = (value: unknown): value is string => typeof value === 'string' && /^[A-Za-z0-9_]{1,15}$/.test(value);
export function xFailure(result: ProviderResponse): GatewayError {
  if (result.status === 402 || result.data.title === 'CreditsDepleted' || result.data.title === 'UsageCapExceeded') return new GatewayError('credits_required', 'x_credits_required', 402, 'Add X API credits in your developer account.');
  if (result.status === 401) return new GatewayError('connect_required', 'x_reconnect_required', 409, 'Reconnect X at auth.chan.dev/connections.');
  if (result.status === 403) return new GatewayError('access_denied', 'x_access_denied', 403, 'X denied access. Check app access and reply permissions.');
  if (result.status === 429) return new GatewayError('rate_limited', 'x_rate_limited', 429, 'X is limiting requests. Try again later.');
  if (result.status === 404) return new GatewayError('not_found', 'x_post_unavailable', 404, 'That post is no longer available.');
  return new GatewayError('unavailable', 'x_unavailable', 503, 'X could not complete this request.');
}
const headers = (access: ProviderAccess) => ({ Authorization: `Bearer ${access.credential}` });
export async function currentSender(access: ProviderAccess, until = Date.now()+50_000): Promise<Sender> {
  const result = await providerJson('https://api.x.com/2/users/me', { headers: headers(access) }, 8192, remaining(until, 15_000));
  if (result.status !== 200) throw xFailure(result);
  const user = object(result.data.data);
  if (!isXId(user.id) || !isUsername(user.username)) throw new GatewayError('unavailable', 'invalid_x_identity');
  return { id: user.id, username: user.username, connectionId: access.connectionId };
}
// An explicit @mention must be a real provider entity at the stated text span,
// not text inside a URL or an inherited conversation participant. X remains the
// final authority for reply controls; the gateway takes the stricter subset.
export function eligible(post: Record<string, unknown>, sender: Sender): boolean {
  if (!isXId(post.id) || !isXId(post.author_id) || post.author_id === sender.id || typeof post.text !== 'string') return false;
  const mentions = object(post.entities).mentions;
  if (!Array.isArray(mentions)) return false;
  const points = Array.from(post.text);
  return mentions.some((value: unknown) => {
    const mention = object(value);
    if (!isUsername(mention.username) || mention.username.toLowerCase() !== sender.username.toLowerCase() ||
      !Number.isInteger(mention.start) || !Number.isInteger(mention.end)) return false;
    const start = Number(mention.start), end = Number(mention.end);
    if (start < 0 || end <= start || end > points.length) return false;
    if (mention.id !== undefined && mention.id !== sender.id) return false;
    return points.slice(start, end).join('').toLowerCase() === `@${sender.username.toLowerCase()}`;
  });
}
export async function mentions(access: ProviderAccess, sender: Sender, until = Date.now()+50_000): Promise<Mention[]> {
  const url = `https://api.x.com/2/users/${sender.id}/mentions?max_results=5&tweet.fields=author_id,created_at,entities&expansions=author_id&user.fields=name,username`;
  const result = await providerJson(url, { headers: headers(access) }, 65536, remaining(until, 15_000));
  if (result.status !== 200) throw xFailure(result);
  if (result.data.data === undefined && object(result.data.meta).result_count === 0) return [];
  if (!Array.isArray(result.data.data) || result.data.data.length > 5) throw new GatewayError('unavailable', 'invalid_x_mentions');
  const users = object(result.data.includes).users;
  if (!Array.isArray(users) || users.length > 10) throw new GatewayError('unavailable', 'invalid_x_authors');
  const output: Mention[] = [];
  for (const value of result.data.data) {
    const post = object(value);
    if (!eligible(post, sender) || typeof post.text !== 'string' || bytes(post.text) > 1000 || /[\u0000-\u0008\u000b-\u001f\u007f]/u.test(post.text)) continue;
    const author = object(users.find((value: unknown) => object(value).id === post.author_id));
    if (!isXId(author.id) || !isUsername(author.username) || typeof author.name !== 'string' || bytes(author.name) > 80 ||
      /[\u0000-\u001f\u007f]/u.test(author.name) || typeof post.created_at !== 'string' || post.created_at.length > 32 || !Number.isFinite(Date.parse(post.created_at))) continue;
    const item = { id: String(post.id), text: post.text, author: { id: author.id, username: author.username, name: author.name }, createdAt: post.created_at };
    if (JSON.stringify(item).includes(access.credential)) continue;
    output.push(item);
  }
  return output;
}
export function sendInput(value: unknown): SendInput {
  const input = object(value);
  if (Object.keys(input).some((key) => !['targetId', 'text', 'senderId', 'connectionId'].includes(key)) ||
    !isXId(input.targetId) || !isXId(input.senderId) || typeof input.text !== 'string' ||
    typeof input.connectionId !== 'string' || !/^[A-Za-z0-9_-]{1,128}$/.test(input.connectionId)) {
    throw new GatewayError('invalid_request', 'invalid_reply', 400, 'The reply request is incomplete. Reopen your mentions.');
  }
  return { targetId: input.targetId, text: input.text, senderId: input.senderId, connectionId: input.connectionId };
}
export function validateReplyText(text: string): void {
  if (!text.trim() || bytes(text) > 3000 || /[\u0000-\u0008\u000b-\u001f\u007f]/u.test(text) || !twitterText.parseTweet(text).valid) {
    throw new GatewayError('invalid_request', 'reply_too_long_or_invalid', 422, 'Record a shorter reply. X allows 280 weighted characters.');
  }
}
export async function verifyTarget(access: ProviderAccess, sender: Sender, targetId: string, until = Date.now()+50_000): Promise<void> {
  const result = await providerJson(`https://api.x.com/2/tweets/${targetId}?tweet.fields=author_id,entities`, { headers: headers(access) }, 65536, remaining(until, 15_000));
  if (result.status !== 200) throw xFailure(result);
  const post = object(result.data.data);
  if (post.id !== targetId || !eligible(post, sender)) throw new GatewayError('access_denied', 'reply_not_eligible', 403, 'Reply only to a post that explicitly mentions your current X account.');
}
export async function createReply(access: ProviderAccess, input: SendInput, timeout = 15_000): Promise<ProviderResponse> {
  return providerJson('https://api.x.com/2/tweets', {
    method: 'POST', headers: { ...headers(access), 'Content-Type': 'application/json' },
    body: JSON.stringify({ text: input.text, reply: { in_reply_to_tweet_id: input.targetId } }),
  }, 8192, timeout);
}
