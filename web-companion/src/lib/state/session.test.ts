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
	it('a response timeout retries the same offset without halving the chunk size', async () => {
		vi.useFakeTimers();
		vi.stubGlobal('confirm', () => true);

		const t = new FakeTransport();
		await session.connect(() => t);

		// BEGIN -> ready at offset 0; first CHUNK gets NO response (timeout);
		// the retry is acknowledged having consumed the whole 200-byte image;
		// END -> success.
		t.scriptWrite('ota', { respond: jsonBytes({ status: 'ready', offset: 0 }) });
		t.scriptWrite('ota', {}); // timeout
		t.scriptWrite('ota', { respond: jsonBytes({ status: 'chunk_ack', offset: 200 }) });
		t.scriptWrite('ota', { respond: jsonBytes({ status: 'success' }) });

		const image = new Uint8Array(200).fill(0xab);
		const transfer = session.startOta(new File([image], 'firmware.bin', { type: 'application/octet-stream' }));

		// The 10 s response timeout fires, the same chunk is retried, the
		// retry is acknowledged, and END completes - all within this advance.
		await vi.advanceTimersByTimeAsync(10_000);
		await transfer;

		// Chunk payloads: BEGIN(5) + CHUNK(1+180) twice + END(1). The retried
		// chunk is the SAME SIZE at the SAME offset - the timeout neither
		// halved the chunk size nor advanced past the unconfirmed data.
		const chunkWrites = t.written.filter((w) => w.characteristic === 'ota');
		expect(chunkWrites).toHaveLength(4);
		expect(chunkWrites[1].data.length).toBe(181);
		expect(chunkWrites[2].data.length).toBe(181);
		expect([...chunkWrites[1].data]).toEqual([...chunkWrites[2].data]);
		expect(chunkWrites[3].data.length).toBe(1);
		expect(session.otaStatus).toContain('OTA transfer complete');
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
		const transfer = session.startOta(new File([image], 'firmware.bin', { type: 'application/octet-stream' }));
		await vi.advanceTimersByTimeAsync(100);
		await transfer;

		const chunkWrites = t.written.filter((w) => w.characteristic === 'ota');
		expect(chunkWrites).toHaveLength(4);
		// First chunk 1+180, retry after halving is 1+90.
		expect(chunkWrites[1].data.length).toBe(181);
		expect(chunkWrites[2].data.length).toBe(91);
	});
});

describe('dashboard import failure (8.5)', () => {
	it('the deferred dashboard import has a {:catch} handler with a retry', () => {
		// A rejected dynamic import must not render an empty pane. The route
		// file cannot be mounted standalone here (full Kit context), so pin
		// the handler's presence at source level - lint-style.
		const page = readFileSync(resolve(dirname(fileURLToPath(import.meta.url)), '../../routes/+page.svelte'), 'utf8');
		expect(page).toMatch(/\{:catch\}/);
		expect(page).toMatch(/dashboardAttempt\+\+/);
	});
});
