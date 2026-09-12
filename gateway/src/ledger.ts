import { DurableObject } from 'cloudflare:workers';
import { providerAccess, ownerName, type DeviceContext, type DevicesIdentity } from './auth';
import { failure, GatewayError, object, utf8, remaining } from './http';
import { createReply, currentSender, isXId, validateReplyText, verifyTarget, xFailure, type SendInput } from './x';

export type Receipt = {
  state: 'pending' | 'unknown' | 'sent' | 'failed'; key: string; targetId: string; senderId: string;
  reply?: { id: string; url: string }; error?: string; message?: string; retryable: false;
};
export type SendRejection = { state: 'idempotency_conflict' | 'rate_limited'; error: string; message: string; retryable: false };
export type SendOutcome = Receipt | SendRejection;
type Row = { key: string; fingerprint: string; sender: string; target: string; state: string; receipt: string; updated: number };
export const AUDIO_DAILY_LIMIT = 40;
export const NEW_RECEIPTS_HOURLY_LIMIT = 60;
export const RETAINED_RECEIPT_LIMIT = 10_000;
export const validKey = (key: unknown): key is string => typeof key === 'string' && /^[A-Za-z0-9_-]{16,80}$/.test(key);
export class ReplyLedger extends DurableObject<Env> {
  constructor(ctx: DurableObjectState, env: Env) {
    super(ctx, env);
    this.ctx.storage.sql.exec('CREATE TABLE IF NOT EXISTS identity (singleton INTEGER PRIMARY KEY CHECK(singleton=1), owner TEXT NOT NULL)');
    this.ctx.storage.sql.exec('CREATE TABLE IF NOT EXISTS receipts (key TEXT PRIMARY KEY, fingerprint TEXT NOT NULL, sender TEXT NOT NULL, target TEXT NOT NULL, state TEXT NOT NULL, receipt TEXT NOT NULL, updated INTEGER NOT NULL)');
    this.ctx.storage.sql.exec('CREATE TABLE IF NOT EXISTS targets (sender TEXT NOT NULL, target TEXT NOT NULL, key TEXT NOT NULL, PRIMARY KEY(sender,target))');
    this.ctx.storage.sql.exec('CREATE TABLE IF NOT EXISTS limits (bucket TEXT PRIMARY KEY, period INTEGER NOT NULL, count INTEGER NOT NULL)');
  }
  private assertOwner(context: DeviceContext) {
    const name = ownerName(context);
    const row = this.ctx.storage.sql.exec<{ owner: string }>('SELECT owner FROM identity WHERE singleton=1').toArray()[0];
    if (row && row.owner !== name) throw new GatewayError('access_denied', 'receipt_owner_mismatch', 403);
    if (!row) this.ctx.storage.sql.exec('INSERT INTO identity(singleton,owner) VALUES(1,?)', name);
  }
  private row(key: string) { return this.ctx.storage.sql.exec<Row>('SELECT * FROM receipts WHERE key=?', key).toArray()[0]; }
  private decode(row: Row): Receipt { return JSON.parse(row.receipt) as Receipt; }
  async permit(context: DeviceContext, bucket: 'inbox' | 'audio' | 'status'): Promise<boolean> {
    this.assertOwner(context);
    if (bucket === 'audio' && !this.consume('audio_daily', AUDIO_DAILY_LIMIT, 86_400_000)) return false;
    return this.consume(bucket, bucket === 'audio' ? 4 : bucket === 'inbox' ? 12 : 60);
  }
  private consume(bucket: string, limit: number, window = 60_000) {
    const period = Math.floor(Date.now() / window);
    const current = this.ctx.storage.sql.exec<{ period: number; count: number }>('SELECT period,count FROM limits WHERE bucket=?', bucket).toArray()[0];
    const count = current?.period === period ? current.count : 0;
    if (count >= limit) return false;
    this.ctx.storage.sql.exec('INSERT INTO limits(bucket,period,count) VALUES(?,?,?) ON CONFLICT(bucket) DO UPDATE SET period=excluded.period,count=excluded.count', bucket, period, count + 1);
    return true;
  }
  async status(context: DeviceContext, key: string): Promise<Receipt | null> {
    this.assertOwner(context);
    if (!validKey(key)) throw new GatewayError('invalid_request', 'invalid_idempotency_key', 400);
    const row = this.row(key); if (!row) return null;
    // An interrupted Worker can leave a persisted pending operation without a
    // final response. Time passage can only make it unknown, never safe to send.
    if (row.state === 'pending' && Date.now() - row.updated > 90_000) {
      const receipt: Receipt = { ...this.decode(row), state: 'unknown', error: 'send_status_unknown', message: 'X has not confirmed whether this reply was sent. Check your X account before taking further action.' };
      this.save(receipt); await this.ctx.storage.sync(); return receipt;
    }
    return this.decode(row);
  }
  private save(receipt: Receipt) {
    this.ctx.storage.sql.exec('UPDATE receipts SET state=?,receipt=?,updated=? WHERE key=?', receipt.state, JSON.stringify(receipt), Date.now(), receipt.key);
    if (receipt.state === 'failed') this.ctx.storage.sql.exec('DELETE FROM targets WHERE sender=? AND target=? AND key=?', receipt.senderId, receipt.targetId, receipt.key);
  }
  async send(context: DeviceContext, bearer: string, input: SendInput, key: string, until = Date.now()+50_000): Promise<SendOutcome> {
    this.assertOwner(context);
    if (!validKey(key)) throw new GatewayError('invalid_request', 'invalid_idempotency_key', 400);
    const digest = await crypto.subtle.digest('SHA-256', utf8.encode(JSON.stringify(input)));
    const fingerprint = Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, '0')).join('');
    const base: Receipt = { state: 'pending', key, targetId: input.targetId, senderId: input.senderId, retryable: false };
    const existing = this.row(key);
    if (existing) {
      if (existing.fingerprint !== fingerprint) return { state: 'idempotency_conflict', error: 'idempotency_conflict', message: 'That send key already belongs to a different reply. Check its status.', retryable: false };
      return (await this.status(context, key))!;
    }
    const retained = this.ctx.storage.sql.exec<{ count: number }>('SELECT count(*) AS count FROM receipts').one().count;
    if (retained >= RETAINED_RECEIPT_LIMIT || !this.consume('admission', NEW_RECEIPTS_HOURLY_LIMIT, 3_600_000)) return { state: 'rate_limited', error: 'receipt_admission_limited', message: 'New replies are blocked. Existing send status is still available.', retryable: false };
    const guarded = this.ctx.storage.sql.exec<{ key: string }>('SELECT key FROM targets WHERE sender=? AND target=?', input.senderId, input.targetId).toArray()[0];
    const limited = !this.consume('send', 6);
    let receipt: Receipt = guarded ? { ...base, state: 'failed', error: 'target_already_handled', message: 'A reply to this post is already sent or awaiting confirmation. Check the previous send status.' } :
      limited ? { ...base, state: 'failed', error: 'send_rate_limited', message: 'Too many reply attempts. Try again later.' } : base;
    this.ctx.storage.transactionSync(() => {
      this.ctx.storage.sql.exec('INSERT INTO receipts(key,fingerprint,sender,target,state,receipt,updated) VALUES(?,?,?,?,?,?,?)', key, fingerprint, input.senderId, input.targetId, receipt.state, JSON.stringify(receipt), Date.now());
      if (receipt.state === 'pending') this.ctx.storage.sql.exec('INSERT INTO targets(sender,target,key) VALUES(?,?,?)', input.senderId, input.targetId, key);
    });
    await this.ctx.storage.sync(); // Persistent receipt/target guard BEFORE any provider POST.
    if (receipt.state === 'failed') return receipt;
    let dispatched = false;
    try {
      validateReplyText(input.text);
      const access = await providerAccess(this.env.DEVICES_IDENTITY as Service<DevicesIdentity>, bearer, 'x:reply', context, until);
      if (access.connectionId !== input.connectionId) throw new GatewayError('access_denied', 'x_connection_changed', 409, 'Your X connection changed. Reload your mentions.');
      const sender = await currentSender(access, until);
      if (sender.id !== input.senderId) throw new GatewayError('access_denied', 'x_sender_changed', 409, 'Your X account changed. Reload your mentions.');
      await verifyTarget(access, sender, input.targetId, until);
      // No new awaits may appear between this marker and the one provider POST.
      // Any failure after this point is unknown unless X definitively rejects it.
      const timeout = remaining(until, 15_000);
      if (timeout < 5000) throw new GatewayError('unavailable', 'preparation_deadline', 503, 'Preparing this reply took too long. Try again.');
      dispatched = true;
      const result = await createReply(access, input, timeout);
      const post = object(result.data.data);
      if (result.status === 201 && isXId(post.id)) {
        receipt = { ...base, state: 'sent', reply: { id: post.id, url: `https://x.com/i/status/${post.id}` } };
      } else if (result.status >= 400 && result.status < 500 && ![408, 409, 425].includes(result.status)) {
        const error = failure(xFailure(result));
        receipt = { ...base, state: 'failed', error: error.error, message: error.message };
      } else receipt = { ...base, state: 'unknown', error: 'send_status_unknown', message: 'X did not confirm this reply. Check its status; do not send it again.' };
    } catch (error) {
      const detail = failure(error);
      receipt = { ...base, state: dispatched ? 'unknown' : 'failed', error: dispatched ? 'send_status_unknown' : detail.error,
        message: dispatched ? 'X may have received this reply. Check its status; do not send it again.' : detail.message };
    }
    this.save(receipt);
    await this.ctx.storage.sync();
    return receipt;
  }
}
