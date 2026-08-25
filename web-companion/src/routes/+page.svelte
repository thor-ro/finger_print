<script lang="ts">
	import { session } from '$lib/state/session.svelte';
	import ConnectionView from '$lib/components/ConnectionView.svelte';
	import WizardView from '$lib/components/WizardView.svelte';
	import AuthView from '$lib/components/AuthView.svelte';

	// The dashboard is only reachable after a connection and a successful
	// login, so it is loaded on demand instead of weighing down the initial
	// load (the bundle-budget gate measures what index.html pulls).
	const view = $derived(session.view);
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
			{#await import('$lib/components/DashboardView.svelte') then Dashboard}
				<Dashboard.default />
			{/await}
		{/if}
	</main>
</div>
