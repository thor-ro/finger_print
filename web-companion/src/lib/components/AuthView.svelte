<script lang="ts">
	import { session } from '$lib/state/session.svelte';

	function submit(event: SubmitEvent): void {
		event.preventDefault();
		const data = new FormData(event.currentTarget as HTMLFormElement);
		const username = String(data.get('username') ?? '');
		const password = String(data.get('password') ?? '');
		if (session.isRegistering) {
			void session.submitRegister(username, password);
		} else {
			void session.submitLogin(username, password);
		}
	}
</script>

<h2>Authentication</h2>
<div class="tabs">
	<button class="tab-btn" class:active={!session.isRegistering} onclick={() => session.setRegistering(false)}>
		Login
	</button>
	<button class="tab-btn" class:active={session.isRegistering} onclick={() => session.setRegistering(true)}>
		Register
	</button>
</div>

<form class="auth-form" onsubmit={submit}>
	{#if session.isRegistering}
		<p class="status-msg register-note">
			Registering again does not create a second account: the account belongs
			to the admin whose fingerprint confirms it, and their existing password
			will be <strong>replaced</strong>. This is the supported way to reset a
			forgotten password.
		</p>
	{/if}
	<div class="input-group">
		<label for="username">Your Name (on the device)</label>
		<input type="text" id="username" name="username" required maxlength="16" placeholder="e.g. Alice" />
	</div>
	<div class="input-group">
		<label for="password">Password</label>
		<input type="password" id="password" name="password" required minlength="4" placeholder="••••••••" />
	</div>
	<button type="submit" class="primary-btn">
		{session.isRegistering ? 'Register' : 'Login'}
	</button>
</form>
<p class="status-msg">{session.authStatus}</p>

<style>
	.tabs {
		display: flex;
		gap: 0.5rem;
		margin-bottom: 1rem;
	}
	.tab-btn {
		flex: 0 0 auto;
		padding: 0.4rem 1.25rem;
		border-radius: var(--radius-pill);
		border: 1px solid var(--border);
		background: transparent;
		color: var(--muted);
		cursor: pointer;
	}
	.tab-btn.active {
		background: var(--accent-gradient);
		border-color: var(--accent);
		color: var(--text-on-accent);
		font-weight: 600;
	}
	.auth-form {
		max-width: 24rem;
	}
	.register-note {
		background: var(--info-tint);
		border: 1px solid var(--border);
		border-radius: var(--radius);
		padding: 0.6rem 0.8rem;
	}
</style>
