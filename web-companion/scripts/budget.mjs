#!/usr/bin/env node
/**
 * Bundle-budget gate.
 *
 * Measures the compressed initial load of the BUILT output (the assets the
 * browser downloads on first load: the entry HTML plus every script and
 * stylesheet it references) and compares against the budget declared in
 * budget.json. Prints measured-vs-budget either way and fails on overrun.
 */
import { gzipSync } from 'node:zlib';
import { existsSync, readFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import process from 'node:process';

const ROOT = new URL('..', import.meta.url).pathname;
const BUILD_DIR = join(ROOT, 'build');
const BUDGET_FILE = join(ROOT, 'budget.json');

if (!existsSync(BUDGET_FILE)) {
	console.error(`budget error  ${BUDGET_FILE} not found`);
	process.exit(1);
}
if (!existsSync(join(BUILD_DIR, 'index.html'))) {
	console.error(
		'budget error  build/index.html not found - run `npm run build` before checking the budget'
	);
	process.exit(1);
}

const budget = JSON.parse(readFileSync(BUDGET_FILE, 'utf8'));
const limitBytes = Number(budget.initialLoadGzipBytes);
if (!Number.isFinite(limitBytes) || limitBytes <= 0) {
	console.error('budget error  budget.json must declare a positive initialLoadGzipBytes');
	process.exit(1);
}

function gzipSize(file) {
	return gzipSync(readFileSync(file), { level: 9 }).length;
}

const htmlPath = join(BUILD_DIR, 'index.html');
const html = readFileSync(htmlPath, 'utf8');
const initialAssets = new Set([htmlPath]);

// Every script and stylesheet the entry document pulls on first load,
// including modulepreload hints. URLs may be "./"-prefixed or root-absolute
// (carrying kit.paths.base on a deployed build); both resolve under the
// build directory.
for (const [, url] of html.matchAll(/(?:src|href)="([^"]+\.(?:js|mjs|css))"/g)) {
	const clean = url.split('?')[0].split('#')[0].replace(/^(\.?\/)+/, '');
	const file = resolve(BUILD_DIR, clean);
	initialAssets.add(file);
}

let total = 0;
console.log('Initial-load assets (gzip):');
for (const file of [...initialAssets].sort()) {
	const size = gzipSize(file);
	total += size;
	console.log(`  ${String(size).padStart(7)} B  ${file.replace(BUILD_DIR + '/', '')}`);
}

const kb = (n) => `${(n / 1024).toFixed(1)} KB`;
console.log(`\nbudget: ${kb(limitBytes)}   measured: ${kb(total)} (${total} bytes)`);

if (total > limitBytes) {
	console.error(`budget EXCEEDED by ${total - limitBytes} bytes`);
	process.exit(1);
}
console.log('budget OK');
