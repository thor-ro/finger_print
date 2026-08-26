<script lang="ts">
	interface Props {
		stepText: string;
		percent: number;
		message: string;
	}
	let { stepText, percent, message }: Props = $props();
</script>

<div class="enroll-progress">
	<div class="step-indicator">
		<span>{stepText}</span>
		<div class="progress-bar">
			<div class="progress-fill" style="width: {percent}%"></div>
		</div>
	</div>
	<p class="status-msg">{message}</p>
</div>

<style>
	.enroll-progress {
		margin: 0.75rem 0;
	}
	.step-indicator {
		display: flex;
		align-items: center;
		gap: 0.75rem;
	}
	.progress-bar {
		flex: 1;
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
</style>
