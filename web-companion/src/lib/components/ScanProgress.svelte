<script lang="ts">
	interface Props {
		/** Scans the device has confirmed it captured. */
		captured: number;
		/** Scan the device is waiting for now, 1-based; 0 when none is. */
		expected: number;
		/** Scans this enrolment requires. */
		total: number;
		message: string;
	}
	let { captured, expected, total, message }: Props = $props();

	const scans = $derived(Array.from({ length: total }, (_, i) => i + 1));
	const percent = $derived(total > 0 ? Math.round((captured / total) * 100) : 0);

	function stateOf(scan: number): 'captured' | 'expected' | 'outstanding' {
		if (scan <= captured) return 'captured';
		return scan === expected ? 'expected' : 'outstanding';
	}

	/* The marker states are also spelled out in text: colour and shape alone
	 * would put the count out of reach of a screen reader. */
	function labelOf(scan: number): string {
		const state = stateOf(scan);
		if (state === 'captured') return `Scan ${scan} of ${total}: captured`;
		if (state === 'expected') return `Scan ${scan} of ${total}: waiting for your finger`;
		return `Scan ${scan} of ${total}: not yet taken`;
	}
</script>

<div class="enroll-progress">
	<ul class="scan-markers" aria-label="Fingerprint scans">
		{#each scans as scan (scan)}
			<li class="scan-marker" data-state={stateOf(scan)}>
				<span class="visually-hidden">{labelOf(scan)}</span>
				<span class="marker-num" aria-hidden="true">{scan}</span>
			</li>
		{/each}
	</ul>
	<div class="progress-bar" aria-hidden="true">
		<div class="progress-fill" style="width: {percent}%"></div>
	</div>
	<p class="status-msg" role="status">{message}</p>
</div>

<style>
	.enroll-progress {
		margin: 0.75rem 0;
	}
	.scan-markers {
		display: flex;
		gap: 0.5rem;
		list-style: none;
		margin: 0 0 0.5rem;
		padding: 0;
	}
	.scan-marker {
		display: grid;
		place-items: center;
		width: 2rem;
		height: 2rem;
		border-radius: 50%;
		border: 2px solid var(--panel-2);
		background: var(--panel-2);
		color: var(--muted);
		font-variant-numeric: tabular-nums;
	}
	.scan-marker[data-state='captured'] {
		border-color: var(--ok);
		background: var(--ok);
		color: var(--text-on-accent);
	}
	.scan-marker[data-state='expected'] {
		border-color: var(--accent);
		background: var(--panel);
		color: var(--text);
	}
	.progress-bar {
		height: 8px;
		background: var(--panel-2);
		border-radius: var(--radius);
		overflow: hidden;
	}
	.progress-fill {
		height: 100%;
		background: var(--accent);
		transition: width 0.2s ease;
	}
	.visually-hidden {
		position: absolute;
		width: 1px;
		height: 1px;
		margin: -1px;
		padding: 0;
		overflow: hidden;
		clip-path: inset(50%);
		white-space: nowrap;
	}
</style>
