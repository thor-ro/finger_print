<script lang="ts">
	import { session } from '$lib/state/session.svelte';

	function onApply(): void {
		void session.applyConfig();
	}
	function onRead(): void {
		void session.readConfig();
	}
</script>

<section class="dashboard-section">
	<h3>Configuration</h3>
	<button class="secondary-btn" onclick={onRead}>Read Config</button>

	{#if session.configVisible}
		<div class="config-display">
			<table class="config-table">
				<tbody>
					{#each session.configEntries as entry (entry.key)}
						<tr>
							<td><label for="cfg-{entry.key}">{entry.key}</label></td>
							<td>
								{#if typeof entry.value === 'boolean'}
									<input
										type="checkbox"
										id="cfg-{entry.key}"
										bind:checked={entry.value}
										disabled={!entry.isEditable}
									/>
								{:else}
									<input
										type="number"
										id="cfg-{entry.key}"
										bind:value={entry.value}
										disabled={!entry.isEditable}
									/>
								{/if}
							</td>
						</tr>
					{/each}
				</tbody>
			</table>
			<button class="primary-btn apply" onclick={onApply}>Apply Changes</button>
		</div>
	{/if}
</section>

<style>
	.config-display {
		margin-top: 0.75rem;
		max-width: 28rem;
	}
	.apply {
		margin-top: 1rem;
	}
</style>
