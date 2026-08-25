import type { BleTransport, SdfCharacteristic } from './ble';

/**
 * Test double: replays scripted device responses without any Bluetooth
 * stack. Writes are matched against a queue (or generator) of responses;
 * reads return a queued value; scripted disconnects can fire mid-exchange.
 */
export class FakeTransport implements BleTransport {
	private connected = false;
	private readQueue = new Map<SdfCharacteristic, Uint8Array[]>();
	private writeQueue = new Map<SdfCharacteristic, Array<{ respond?: Uint8Array; fail?: Error }>>();
	private subscribers = new Map<SdfCharacteristic, Array<(data: Uint8Array) => void>>();
	private disconnectCallbacks: Array<() => void> = [];
	written: Array<{ characteristic: SdfCharacteristic; data: Uint8Array }> = [];

	/** Queue the value returned by the next read of `characteristic`. */
	queueRead(characteristic: SdfCharacteristic, data: Uint8Array): this {
		const list = this.readQueue.get(characteristic) ?? [];
		list.push(data);
		this.readQueue.set(characteristic, list);
		return this;
	}

	/** Script what the next write to `characteristic` triggers or fails with. */
	scriptWrite(
		characteristic: SdfCharacteristic,
		response: { respond?: Uint8Array; fail?: Error }
	): this {
		const list = this.writeQueue.get(characteristic) ?? [];
		list.push(response);
		this.writeQueue.set(characteristic, list);
		return this;
	}

	/** Deliver an unsolicited notification (e.g. progress or health update). */
	notify(characteristic: SdfCharacteristic, data: Uint8Array): void {
		for (const cb of this.subscribers.get(characteristic) ?? []) cb(data);
	}

	/** Simulate the device dropping the connection mid-exchange. */
	simulateDisconnect(): void {
		this.connected = false;
		for (const cb of this.disconnectCallbacks) cb();
	}

	async connect(): Promise<void> {
		this.connected = true;
	}

	disconnect(): void {
		this.simulateDisconnect();
	}

	isConnected(): boolean {
		return this.connected;
	}

	async read(characteristic: SdfCharacteristic): Promise<Uint8Array> {
		const next = this.readQueue.get(characteristic)?.shift();
		if (!next) throw new Error(`FakeTransport: no queued read for ${characteristic}`);
		return next;
	}

	async write(characteristic: SdfCharacteristic, data: Uint8Array): Promise<void> {
		this.written.push({ characteristic, data });
		const scripted = this.writeQueue.get(characteristic)?.shift();
		if (!scripted) return;
		if (scripted.fail) throw scripted.fail;
		if (scripted.respond) {
			for (const cb of this.subscribers.get(characteristic) ?? []) cb(scripted.respond);
		}
	}

	async subscribe(
		characteristic: SdfCharacteristic,
		callback: (data: Uint8Array) => void
	): Promise<void> {
		const list = this.subscribers.get(characteristic) ?? [];
		list.push(callback);
		this.subscribers.set(characteristic, list);
	}

	onDisconnected(callback: () => void): () => void {
		this.disconnectCallbacks.push(callback);
		return () => {
			this.disconnectCallbacks = this.disconnectCallbacks.filter((cb) => cb !== callback);
		};
	}
}
