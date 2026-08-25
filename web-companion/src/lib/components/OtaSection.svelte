<script lang="ts">
	import { session } from '$lib/state/session.svelte';

	let fileInput = $state<HTMLInputElement | null>(null);

	function onSubmit(event: SubmitEvent): void {
		event.preventDefault();
		const file = fileInput?.files?.[0];
		if (!file) {
			session.otaStatus = 'Select a firmware (.bin) file first.';
			return;
		}
		void session.startOta(file);
	}
</script>

<section class="dashboard-section">
	<h3>Firmware Update (OTA)</h3>
	<div class="alert warning">
		<strong>Warning:</strong> Ensure your battery is above 20%. OTA transfer
		over Bluetooth draws significant power and firmware is large — keep the app
		open and the device nearby until it completes.
	</div>
	<form onsubmit={onSubmit}>
		<div class="input-group">
			<label for="firmware-file">Firmware Image (.bin)</label>
			<input type="file" id="firmware-file" accept=".bin" required bind:this={fileInput} />
		</div>
		<button type="submit" class="primary-btn danger-btn">Start OTA Update</button>
	</form>
	<p class="status-msg">{session.otaStatus}</p>
	{#if session.otaProgressVisible}
		<progress max="100" value={session.otaProgressPercent}></progress>
	{/if}
</section>

<style>
	form {
		max-width: 24rem;
	}
	progress {
		width: 100%;
		margin-top: 0.5rem;
	}
</style>
