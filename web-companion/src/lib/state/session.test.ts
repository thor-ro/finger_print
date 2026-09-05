// @vitest-environment jsdom
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { cleanup, render, screen } from '@testing-library/svelte';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import * as setup from '$lib/protocol/setup';
import { session } from '$lib/state/session.svelte';
import { resetSessionForTests } from '$lib/testing/session-reset';
import { FakeTransport } from '$lib/transport/FakeTransport';
import ConfigSection from '$lib/components/ConfigSection.svelte';
import WizardView from '$lib/components/WizardView.svelte';

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

describe('wizard asks for one fingerprint scan at a time', () => {
	async function connectInSetupPhase(t: FakeTransport): Promise<void> {
		t.queueRead('setup_state', new Uint8Array([0])); // SETUP_NOT_STARTED
		await session.connect(() => t);
	}

	function progress(captured: number, step: number): Uint8Array {
		return jsonBytes({ status: 'progress', captured, step, total: 3 });
	}

	async function startAdminEnrolment(t: FakeTransport): Promise<void> {
		t.scriptWrite('enroll', { respond: jsonBytes({ req: 1, result: 'ok' }) });
		await session.wizardEnrollAdmin();
	}

	it('asks for the first scan by name with nothing captured yet', async () => {
		const t = new FakeTransport();
		await connectInSetupPhase(t);
		await startAdminEnrolment(t);

		expect(session.view).toBe('wizard');
		expect(session.wizardEnrollProgressVisible).toBe(true);
		expect(session.wizardEnrollCaptured).toBe(0);
		expect(session.wizardEnrollExpected).toBe(1);
	});

	it('advances the prompt on each scan the device reports captured', async () => {
		const t = new FakeTransport();
		await connectInSetupPhase(t);
		await startAdminEnrolment(t);

		t.notify('enroll', progress(1, 2));
		expect(session.wizardEnrollCaptured).toBe(1);
		expect(session.wizardEnrollExpected).toBe(2);

		t.notify('enroll', progress(2, 3));
		expect(session.wizardEnrollCaptured).toBe(2);
		expect(session.wizardEnrollExpected).toBe(3);
	});

	it('does not advance without the device saying so', async () => {
		const t = new FakeTransport();
		await connectInSetupPhase(t);
		await startAdminEnrolment(t);

		vi.useFakeTimers();
		await vi.advanceTimersByTimeAsync(30_000);

		expect(session.wizardEnrollCaptured).toBe(0);
		expect(session.wizardEnrollExpected).toBe(1);
	});

	it('confirms success on the step itself instead of jumping away', async () => {
		const t = new FakeTransport();
		await connectInSetupPhase(t);
		await startAdminEnrolment(t);
		t.notify('enroll', progress(1, 2));
		t.notify('enroll', progress(2, 3));

		t.notify('enroll', jsonBytes({ status: 'success', user_id: 1 }));

		expect(session.wizardEnrollPhase).toBe('success');
		expect(session.wizardEnrollCaptured).toBe(session.wizardEnrollTotal);
		expect(session.wizardEnrollProgressVisible).toBe(false);
		// The confirmation is the point: advancing is the user's own click.
		expect(session.wizardStep).toBe('enroll');
	});

	it('moves on only when the user confirms', async () => {
		const t = new FakeTransport();
		await connectInSetupPhase(t);
		await startAdminEnrolment(t);
		t.notify('enroll', jsonBytes({ status: 'success', user_id: 1 }));

		session.wizardEnrollContinue();

		expect(session.wizardStep).toBe('register');
		expect(session.wizardEnrollPhase).toBe('idle');
	});

	it('stops asking for a scan when the enrolment fails', async () => {
		const t = new FakeTransport();
		await connectInSetupPhase(t);
		await startAdminEnrolment(t);
		t.notify('enroll', progress(1, 2));

		t.notify('enroll', jsonBytes({ status: 'failed', step: 2, error_code: 1 }));

		expect(session.wizardEnrollProgressVisible).toBe(false);
		expect(session.wizardEnrollExpected).toBe(0);
		expect(session.wizardEnrollPhase).toBe('failed');
		expect(session.wizardEnrollError).toContain('failed at scan 2');
		expect(session.wizardEnrollHint).toBeTruthy();
		expect(session.wizardStep).toBe('enroll');
	});

	it('a progress notification does not resolve the request that started it', async () => {
		const t = new FakeTransport();
		await connectInSetupPhase(t);
		t.scriptWrite('enroll', {
			respond: [progress(0, 1), jsonBytes({ req: 1, result: 'ok' })]
		});

		await session.wizardEnrollAdmin();

		// The terminal reply still arrived and was the one acted on.
		expect(session.wizardEnrollProgressVisible).toBe(true);
	});

	it('states the scan count when the device reports no progress at all', async () => {
		const t = new FakeTransport();
		await connectInSetupPhase(t);
		await startAdminEnrolment(t);

		expect(session.wizardEnrollProgressVisible).toBe(true);
		expect(session.wizardEnrollTotal).toBe(3);
		expect(session.wizardEnrollExpected).toBe(1);
	});
});

