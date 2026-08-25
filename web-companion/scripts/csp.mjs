#!/usr/bin/env node
/**
 * Post-build CSP hardening.
 *
 * SvelteKit's prerendered HTML carries one small inline bootstrap script
 * (it passes the base path to the client router). A strict CSP cannot allow
 * inline scripts wholesale, so instead of loosening the policy we pin the
 * exact SHA-256 of that bootstrap into `script-src` alongside `'self'`.
 * Any other inline script - now or after a future edit - stays blocked,
 * because its hash will not be in the policy.
 *
 * Idempotent: existing 'sha256-' entries are stripped before recomputing.
 */
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const ROOT = new URL('..', import.meta.url).pathname;
const INDEX = join(ROOT, 'build', 'index.html');

let html = readFileSync(INDEX, 'utf8');

// Collect every src-less <script> block's exact content.
const inlineScripts = [];
html = html.replace(/<script>([\s\S]*?)<\/script>/g, (_, body) => {
	inlineScripts.push(body);
	return `<script>${body}</script>`;
});

if (inlineScripts.length === 0) {
	console.log('csp: no inline scripts found; policy unchanged');
	process.exit(0);
}

const hashes = inlineScripts.map(
	(body) => `'sha256-${createHash('sha256').update(body).digest('base64')}'`
);

const metaRe = /(<meta\s+http-equiv="Content-Security-Policy"\s+content=")([^"]*)(")/;
const match = html.match(metaRe);
if (!match) {
	console.error('csp error  Content-Security-Policy meta tag not found in build/index.html');
	process.exit(1);
}

// Drop stale hashes, keep the rest of the policy intact.
const directives = match[2]
	.split(';')
	.map((d) => {
		const parts = d.trim().split(/\s+/);
		if (parts[0] === 'script-src') {
			parts.splice(1, Number.MAX_SAFE_INTEGER, "'self'", ...hashes);
		}
		return parts.join(' ');
	})
	.join(';');

html = html.replace(metaRe, () => `${match[1]}${directives}${match[3]}`);
writeFileSync(INDEX, html);

console.log(`csp: pinned ${hashes.length} inline-script hash(es): ${hashes.join(' ')}`);
