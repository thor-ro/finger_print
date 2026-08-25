<script lang="ts">
	import { session } from '$lib/state/session.svelte';
	import * as health from '$lib/protocol/health';
	import HealthSection from './HealthSection.svelte';
	import ConfigSection from './ConfigSection.svelte';
	import EnrollSection from './EnrollSection.svelte';
	import UserMgmtSection from './UserMgmtSection.svelte';
	import AdminActionsSection from './AdminActionsSection.svelte';
	import OtaSection from './OtaSection.svelte';

	function disconnect(): void {
		session.disconnect();
	}
</script>

<h2>Device Dashboard</h2>
<p>Manage your Smart Door Bridge.</p>

{#if session.healthReport && (session.healthReport.lock || session.healthReport.battery)}
	<div class="status-cards">
		<div class="status-card">
			<span class="status-label">Lock</span>
			<span class="status-value">{health.describeLockState(session.healthReport)}</span>
		</div>
		<div class="status-card">
			<span class="status-label">Battery</span>
			<span class="status-value">{health.describeBattery(session.healthReport)}</span>
		</div>
	</div>
{/if}

<HealthSection />
<ConfigSection />
<EnrollSection />
<UserMgmtSection />
<AdminActionsSection />
<OtaSection />

<button class="secondary-btn" onclick={disconnect}>Disconnect</button>

<style>
	.status-cards {
		display: flex;
		gap: 1rem;
		margin-bottom: 1rem;
	}
	.status-card {
		background: var(--panel);
		border: 1px solid var(--border);
		border-radius: 8px;
		padding: 0.75rem 1.25rem;
		display: flex;
		flex-direction: column;
		min-width: 9rem;
	}
	.status-label {
		font-size: 0.75rem;
		text-transform: uppercase;
		letter-spacing: 0.05em;
		color: var(--muted);
	}
	.status-value {
		font-size: 1.25rem;
		font-weight: 600;
	}
	button:last-child {
		margin-top: 1rem;
	}
</style>
