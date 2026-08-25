<script lang="ts">
	import { session } from '$lib/state/session.svelte';
	import ScanProgress from './ScanProgress.svelte';
</script>

<h2>First-Time Setup</h2>
<div class="alert warning">
	<strong>Time limit:</strong> Setup must be completed within the
	device's setup window (about 15 minutes from when the device was
	armed or the button was pressed). If the window lapses, all
	progress is erased — including enrolled fingerprints, accounts,
	and pairing data — and the device must be re-armed with its button.
</div>
<p class="status-msg">{session.wizardIndicator}</p>

{#if session.wizardStep === 'enroll'}
	<section class="dashboard-section">
		<h3>Step 1 of 4 · Enrol the Admin Fingerprint</h3>
		<p>
			The first Admin fingerprint unlocks every later step. Place the admin's
			finger on the sensor when prompted by the device.
		</p>
		<button class="primary-btn" onclick={() => void session.wizardEnrollAdmin()}>
			Enrol Admin Finger
		</button>
		{#if session.wizardEnrollProgressVisible}
			<ScanProgress
				stepText={session.wizardEnrollStepText}
				percent={session.wizardEnrollPercent}
				message={session.wizardEnrollMessage}
			/>
		{/if}
		<p class="status-msg">{session.wizardEnrollStatus}</p>
	</section>
{:else if session.wizardStep === 'register'}
	<section class="dashboard-section">
		<h3>Step 2 of 4 · Register Your Account</h3>
		<p>
			The name below is <strong>your name on the device</strong> — it is not a
			separate account username, and it must be unique. Confirming registration
			requires a scan of the Admin finger just enrolled, and the account will
			belong to that admin: whoever confirms with their fingerprint owns this
			account.
		</p>
		<form
			onsubmit={(e) => {
				e.preventDefault();
				const data = new FormData(e.currentTarget);
				void session.wizardRegister(
					String(data.get('username') ?? ''),
					String(data.get('password') ?? '')
				);
			}}
		>
			<div class="input-group">
				<label for="wizard-username">Your Name (on the device)</label>
				<input type="text" id="wizard-username" name="username" required maxlength="16" placeholder="e.g. Alice" />
			</div>
			<div class="input-group">
				<label for="wizard-password">Password</label>
				<input type="password" id="wizard-password" name="password" required minlength="4" placeholder="••••••••" />
			</div>
			<button type="submit" class="primary-btn">Register Account</button>
		</form>
		<p class="status-msg">{session.wizardRegisterStatus}</p>
	</section>
{:else if session.wizardStep === 'nuki'}
	<section class="dashboard-section">
		<h3>Step 3 of 4 · Pair Your Nuki Lock</h3>
		<p>
			Put your Nuki Smart Lock into pairing mode first: press and hold the
			lock's button (on the inside of the door) for about 5 seconds until its
			LED lights up. Then start pairing here.
		</p>
		<button class="primary-btn" onclick={() => void session.wizardNukiPair()}>
			Start Nuki Pairing
		</button>
		<p class="status-msg">{session.wizardNukiStatus}</p>
	</section>
{:else}
	<section class="dashboard-section">
		<h3>Step 4 of 4 · Finish Setup</h3>
		<p>
			Finishing locks this device to this browser: it will switch to filtered
			advertising and only reconnect to the companion that completed setup.
			Everything else stays as configured.
		</p>
		<button class="primary-btn" onclick={() => void session.wizardFinish()}>
			Finish Setup
		</button>
		<p class="status-msg">{session.wizardFinishStatus}</p>
	</section>
{/if}

<style>
	form {
		max-width: 24rem;
	}
</style>
