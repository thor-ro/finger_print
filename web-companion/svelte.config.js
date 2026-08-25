import adapter from '@sveltejs/adapter-static';
import { vitePreprocess } from '@sveltejs/vite-plugin-svelte';

/** @type {import('@sveltejs/kit').Config} */
const config = {
	preprocess: vitePreprocess(),
	kit: {
		adapter: adapter({
			pages: 'build',
			assets: 'build',
			fallback: undefined,
			precompress: false,
			strict: true
		}),
		paths: {
			// Project-site subpath for the deployed build (e.g. /finger_print),
			// empty for local development. Driven by an environment variable so
			// a fork with a different repository name needs no source edit:
			//   BASE_PATH=/finger_print npm run build
			base: process.env.BASE_PATH || ''
		}
	}
};

export default config;
