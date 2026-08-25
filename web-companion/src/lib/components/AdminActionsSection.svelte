<script lang="ts">
	import { session } from '$lib/state/session.svelte';

	let nukiStatus = $state('');
	let enrollAdminStatus = $state('');
	let zbJoinStatus = $state('');

	function nukiRepair(): void {
		void session.requestAdminAction(
			'nuki_repair',
			'Requesting Nuki re-pair... scan the Admin fingerprint on the device.',
			'Nuki pairing started on the device.',
			(msg) => (nukiStatus = msg)
		);
	}
	function enrollAdmin(): void {
		void session.requestAdminAction(
			'enroll_admin',
			'Requesting Enroll-Admin... scan the Admin fingerprint on the device.',
			'Admin enrollment started on the device - follow the fingerprint prompts.',
			(msg) => (enrollAdminStatus = msg)
		);
	}
	function zbJoin(): void {
		void session.requestAdminAction(
			'zb_join',
			'Requesting Zigbee Join window... scan the Admin fingerprint on the device.',
			'Zigbee join window opened on the device.',
			(msg) => (zbJoinStatus = msg)
		);
	}
</script>

<section class="dashboard-section">
	<h3>Nuki Pairing</h3>
	<p>
		If the door lock needs to be re-paired with Nuki (e.g. after replacing the
		lock), request a re-pair below. An Admin must scan their fingerprint on the
		device to approve it — a BLE request alone is never enough.
	</p>
	<button class="secondary-btn" onclick={nukiRepair}>Request Nuki Re-pair</button>
	<p class="status-msg">{nukiStatus}</p>
</section>

<section class="dashboard-section">
	<h3>Enroll Admin</h3>
	<p>
		Enroll a new Admin-permission fingerprint. An existing Admin must scan
		their fingerprint on the device to approve it — a BLE request alone is
		never enough.
	</p>
	<button class="secondary-btn" onclick={enrollAdmin}>Request Enroll Admin</button>
	<p class="status-msg">{enrollAdminStatus}</p>
</section>

<section class="dashboard-section">
	<h3>Zigbee Join</h3>
	<p>
		Open a window for a new Zigbee device to join the network. An Admin must
		scan their fingerprint on the device to approve it — a BLE request alone is
		never enough.
	</p>
	<button class="secondary-btn" onclick={zbJoin}>Request Zigbee Join</button>
	<p class="status-msg">{zbJoinStatus}</p>
</section>