describe('dashboard enrolment shares the per-scan prompting', () => {
	it('tracks captured scans from the device notifications', async () => {
		const t = new FakeTransport();
		await session.connect(() => t);
		t.scriptWrite('enroll', { respond: jsonBytes({ req: 1, result: 'ok' }) });

		await session.enroll(2, 1);
		expect(session.enrollExpected).toBe(1);

		t.notify('enroll', jsonBytes({ status: 'progress', captured: 1, step: 2, total: 3 }));

		expect(session.enrollCaptured).toBe(1);
		expect(session.enrollExpected).toBe(2);
		expect(session.enrollMessage).toContain('scan 2 of 3');
	});

	it('does not expect a scan of the new user before the admin authorizes', async () => {
		vi.useFakeTimers();
		const t = new FakeTransport();
		await session.connect(() => t);
		t.scriptWrite('enroll', {}); // no reply yet: still waiting on the admin

		const pending = session.enroll(2, 1);
		expect(session.enrollExpected).toBe(0);
		expect(session.enrollMessage).toContain('authorizing Admin scan');

		// Let the client-side reply timeout lapse so nothing is left pending.
		await vi.advanceTimersByTimeAsync(20_000);
		await pending;
	});
});

describe('a refused enrolment write is reported, not swallowed', () => {
	async function connectInSetupPhase(t: FakeTransport): Promise<void> {
		t.queueRead('setup_state', new Uint8Array([0])); // SETUP_NOT_STARTED
		await session.connect(() => t);
	}

	it('replaces the opening status when the device rejects the write', async () => {
		const t = new FakeTransport();
		await connectInSetupPhase(t);
		// What a lapsed setup phase looks like from the browser: the device
		// answers the write with an ATT error instead of a reply.
		t.scriptWrite('enroll', { fail: new Error('GATT operation not authorized.') });

		await expect(session.wizardEnrollAdmin()).resolves.toBeUndefined();

		expect(session.wizardEnrollPhase).toBe('failed');
		expect(session.wizardEnrollError).toContain('GATT operation not authorized.');
		expect(session.wizardEnrollHint).toContain('re-arm setup');
		expect(session.wizardEnrollProgressVisible).toBe(false);
	});
});

describe('setup deadline countdown', () => {
	it('runs while the wizard is open and counts down in real time', async () => {
		vi.useFakeTimers();
		const t = new FakeTransport();
		t.queueRead('setup_state', new Uint8Array([0]));
		await session.connect(() => t);

		expect(session.wizardDeadlineRemainingMs).toBe(setup.SETUP_DEADLINE_MS);

		await vi.advanceTimersByTimeAsync(60_000);
		expect(session.wizardDeadlineRemainingMs).toBe(setup.SETUP_DEADLINE_MS - 60_000);
	});

	it('floors at zero and stops rather than going negative', async () => {
		vi.useFakeTimers();
		const t = new FakeTransport();
		t.queueRead('setup_state', new Uint8Array([0]));
		await session.connect(() => t);

		await vi.advanceTimersByTimeAsync(setup.SETUP_DEADLINE_MS + 30_000);

		expect(session.wizardDeadlineRemainingMs).toBe(0);
	});

	it('does not run for a device that is past setup', async () => {
		const t = new FakeTransport();
		t.queueRead('setup_state', new Uint8Array([setup.SETUP_COMPLETE]));
		await session.connect(() => t);

		expect(session.view).not.toBe('wizard');
		expect(session.wizardDeadlineRemainingMs).toBeNull();
	});

	it('clears on disconnect', async () => {
		vi.useFakeTimers();
		const t = new FakeTransport();
		t.queueRead('setup_state', new Uint8Array([0]));
		await session.connect(() => t);
		expect(session.wizardDeadlineRemainingMs).not.toBeNull();

		t.simulateDisconnect();

		expect(session.wizardDeadlineRemainingMs).toBeNull();
	});
});

