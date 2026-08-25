<script lang="ts">
	import { session } from '$lib/state/session.svelte';
	import ScanProgress from './ScanProgress.svelte';

	let userId = $state(1);
	let permission = $state(1);

	function onEnroll(): void {
		void session.enroll(userId, permission);
	}
</script>

<section class="dashboard-section">
	<h3>Enroll Fingerprint</h3>
	<p>
		Four scans are required: an Admin first scans their fingerprint on the
		device to authorize the enrolment, then the new user scans three times.
	</p>
	<div class="enrollment-panel">
		<div class="input-group">
			<label for="enroll-user-id">User ID (1-10)</label>
			<input type="number" id="enroll-user-id" min="1" max="10" bind:value={userId} />
		</div>
		<div class="input-group">
			<label for="enroll-permission">Permission</label>
			<select id="enroll-permission" bind:value={permission}>
				<option value={1}>Standard</option>
				<option value={3}>Admin</option>
			</select>
		</div>
		<button class="primary-btn" onclick={onEnroll}>Enroll Fingerprint</button>

		{#if session.enrollProgressVisible}
			<ScanProgress
				stepText={session.enrollStepText}
				percent={session.enrollPercent}
				message={session.enrollMessage}
			/>
		{/if}
		{#if session.enrollResultText}
			<p class="status-msg result" style:color={session.enrollResultColor || 'inherit'}>
				{session.enrollResultText}
			</p>
		{/if}
	</div>
</section>

<style>
	.enrollment-panel {
		max-width: 24rem;
	}
	.result {
		margin-top: 0.5rem;
	}
</style>
