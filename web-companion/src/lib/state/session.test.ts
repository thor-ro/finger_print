// @vitest-environment jsdom
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { cleanup, render, screen } from '@testing-library/svelte';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { session } from '$lib/state/session.svelte';
import { resetSessionForTests } from '$lib/testing/session-reset';
import { FakeTransport } from '$lib/transport/FakeTransport';
import ConfigSection from '$lib/components/ConfigSection.svelte';

// The store is a module singleton: reset it between tests so ordering
// cannot mask a failure.
beforeEach(() => {
	resetSessionForTests();
});
afterEach(() => {
	cleanup();
	vi.useRealTimers();
	vi.unstubAllGlobals();
});

function jsonBytes(value: unknown): Uint8Array {
	return new TextEncoder().encode(JSON.stringify(value));
}

describe('config status separation (8.3)', () => {
	it('a config read reports in configStatus and leaves the OTA status untouched', async () => {
		const t = new FakeTransport();
		session.transport = t;
		t.queueRead('config', jsonBytes({ zigbee_checkin_s: 15 }));
		session.otaStatus = 'pre-existing OTA status';

		await session.readConfig();

		expect(session.configStatus).toBe('Config read successfully');
		expect(session.otaStatus).toBe('pre-existing OTA status');
		expect(session.configVisible).toBe(true);
	});

	it('an applied config reports in configStatus and leaves the OTA status untouched', async () => {
		const t = new FakeTransport();
		session.transport = t;
		session.configEntries = [{ key: 'led_on', value: true, isEditable: true }];
		session.otaStatus = 'pre-existing OTA status';

		await session.applyConfig();

		expect(session.configStatus).toBe('Config applied successfully');
		expect(session.otaStatus).toBe('pre-existing OTA status');
	});

	it('renders with the configuration controls, not in the OTA panel', async () => {
		const t = new FakeTransport();
		session.transport = t;
		t.queueRead('config', jsonBytes({}));
		session.otaStatus = 'pre-existing OTA status';

		await session.readConfig();
		render(ConfigSection);

		expect(screen.getByText('Config read successfully')).toBeTruthy();
		expect(screen.queryByText('pre-existing OTA status')).toBeNull();
	});
});

