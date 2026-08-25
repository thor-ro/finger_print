<script lang="ts">
	import { session } from '$lib/state/session.svelte';
	import * as health from '$lib/protocol/health';

	const rows = $derived(session.healthReport ? health.healthRows(session.healthReport) : []);
</script>

<section class="dashboard-section">
	<h3>Device Health</h3>
	<p class="hint">
		Every value comes from the device's own health report. Unknown means the
		device holds no reading; N/A means the subsystem is absent by build or
		configuration.
	</p>
	{#if rows.length > 0}
		<table class="config-table">
			<tbody>
				{#each rows as row (row.label)}
					<tr>
						<td><span class="row-label">{row.label}</span></td>
						<td>{row.value}</td>
					</tr>
				{/each}
			</tbody>
		</table>
	{:else}
		<p class="status-msg">No health report received yet.</p>
	{/if}
	<button class="secondary-btn" onclick={() => void session.refreshDeviceHealth()}>
		Refresh
	</button>
</section>

<style>
	.hint {
		font-size: 0.85rem;
		color: var(--muted);
	}
	.row-label {
		font-size: 0.85rem;
		color: var(--muted);
	}
	button {
		margin-top: 0.5rem;
	}
</style>
