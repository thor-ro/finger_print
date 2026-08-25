import { sveltekit } from '@sveltejs/kit/vite';
import { defineConfig } from 'vite';

export default defineConfig({
	plugins: [sveltekit()],
	test: {
		include: ['src/**/*.test.ts'],
		environment: 'node'
	},
	// Under Vitest, resolve Svelte to the client build so component tests can
	// mount against jsdom (files opt into jsdom with a per-file annotation).
	resolve: process.env.VITEST ? { conditions: ['browser'] } : undefined
});
