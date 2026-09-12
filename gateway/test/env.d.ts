import type {} from '@cloudflare/vitest-plugin/types';
declare module 'cloudflare:test' { interface ProvidedEnv extends Env {} }