describe('the enrolment step shows one instruction per phase', () => {
	async function inWizard(t: FakeTransport): Promise<void> {
		t.queueRead('setup_state', new Uint8Array([0]));
		await session.connect(() => t);
	}

	it('idle offers a single start button and explains the finger', async () => {
		const t = new FakeTransport();
		await inWizard(t);
		render(WizardView);

		expect(session.wizardEnrollPhase).toBe('idle');
		expect(screen.getByRole('button', { name: /start enrolment/i })).toBeTruthy();
		expect(screen.getByText(/same finger/i)).toBeTruthy();
	});

	it('tells the user to place the finger while the device waits for scan 1', async () => {
		const t = new FakeTransport();
		await inWizard(t);
		t.scriptWrite('enroll', { respond: jsonBytes({ req: 1, result: 'ok' }) });
		await session.wizardEnrollAdmin();
		render(WizardView);

		expect(session.wizardEnrollPhase).toBe('scanning');
		expect(screen.getByText(/place the admin's finger on the sensor/i)).toBeTruthy();
		// The count also appears in ScanProgress's screen-reader labels, so
		// match the visible sub-line specifically.
		expect(screen.getByText(/scan 1 of 3\s+· hold still/i)).toBeTruthy();
	});

	it('tells the user to lift and place again once a scan is captured', async () => {
		const t = new FakeTransport();
		await inWizard(t);
		t.scriptWrite('enroll', { respond: jsonBytes({ req: 1, result: 'ok' }) });
		await session.wizardEnrollAdmin();
		t.notify('enroll', jsonBytes({ status: 'progress', captured: 1, step: 2, total: 3 }));
		render(WizardView);

		expect(screen.getByText(/lift your finger, then place it again/i)).toBeTruthy();
		expect(screen.getByText(/scan 2 of 3\s+· hold still/i)).toBeTruthy();
	});

	it('states success and waits for the user to continue', async () => {
		const t = new FakeTransport();
		await inWizard(t);
		t.scriptWrite('enroll', { respond: jsonBytes({ req: 1, result: 'ok' }) });
		await session.wizardEnrollAdmin();
		t.notify('enroll', jsonBytes({ status: 'success', user_id: 1 }));
		render(WizardView);

		expect(screen.getByText(/admin fingerprint enrolled/i)).toBeTruthy();
		expect(screen.getByRole('button', { name: /continue to account registration/i })).toBeTruthy();
	});

	it('states the failure and offers a retry', async () => {
		const t = new FakeTransport();
		await inWizard(t);
		t.scriptWrite('enroll', { respond: jsonBytes({ req: 1, result: 'ok' }) });
		await session.wizardEnrollAdmin();
		t.notify('enroll', jsonBytes({ status: 'failed', step: 2, error_code: 1 }));
		render(WizardView);

		expect(screen.getByText(/that did not work/i)).toBeTruthy();
		expect(screen.getByText(/failed at scan 2 of 3/i)).toBeTruthy();
		expect(screen.getByRole('button', { name: /try again/i })).toBeTruthy();
	});
});

describe('a rejected write is reported by every user-management caller', () => {
	const refused = () => new Error('GATT operation not permitted.');

	it('listUsers reports the refusal instead of rejecting', async () => {
		const t = new FakeTransport();
		await session.connect(() => t);
		t.scriptWrite('enroll', { fail: refused() });

		await expect(session.listUsers()).resolves.toBe('error');

		expect(session.umStatus).toContain('GATT operation not permitted.');
		expect(session.umStatusRefusal).toBe(true);
	});

	it('the dashboard enrolment clears its progress and states the refusal', async () => {
		const t = new FakeTransport();
		await session.connect(() => t);
		t.scriptWrite('enroll', { fail: refused() });

		await expect(session.enroll(2, 1)).resolves.toBe(false);

		expect(session.enrollProgressVisible).toBe(false);
		expect(session.enrollExpected).toBe(0);
		expect(session.enrollResultText).toContain('GATT operation not permitted.');
	});

	it('a silent refresh does not overwrite the outcome its caller reported', async () => {
		const t = new FakeTransport();
		await session.connect(() => t);
		// The rename itself succeeds; the refresh that follows is refused.
		t.scriptWrite('enroll', { respond: jsonBytes({ req: 1, result: 'ok' }) });
		t.scriptWrite('enroll', { fail: refused() });
		vi.stubGlobal('prompt', () => 'Bob');

		await session.renameUser(2, 'Alice');

		expect(session.umStatus).toContain('renamed');
		expect(session.umStatus).not.toContain('GATT operation not permitted.');
	});
});