describe('OTA chunk timeout vs over-MTU rejection (8.6)', () => {
	it('a response timeout resyncs from the device offset instead of re-sending the chunk', async () => {
		vi.useFakeTimers();
		vi.stubGlobal('confirm', () => true);

		const t = new FakeTransport();
		await session.connect(() => t);

		// BEGIN -> ready at 0; the first CHUNK draws no response (its ack is
		// lost, though the device DID write the bytes); the resync BEGIN is
		// answered by the late ack first and then by ready at 180.
		t.scriptWrite('ota', { respond: jsonBytes({ status: 'ready', offset: 0 }) });
		t.scriptWrite('ota', {}); // no response -> timeout
		t.scriptWrite('ota', {
			respond: [
				jsonBytes({ status: 'chunk_ack', offset: 180 }), // stale, must be dropped
				jsonBytes({ status: 'ready', offset: 180 })
			]
		});
		t.scriptWrite('ota', { respond: jsonBytes({ status: 'chunk_ack', offset: 200 }) });
		t.scriptWrite('ota', { respond: jsonBytes({ status: 'success' }) });

		const image = new Uint8Array(200).fill(0xab);
		const transfer = session.startOta(
			new File([image], 'firmware.bin', { type: 'application/octet-stream' })
		);
		await vi.advanceTimersByTimeAsync(10_000);
		await transfer;

		const writes = t.written.filter((w) => w.characteristic === 'ota');
		// BEGIN(5), CHUNK(1+180), BEGIN(5) again, CHUNK(1+20), END(1).
		expect(writes.map((w) => w.data[0])).toEqual([1, 2, 1, 2, 3]);
		// The recovery is a BEGIN, not the same chunk sent twice: a CHUNK
		// carries no offset, so re-sending would append 180 duplicate bytes.
		expect(writes[2].data.length).toBe(5);
		// The device said it holds 180 of 200 bytes, so only 20 remain - and
		// the chunk size was NOT halved on the way (that is the over-MTU path).
		expect(writes[3].data.length).toBe(21);
		expect(session.otaStatus).toContain('OTA transfer complete');
	});

	it('a stale chunk acknowledgement is not mistaken for the resync response', async () => {
		vi.useFakeTimers();
		vi.stubGlobal('confirm', () => true);

		const t = new FakeTransport();
		await session.connect(() => t);

		t.scriptWrite('ota', { respond: jsonBytes({ status: 'ready', offset: 0 }) });
		t.scriptWrite('ota', {}); // timeout
		// Only the stale ack comes back: the resync must keep waiting for a
		// `ready` rather than resuming from a chunk acknowledgement, so this
		// BEGIN times out too and the transfer reports the failure.
		t.scriptWrite('ota', { respond: jsonBytes({ status: 'chunk_ack', offset: 180 }) });

		const image = new Uint8Array(200).fill(0xab);
		const transfer = session.startOta(
			new File([image], 'firmware.bin', { type: 'application/octet-stream' })
		);
		await vi.advanceTimersByTimeAsync(60_000);
		await transfer;

		const writes = t.written.filter((w) => w.characteristic === 'ota');
		// No further CHUNK was written after the unanswered resync.
		expect(writes.filter((w) => w.data[0] === 2)).toHaveLength(1);
		expect(session.otaStatus).toMatch(/Error/);
	});

	it('a rejected (non-timeout) chunk write still halves the chunk size', async () => {
		vi.useFakeTimers();
		vi.stubGlobal('confirm', () => true);

		const t = new FakeTransport();
		await session.connect(() => t);

		t.scriptWrite('ota', { respond: jsonBytes({ status: 'ready', offset: 0 }) });
		t.scriptWrite('ota', { fail: new Error('att write rejected') }); // over-MTU
		t.scriptWrite('ota', { respond: jsonBytes({ status: 'chunk_ack', offset: 200 }) });
		t.scriptWrite('ota', { respond: jsonBytes({ status: 'success' }) });

		const image = new Uint8Array(200).fill(0xab);
		const transfer = session.startOta(
			new File([image], 'firmware.bin', { type: 'application/octet-stream' })
		);
		await vi.advanceTimersByTimeAsync(100);
		await transfer;

		const chunkWrites = t.written.filter((w) => w.characteristic === 'ota');
		expect(chunkWrites).toHaveLength(4);
		// First chunk 1+180, retry after halving is 1+90 - no BEGIN in between.
		expect(chunkWrites[1].data.length).toBe(181);
		expect(chunkWrites[2].data.length).toBe(91);
	});
});

describe('dashboard import failure (8.5)', () => {
	it('a failed deferred import offers a reload, not a dead in-place retry', () => {
		// A browser caches a failed module fetch, so re-importing the same
		// specifier fails without a network request - the recovery has to be a
		// reload. The route file cannot be mounted standalone here (it needs
		// full Kit context), so pin the handler at source level.
		const page = readFileSync(
			resolve(dirname(fileURLToPath(import.meta.url)), '../../routes/+page.svelte'),
			'utf8'
		);
		expect(page).toMatch(/\{:catch\}/);
		expect(page).toMatch(/location\.reload\(\)/);
	});
});

describe('test isolation covers the whole store', () => {
	it('resetSessionForTests resets every field the store declares', () => {
		const here = dirname(fileURLToPath(import.meta.url));
		const store = readFileSync(resolve(here, './session.svelte.ts'), 'utf8');
		const reset = readFileSync(resolve(here, '../testing/session-reset.ts'), 'utf8');

		const classBody = store.slice(
			store.indexOf('class SessionStore {'),
			store.indexOf('export const session =')
		);
		const declared = [...classBody.matchAll(/^\t(?:private\s+)?(\w+)\s*[:=]/gm)].map((m) => m[1]);
		const cleared = new Set([...reset.matchAll(/^\ts\.(\w+)/gm)].map((m) => m[1]));

		// A field added to the store without a line here would leak between
		// tests, which is exactly what the reset exists to prevent.
		expect(declared.filter((name) => !cleared.has(name))).toEqual([]);
	});
});
