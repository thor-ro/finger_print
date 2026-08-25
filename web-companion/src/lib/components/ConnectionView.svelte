<script lang="ts">
	import { session } from '$lib/state/session.svelte';
	import { WebBluetoothTransport } from '$lib/transport/ble';

	function connect(): void {
		void session.connect(() => new WebBluetoothTransport());
	}
</script>

<h2>Connect to Bridge</h2>
<p>Ensure your device has Bluetooth enabled.</p>
<button class="primary-btn" onclick={connect}>Connect via WebBLE</button>
<p class="status-msg">{session.connectionStatus}</p>

<style>
	button {
		margin-bottom: 0.5rem;
	}
</style>
