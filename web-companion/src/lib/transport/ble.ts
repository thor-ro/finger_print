/**
 * The BLE transport boundary.
 *
 * `navigator.bluetooth` may ONLY be referenced inside
 * `WebBluetoothTransport` below. Everything else - components, stores,
 * tests - goes through the `BleTransport` interface so tests can substitute
 * a fake device.
 */

export type SdfCharacteristic =
	| 'auth'
	| 'config'
	| 'enroll'
	| 'ota'
	| 'setup_state'
	| 'status';

export interface BleTransport {
	connect(): Promise<void>;
	disconnect(): void;
	isConnected(): boolean;
	read(characteristic: SdfCharacteristic): Promise<Uint8Array>;
	write(characteristic: SdfCharacteristic, data: Uint8Array): Promise<void>;
	subscribe(
		characteristic: SdfCharacteristic,
		callback: (data: Uint8Array) => void
	): Promise<void>;
	onDisconnected(callback: () => void): () => void;
}

// --- Minimal Web Bluetooth ambient types (subset the transport uses) ---

declare global {
	interface Navigator {
		readonly bluetooth: Bluetooth;
	}
}

interface BluetoothRemoteGATTCharacteristic extends EventTarget {
	readValue(): Promise<DataView>;
	writeValue(value: BufferSource): Promise<void>;
	startNotifications(): Promise<BluetoothRemoteGATTCharacteristic>;
}

interface BluetoothRemoteGATTService {
	getCharacteristic(uuid: string): Promise<BluetoothRemoteGATTCharacteristic>;
}

interface BluetoothRemoteGATTServer {
	connected: boolean;
	connect(): Promise<BluetoothRemoteGATTServer>;
	disconnect(): void;
	getPrimaryService(uuid: string): Promise<BluetoothRemoteGATTService>;
}

export interface WebBluetoothDevice {
	gatt?: BluetoothRemoteGATTServer;
	addEventListener(type: 'gattserverdisconnected', listener: () => void): void;
	removeEventListener(type: 'gattserverdisconnected', listener: () => void): void;
}

interface Bluetooth {
	requestDevice(options: {
		filters: Array<{ services: [string] }>;
		optionalServices: string[];
	}): Promise<WebBluetoothDevice>;
}

// --- Implementation ---

export const SDF_SERVICE_UUID = '7d5a0000-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const CHARACTERISTIC_UUIDS: Record<SdfCharacteristic, string> = {
	auth: '7d5a0001-5c2b-4f8a-9e3d-1a2b3c4d5e6f',
	config: '7d5a0002-5c2b-4f8a-9e3d-1a2b3c4d5e6f',
	enroll: '7d5a0003-5c2b-4f8a-9e3d-1a2b3c4d5e6f',
	ota: '7d5a0004-5c2b-4f8a-9e3d-1a2b3c4d5e6f',
	setup_state: '7d5a0005-5c2b-4f8a-9e3d-1a2b3c4d5e6f',
	status: '7d5a0006-5c2b-4f8a-9e3d-1a2b3c4d5e6f'
};

/** The only production implementation of {@link BleTransport}. */
export class WebBluetoothTransport implements BleTransport {
	private device: WebBluetoothDevice | null = null;
	private characteristics = new Map<SdfCharacteristic, BluetoothRemoteGATTCharacteristic>();
	private disconnectCallbacks: Array<() => void> = [];

	async connect(): Promise<void> {
		const bluetooth: Bluetooth = navigator.bluetooth;
		this.device = await bluetooth.requestDevice({
			filters: [{ services: [SDF_SERVICE_UUID] }],
			optionalServices: []
		});
		this.device.addEventListener('gattserverdisconnected', () => {
			for (const cb of this.disconnectCallbacks) cb();
		});

		const gatt = this.device.gatt;
		if (!gatt) throw new Error('Device exposes no GATT server');
		const server = await gatt.connect();

		try {
			const mtu = await (
				server as unknown as { requestMTU?: (n: number) => Promise<number> }
			).requestMTU?.(512);
			if (mtu) console.log(`MTU negotiated to ${mtu} bytes`);
		} catch (e) {
			console.warn('MTU negotiation not supported:', e);
		}

		const service = await server.getPrimaryService(SDF_SERVICE_UUID);
		this.characteristics.clear();
		for (const [name, uuid] of Object.entries(CHARACTERISTIC_UUIDS)) {
			this.characteristics.set(name as SdfCharacteristic, await service.getCharacteristic(uuid));
		}
	}

	disconnect(): void {
		if (this.device?.gatt?.connected) {
			this.device.gatt.disconnect();
		}
	}

	isConnected(): boolean {
		return !!(this.device?.gatt?.connected && this.characteristics.size > 0);
	}

	async read(characteristic: SdfCharacteristic): Promise<Uint8Array> {
		return this.charFor(characteristic).readValue().then(dvToBytes);
	}

	async write(characteristic: SdfCharacteristic, data: Uint8Array): Promise<void> {
		await this.charFor(characteristic).writeValue(data as unknown as BufferSource);
	}

	async subscribe(
		characteristic: SdfCharacteristic,
		callback: (data: Uint8Array) => void
	): Promise<void> {
		const ch = this.charFor(characteristic);
		ch.addEventListener('characteristicvaluechanged', (event: Event) => {
			const target = event.target as unknown as { value: DataView };
			callback(dvToBytes(target.value));
		});
		await ch.startNotifications();
	}

	onDisconnected(callback: () => void): () => void {
		this.disconnectCallbacks.push(callback);
		return () => {
			this.disconnectCallbacks = this.disconnectCallbacks.filter((cb) => cb !== callback);
		};
	}

	private charFor(characteristic: SdfCharacteristic): BluetoothRemoteGATTCharacteristic {
		const ch = this.characteristics.get(characteristic);
		if (!ch) throw new Error(`Characteristic ${characteristic} is not available`);
		return ch;
	}
}

function dvToBytes(view: DataView): Uint8Array {
	return new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
}
