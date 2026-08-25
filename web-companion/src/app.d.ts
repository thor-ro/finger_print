import type { WebBluetoothDevice } from '$lib/transport/ble';

declare global {
	namespace App {
		interface Error {
			message: string;
		}
	}
}

export {};
