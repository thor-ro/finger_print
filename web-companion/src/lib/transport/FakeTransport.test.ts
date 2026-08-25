import { describe, expect, it } from 'vitest';
import { FakeTransport } from './FakeTransport';

function bytes(s: string): Uint8Array {
	return new TextEncoder().encode(s);
}

describe('FakeTransport', () => {
	it('replays scripted responses to writes', async () => {
		const t = new FakeTransport();
		await t.connect();
		t.scriptWrite('auth', { respond: bytes('{"ok":true}') });

		let got = '';
		await t.subscribe('auth', (data) => (got = new TextDecoder().decode(data)));
		await t.write('auth', bytes('REQ'));

		expect(got).toBe('{"ok":true}');
		expect(t.written).toEqual([{ characteristic: 'auth', data: bytes('REQ') }]);
	});

	it('can fail a scripted write mid-exchange', async () => {
		const t = new FakeTransport();
		await t.connect();
		t.scriptWrite('ota', { fail: new Error('gatt rejected') });
		await expect(t.write('ota', bytes('X'))).rejects.toThrow('gatt rejected');
	});

	it('delivers disconnect mid-exchange to onDisconnected subscribers', async () => {
		const t = new FakeTransport();
		let disconnected = false;
		const unsub = t.onDisconnected(() => (disconnected = true));

		await t.connect();
		t.simulateDisconnect(); // e.g. between a write and its awaited notify
		expect(disconnected).toBe(true);
		expect(t.isConnected()).toBe(false);

		unsub();
		disconnected = false;
		t.simulateDisconnect();
		expect(disconnected).toBe(false);
	});

	it('queues reads and throws when the queue is empty', async () => {
		const t = new FakeTransport();
		t.queueRead('setup_state', Uint8Array.of(0));
		expect(await t.read('setup_state')).toEqual(Uint8Array.of(0));
		await expect(t.read('setup_state')).rejects.toThrow('no queued read');
	});
});
