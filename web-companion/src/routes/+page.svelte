<script lang="ts">
	import { session } from '$lib/state/session.svelte';
	import ConnectionView from '$lib/components/ConnectionView.svelte';
	import WizardView from '$lib/components/WizardView.svelte';
	import AuthView from '$lib/components/AuthView.svelte';

	// The dashboard is only reachable after a connection and a successful
	// login, so it is loaded on demand instead of weighing down the initial
	// load (the bundle-budget gate measures what index.html pulls).
	const view = $derived(session.view);
	// Incremented by Retry to re-run the deferred import after a failure.
	let dashboardAttempt = $state(0);

	function loadDashboard() {
		return import('$lib/components/DashboardView.svelte');
	}
</script>

<svelte:head>
	<title>Smart Door Web Companion</title>
</svelte:head>

<div class="app-container">
	<header>
		<h1>Smart Door</h1>
		<p>Web Companion</p>
	</header>

	<main>
		{#if view === 'connection'}
			<ConnectionView />
		{:else if view === 'wizard'}
			<WizardView />
		{:else if view === 'auth'}
			<AuthView />
		{:else if view === 'dashboard'}
			{#key dashboardAttempt}
				{#await loadDashboard() then Dashboard}
					<Dashboard.default />
				{:catch}
					<section class="dashboard-section">
						<h3>Dashboard unavailable</h3>
						<p class="status-msg">
							The dashboard view could not be loaded (the connection may have
							dropped mid-download). Your session is unaffected.
						</p>
						<button class="primary-btn" onclick={() => dashboardAttempt++}>Retry</button>
					</section>
				{/await}
			{/key}
		{/if}
	</main>
</div>
