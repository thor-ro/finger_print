<script lang="ts">
	import { session } from '$lib/state/session.svelte';
	import ConnectionView from '$lib/components/ConnectionView.svelte';
	import WizardView from '$lib/components/WizardView.svelte';
	import AuthView from '$lib/components/AuthView.svelte';
	import ThemePicker from '$lib/components/ThemePicker.svelte';

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
		<!-- Outside <main> and every view switch: selectable before
		     authentication, on every view. -->
		<ThemePicker />
	</header>

	<main>
		{#if view === 'connection'}
			<ConnectionView />
		{:else if view === 'wizard'}
			<WizardView />
		{:else if view === 'auth'}
			<AuthView />
		{:else if view === 'dashboard'}
			<!-- Reload, not an in-place retry: the browser caches a failed module
			     fetch, so re-importing the same specifier fails without touching
			     the network, and cache-busting the chunk URL would not help when
			     the poisoned entry is one of its dependencies. -->
			{#await import('$lib/components/DashboardView.svelte') then Dashboard}
				<Dashboard.default />
			{:catch}
				<section class="dashboard-section">
					<h3>Dashboard unavailable</h3>
					<p class="status-msg">
						The dashboard could not be loaded. Reloading fetches it again;
						you will need to reconnect to the device afterwards.
					</p>
					<button class="primary-btn" onclick={() => location.reload()}>Reload</button>
				</section>
			{/await}
		{/if}
	</main>
</div>
