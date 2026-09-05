<script lang="ts">
	import { session } from '$lib/state/session.svelte';
	import WizardView from './WizardView.svelte';
	import AuthView from './AuthView.svelte';
	import DashboardView from './DashboardView.svelte';

	// Every view behind a connection, in one deferred chunk. Nothing here is
	// reachable until the device is connected, and the switch stays derived
	// from session state - no display flag is toggled imperatively.
	const view = $derived(session.view);
</script>

{#if view === 'wizard'}
	<WizardView />
{:else if view === 'auth'}
	<AuthView />
{:else if view === 'dashboard'}
	<DashboardView />
{/if}
