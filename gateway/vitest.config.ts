import { cloudflareTest } from '@cloudflare/vitest-plugin';
import { defineConfig } from 'vitest/config';
import { readFileSync } from 'node:fs';

export default defineConfig({
  plugins: [cloudflareTest({
    wrangler: { configPath: './wrangler.jsonc' },
    remoteBindings: false,
    miniflare: {
      serviceBindings: { DEVICES_IDENTITY: { name: 'test-identity', entrypoint: 'DevicesIdentity' } },
      workers: [{ name: 'test-identity', modules: true,
        compatibilityDate: '2026-09-11', compatibilityFlags: ['nodejs_compat'],
        script: readFileSync(new URL('./test/fake-identity.js', import.meta.url), 'utf8') }],
    },
  })],
  test: { include: ['test/**/*.test.ts'], testTimeout: 20_000 },
});
