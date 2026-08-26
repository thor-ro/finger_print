#!/usr/bin/env node
/**
 * Interactive runner for the hardware parity suite
 * (openspec/changes/web-companion-tooling tasks 6.1 - 6.8).
 *
 *   node hardware-tests/run.mjs            # run the suite interactively
 *   node hardware-tests/run.mjs --verify   # gate: exit non-zero unless every
 *                                          # case has a recorded PASS
 *   node hardware-tests/run.mjs --list     # print the cases, run nothing
 *   node hardware-tests/run.mjs --reset    # discard recorded verdicts
 *
 * Verdicts are stored in hardware-tests/results.json; the completed file is
 * the parity record required by task 6.9 (rendered to parity-results.md by
 * --verify once all cases pass).
 */
import { createInterface } from 'node:readline/promises';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import process from 'node:process';
import { cases } from './cases.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const RESULTS = join(HERE, 'results.json');
const REPORT = join(HERE, 'parity-results.md');

function load() {
	if (!existsSync(RESULTS)) return { recorded: new Date(0).toISOString(), tester: '', firmware: '', verdicts: {} };
	return JSON.parse(readFileSync(RESULTS, 'utf8'));
}

function save(results) {
	writeFileSync(RESULTS, JSON.stringify(results, null, '\t') + '\n');
}

function renderReport(results) {
	const lines = [
		'# Hardware Parity Results — web-companion-tooling (task 6.9)',
		'',
		`- Recorded: ${results.recorded}`,
		`- Tester: ${results.tester || '(unnamed)'}`,
		`- Device firmware: ${results.firmware || '(not recorded)'}`,
		`- App under test: ${results.appUrl || '(not recorded)'}`,
		'',
		'| Case | Task | Title | Verdict | Notes |',
		'|---|---|---|---|---|'
	];
	let pass = 0;
	for (const c of cases) {
		const v = results.verdicts[c.id] ?? { verdict: 'NOT RUN', notes: '' };
		if (v.verdict === 'PASS') pass++;
		lines.push(`| ${c.id} | ${c.task} | ${c.title} | ${v.verdict} | ${v.notes || ''} |`);
	}
	lines.push('', `**${pass}/${cases.length} cases PASS.**`);
	return lines.join('\n') + '\n';
}

/**
 * Prompt helper. With a TTY it delegates to readline's question(); with
 * piped input (scripted runs, CI dry-runs) readline's line events for
 * already-buffered input fire before the next question() registers, so we
 * pre-read every line and consume them in order instead.
 */
class Prompt {
	constructor(rl) {
		this.rl = rl;
		this.tty = process.stdin.isTTY;
		this.buffered = [];
	}

	async preload() {
		if (!this.tty) {
			for await (const line of this.rl) this.buffered.push(line.trim());
		}
	}

	async ask(text) {
		if (this.tty) return (await this.rl.question(text)).trim();
		const value = this.buffered.shift() ?? '';
		console.log(`${text}${value}`);
		return value;
	}
}

async function runInteractive() {
	const results = load();
	const rl = createInterface({ input: process.stdin, output: process.stdout });
	const prompt = new Prompt(rl);
	await prompt.preload();

	if (!results.tester) {
		results.tester = await prompt.ask('Tester name: ');
	}
	if (!results.firmware) {
		results.firmware = await prompt.ask('Device firmware version (e.g. from `ota version`): ');
	}
	if (!results.appUrl) {
		results.appUrl =
			(await prompt.ask('App URL under test: ')).trim() ||
			'https://thor-ro.github.io/finger_print/';
	}

	for (const c of cases) {
		const prev = results.verdicts[c.id];
		if (prev?.verdict === 'PASS') {
			console.log(`\n=== ${c.id} ${c.title} — already PASS, skipping (delete results.json to redo)`);
			continue;
		}
		console.log(`\n=== ${c.id}  (task ${c.task})  ${c.title}`);
		console.log('Preconditions:');
		for (const p of c.preconditions) console.log(`  ? ${p}`);
		console.log('Steps:');
		c.steps.forEach((s, i) => console.log(`  ${i + 1}. ${s}`));
		console.log('Expected:');
		for (const e of c.expected) console.log(`  ✓ ${e}`);

		let verdict = '';
		while (!['p', 'f', 's'].includes(verdict)) {
			verdict = (await prompt.ask('Verdict — (p)ass / (f)ail / (s)kip: ')).toLowerCase();
		}
		const notes = await prompt.ask('Notes (optional): ');
		results.verdicts[c.id] = {
			verdict: verdict === 'p' ? 'PASS' : verdict === 'f' ? 'FAIL' : 'SKIPPED',
			notes,
			at: new Date().toISOString()
		};
		save(results);
	}

	rl.close();
	results.recorded = new Date().toISOString();
	save(results);
	console.log(`\nResults written to ${RESULTS}`);
}

function verify() {
	if (!existsSync(RESULTS)) {
		console.error(`verify FAILED: ${RESULTS} does not exist - run the suite first (npm run hw)`);
		process.exit(1);
	}
	const results = load();
	const missing = cases.filter((c) => results.verdicts[c.id]?.verdict !== 'PASS');
	if (missing.length > 0) {
		for (const c of missing) {
			const v = results.verdicts[c.id];
			console.error(`verify FAILED: ${c.id} is ${v ? v.verdict : 'NOT RUN'} — ${c.title}`);
		}
		process.exit(1);
	}
	writeFileSync(REPORT, renderReport(results));
	console.log(`verify OK: all ${cases.length} hardware cases PASS`);
	console.log(`parity record written to ${REPORT}`);
}

if (process.argv.includes('--verify')) {
	verify();
} else if (process.argv.includes('--list')) {
	for (const c of cases) {
		console.log(`${c.id}  (task ${c.task})  ${c.title}`);
	}
} else if (process.argv.includes('--reset')) {
	if (existsSync(RESULTS)) {
		writeFileSync(RESULTS, JSON.stringify({ recorded: new Date(0).toISOString(), tester: '', firmware: '', appUrl: '', verdicts: {} }, null, '\t') + '\n');
	}
	console.log('verdicts reset');
} else {
	await runInteractive();
}
