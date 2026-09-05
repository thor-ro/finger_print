<script lang="ts">
	import { session } from '$lib/state/session.svelte';
	import ConnectionView from '$lib/components/ConnectionView.svelte';
	import ThemePicker from '$lib/components/ThemePicker.svelte';

	// Only the connection view can be the first thing on screen; the wizard,
	// the login pane and the dashboard all need a connected device first, so
	// they load on demand instead of weighing down the initial load (the
	// bundle-budget gate measures what index.html pulls). They share ONE
	// deferred chunk on purpose: split per view, the code they have in common
	// is either duplicated or hoisted into its own chunk, and the total load
	// the budget also measures goes UP.
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
		{:else}
			<!-- Reload, not an in-place retry: the browser caches a failed module
			     fetch, so re-importing the same specifier fails without touching
			     the network, and cache-busting the chunk URL would not help when
			     the poisoned entry is one of its dependencies. -->
			{#await import('$lib/components/PostConnectView.svelte') then Post}
				<Post.default />
			{:catch}
				<section class="dashboard-section">
					<h3>Could not load this screen</h3>
					<p class="status-msg">
						The rest of the app could not be loaded. Reloading fetches it
						again; you will need to reconnect to the device afterwards.
					</p>
					<button class="primary-btn" onclick={() => location.reload()}>Reload</button>
				</section>
			{/await}
		{/if}
	</main>
</div>
