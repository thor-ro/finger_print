<script lang="ts">
	import { currentTheme, setTheme } from '$lib/state/theme.svelte';

	/**
	 * Theme picker. Rendered in the page header, so it is reachable from
	 * every view - connection, wizard, auth and dashboard - including
	 * before authentication. Selecting a theme only touches
	 * <html data-theme> and localStorage; nothing is written to the device.
	 *
	 * The two options are unrolled by hand: an {#each} block here would
	 * pull Svelte's keyed-each/template runtime into the initial bundle,
	 * and the budget gate has no room for it. Add a button when a third
	 * theme ships - and keep it off the critical path if one ever does.
	 */
</script>

<div class="theme-picker" role="group" aria-label="Colour theme">
	<span class="theme-picker-label">Theme</span>
	<button
		type="button"
		class="theme-btn"
		class:active={currentTheme() === 'dark'}
		aria-pressed={currentTheme() === 'dark'}
		onclick={() => setTheme('dark')}
	>
		Dark
	</button>
	<button
		type="button"
		class="theme-btn"
		class:active={currentTheme() === 'axolotl'}
		aria-pressed={currentTheme() === 'axolotl'}
		onclick={() => setTheme('axolotl')}
	>
		Axolotl
	</button>
</div>

<style>
	.theme-picker {
		display: inline-flex;
		align-items: center;
		gap: 0.35rem;
		margin-top: 0.5rem;
	}
	.theme-picker-label {
		font-size: 0.85rem;
		color: var(--muted);
	}
	.theme-btn {
		padding: 0.15rem 0.7rem;
		font-size: 0.8rem;
		border-radius: var(--radius-pill);
		background: transparent;
		border-color: var(--border);
		color: var(--muted);
	}
	.theme-btn.active {
		background: var(--accent-gradient);
		border-color: var(--accent);
		color: var(--text-on-accent);
		font-weight: 600;
	}
</style>
