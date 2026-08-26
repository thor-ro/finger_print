#!/usr/bin/env node
/**
 * Bundle-budget gate.
 *
 * Measures two things from the BUILT output (compressed with gzip -9):
 *
 *   initial load  - build/index.html plus every script/stylesheet it
 *                   references on first load (including modulepreload)
 *   total load    - the initial load plus every other immutable JS/CSS
 *                   asset in build/_app, i.e. everything that loads on the
 *                   way to the last view (the lazily imported dashboard
 *                   included). Declared separately so weight moved behind a
 *                   deferred import is still measured, not hidden.
 *
 * Limits are declared in budget.json. Prints measured-vs-budget either way
 * and fails on overrun. The budget NEVER rises: a new measurement can only
 * re-declare min(measured + headroom, previous limit) - see design.md.
 */
import { gzipSync } from 'node:zlib';
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs';
import { join, resolve } from 'node:path';
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
const initialLimit = Number(budget.initialLoadGzipBytes);
const totalLimit = Number(budget.totalLoadGzipBytes);
for (const [name, value] of [
	['initialLoadGzipBytes', initialLimit],
	['totalLoadGzipBytes', totalLimit]
]) {
	if (!Number.isFinite(value) || value <= 0) {
		console.error(`budget error  budget.json must declare a positive ${name}`);
		process.exit(1);
	}
}

function gzipSize(file) {
	return gzipSync(readFileSync(file), { level: 9 }).length;
}

function walk(dir) {
	const entries = [];
	for (const name of readdirSync(dir)) {
		const full = join(dir, name);
		if (statSync(full).isDirectory()) entries.push(...walk(full));
		else entries.push(full);
	}
	return entries;
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
	initialAssets.add(resolve(BUILD_DIR, clean));
}

// Everything else the app can load later (the deferred dashboard chunk and
// its dependencies): immutable JS/CSS not referenced by the entry document.
const deferredAssets = walk(join(BUILD_DIR, '_app')).filter(
	(f) => /\.(js|mjs|css)$/.test(f) && !initialAssets.has(f)
);

function report(label, files) {
	let total = 0;
	console.log(`${label} (gzip):`);
	for (const file of [...files].sort()) {
		const size = gzipSize(file);
		total += size;
		console.log(`  ${String(size).padStart(7)} B  ${file.replace(BUILD_DIR + '/', '')}`);
	}
	return total;
}

const initialSize = report('Initial-load assets', initialAssets);
const deferredSize = report('Deferred assets', deferredAssets);
const totalSize = initialSize + deferredSize;

const kb = (n) => `${(n / 1024).toFixed(1)} KB`;
console.log(
	`\ninitial:  ${kb(initialSize)} measured / ${kb(initialLimit)} budget` +
		`\ntotal:    ${kb(totalSize)} measured / ${kb(totalLimit)} budget` +
		`\n(deferred beyond the initial load: ${kb(deferredSize)})`
);

let failed = false;
if (initialSize > initialLimit) {
	console.error(`initial-load budget EXCEEDED by ${initialSize - initialLimit} bytes`);
	failed = true;
}
if (totalSize > totalLimit) {
	console.error(`total-load budget EXCEEDED by ${totalSize - totalLimit} bytes`);
	failed = true;
}
if (failed) process.exit(1);
console.log('budget OK');
