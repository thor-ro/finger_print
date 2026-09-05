<script lang="ts">
	import { formatRemaining, presentEnrol } from '$lib/protocol/setup';
	import { session } from '$lib/state/session.svelte';
	import ScanProgress from './ScanProgress.svelte';

	const remaining = $derived(session.wizardDeadlineRemainingMs);
	const expired = $derived(remaining !== null && remaining <= 0);

	const enrol = $derived(
		presentEnrol({
			phase: session.wizardEnrollPhase,
			captured: session.wizardEnrollCaptured,
			expected: session.wizardEnrollExpected,
			total: session.wizardEnrollTotal,
			error: session.wizardEnrollError
		})
	);

	function onEnrolAction(): void {
		if (session.wizardEnrollPhase === 'success') session.wizardEnrollContinue();
		else if (session.wizardEnrollPhase === 'failed') session.wizardEnrollRetry();
		else void session.wizardEnrollAdmin();
	}
</script>

<h2>First-Time Setup</h2>
<div class="alert warning">
	{#if expired}
		<p class="deadline-head">Time is up.</p>
		<p class="fineprint">
			The device discarded this attempt. Press its button to re-arm setup, then
			reconnect and start again.
		</p>
	{:else if remaining !== null}
		<p class="deadline-head" aria-live="polite">At most {formatRemaining(remaining)} left</p>
		<p class="fineprint">
			If it lapses, all progress is erased and the device must be re-armed with
			its button. The clock starts at the device's first connection, so it may
			have less left than shown.
		</p>
	{:else}
		<p class="fineprint">
			Setup runs under a time limit on the device — about 10 minutes. If it
			lapses, progress is erased and the device must be re-armed with its button.
		</p>
	{/if}
</div>
<p class="status-msg">{session.wizardIndicator}</p>

{#if session.wizardStep === 'enroll'}
	<section class="dashboard-section">
		<h3>Step 1 of 4 · Enrol the Admin Fingerprint</h3>

		{#if enrol.tone === 'none'}
			{#if enrol.headline}
				<p class="instruction" role="status">{enrol.headline}</p>
			{/if}
			<p class="fineprint">{enrol.sub}</p>
		{:else}
			<div class="outcome {enrol.tone}" role={enrol.tone === 'bad' ? 'alert' : 'status'}>
				<p class="outcome-head">
					<span aria-hidden="true">{enrol.tone === 'ok' ? '✓' : '✕'}</span>
					{enrol.headline}
				</p>
				<p>{enrol.sub}</p>
				{#if session.wizardEnrollHint}
					<p class="fineprint">{session.wizardEnrollHint}</p>
				{/if}
			</div>
		{/if}

		<!-- The markers carry the count; the lines above carry the words, so
		     the prompt is never printed twice. -->
		{#if enrol.markers}
			<ScanProgress
				captured={session.wizardEnrollCaptured}
				expected={session.wizardEnrollExpected}
				total={session.wizardEnrollTotal}
				message=""
			/>
		{/if}

		{#if enrol.action}
			<button class="primary-btn" onclick={onEnrolAction}>{enrol.action}</button>
		{/if}
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

	/* One instruction, sized so it is the thing you read first. */
	.instruction {
		font-size: 1.35rem;
		font-weight: 600;
		margin: 0.75rem 0 0.25rem;
	}
	.deadline-head,
	.outcome-head {
		font-weight: 600;
		margin: 0 0 0.25rem;
	}
	.outcome-head {
		font-size: 1.2rem;
	}
	.fineprint {
		margin: 0.25rem 0 0.5rem;
		color: var(--muted);
		font-size: 0.875rem;
	}

	.outcome {
		border-radius: var(--radius);
		border: 1px solid var(--border);
		padding: 0.75rem 1rem;
		margin: 0.75rem 0;
	}
	/* Colour reinforces the glyph and the sentence; it never carries the
	   outcome on its own (themes/CONTRACT.md). */
	.outcome.ok {
		border-color: var(--ok);
		background: var(--info-tint);
	}
	.outcome.ok .outcome-head {
		color: var(--ok);
	}
	.outcome.bad {
		border-color: var(--danger);
		background: var(--danger-tint);
	}
	.outcome.bad .outcome-head {
		color: var(--danger);
	}
	.outcome p {
		margin: 0.25rem 0 0;
	}
</style>
