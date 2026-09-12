import { expect, vi } from 'vitest';
type Options = { path: string; method?: string; headers?: Record<string,string>; body?: string };
type Pending = { origin: string; options: Options; run: (request: Request) => Promise<Response> };
const pending: Pending[] = [];
export const fetchMock = {
  activate() {
    vi.stubGlobal('fetch', async (input: RequestInfo | URL, init?: RequestInit) => {
      const request = new Request(input, init); const url = new URL(request.url);
      const index = pending.findIndex(entry => entry.origin === url.origin && entry.options.path === url.pathname + url.search && (entry.options.method ?? 'GET') === request.method);
      if (index < 0) throw new Error(`Unexpected provider request: ${request.method} ${url.origin}${url.pathname}`);
      const entry = pending.splice(index, 1)[0];
      expect(init?.redirect).toBe('manual');
      for (const [name,value] of Object.entries(entry.options.headers ?? {})) expect(request.headers.get(name)).toBe(value);
      if (entry.options.body !== undefined) expect(await request.clone().text()).toBe(entry.options.body);
      return entry.run(request);
    });
  },
  deactivate() { vi.unstubAllGlobals(); pending.length=0; },
  disableNetConnect() {}, // The stub above has no fallback to the real network.
  assertNoPendingInterceptors() { expect(pending.map(p=>`${p.options.method??'GET'} ${p.origin}${p.options.path}`)).toEqual([]); },
  get(origin: string) {
    return { intercept(options: Options) { return {
      reply(status: number, data: unknown, extra: { headers?: Record<string,string> } = {}) {
        pending.push({origin,options,run:async()=>new Response(typeof data === 'string' ? data : JSON.stringify(data),{status,headers:extra.headers})});
      },
      replyWithError(error: Error) { pending.push({origin,options,run:async()=>{throw error;}}); },
      replyWith(run: (request: Request) => Promise<Response>) { pending.push({origin,options,run}); },
    }; } };
  },
};
