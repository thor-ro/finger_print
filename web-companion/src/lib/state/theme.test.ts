// @vitest-environment jsdom
import { readdirSync, readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { cleanup, fireEvent, render, screen } from '@testing-library/svelte';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { session } from '$lib/state/session.svelte';
import { resetSessionForTests } from '$lib/testing/session-reset';
import { FakeTransport } from '$lib/transport/FakeTransport';
import {
	THEMES,
	THEME_STORAGE_KEY,
	currentTheme,
	setTheme,
	themeLabels
} from '$lib/state/theme.svelte';
import ThemePicker from '$lib/components/ThemePicker.svelte';

const here = dirname(fileURLToPath(import.meta.url));
const APP_HTML = resolve(here, '../../app.html');
const THEMES_DIR = resolve(here, '../../../themes');

/**
 * The jsdom environment here ships no Storage implementation, so the tests
 * supply one. That is also why `setTheme` must survive storage throwing -
 * see the private-mode case below.
 */
function memoryStorage(onSet?: () => void): Storage {
	const map = new Map<string, string>();
	return {
		get length() {
			return map.size;
		},
		clear: () => map.clear(),
		getItem: (key: string) => map.get(key) ?? null,
		key: (index: number) => [...map.keys()][index] ?? null,
		removeItem: (key: string) => void map.delete(key),
		setItem: (key: string, value: string) => {
			onSet?.();
			map.set(key, String(value));
		}
	};
}

beforeEach(() => {
	resetSessionForTests();
	vi.stubGlobal('localStorage', memoryStorage());
	setTheme('dark');
});
afterEach(() => {
	cleanup();
	vi.restoreAllMocks();
	vi.unstubAllGlobals();
	vi.resetModules();
});

describe('a theme choice is local to the browser', () => {
	it('changing theme writes nothing to the device', async () => {
		const t = new FakeTransport();
		await t.connect();
		session.transport = t;
		const write = vi.spyOn(t, 'write');
		const read = vi.spyOn(t, 'read');
		const subscribe = vi.spyOn(t, 'subscribe');

		render(ThemePicker);
		await fireEvent.click(screen.getByRole('button', { name: themeLabels.axolotl }));
		await fireEvent.click(screen.getByRole('button', { name: themeLabels.dark }));

		// The requirement is about the connection, not just this store: a
		// theme is presentation and the device holds no record of it.
		expect(t.written).toEqual([]);
		expect(write).not.toHaveBeenCalled();
		expect(read).not.toHaveBeenCalled();
		expect(subscribe).not.toHaveBeenCalled();
	});

	it('persists the choice and applies it to the document', () => {
		setTheme('axolotl');

		expect(localStorage.getItem(THEME_STORAGE_KEY)).toBe('axolotl');
		expect(document.documentElement.dataset.theme).toBe('axolotl');
		expect(currentTheme()).toBe('axolotl');
	});

	it('still applies the choice when storage is unavailable', () => {
		vi.stubGlobal(
			'localStorage',
			memoryStorage(() => {
				throw new Error('storage disabled');
			})
		);

		expect(() => setTheme('axolotl')).not.toThrow();
		expect(document.documentElement.dataset.theme).toBe('axolotl');
		expect(currentTheme()).toBe('axolotl');
	});

	it('starts from the theme the pre-paint script already applied', async () => {
		document.documentElement.dataset.theme = 'axolotl';
		vi.resetModules();

		const fresh = await import('$lib/state/theme.svelte');

		expect(fresh.currentTheme()).toBe('axolotl');
	});
});

describe('the picker offers every shipped theme', () => {
	it('renders a button per theme and marks the active one', async () => {
		render(ThemePicker);

		for (const id of THEMES) {
			expect(screen.getByRole('button', { name: themeLabels[id] })).toBeTruthy();
		}
		await fireEvent.click(screen.getByRole('button', { name: themeLabels.axolotl }));
		expect(
			screen.getByRole('button', { name: themeLabels.axolotl }).getAttribute('aria-pressed')
		).toBe('true');
		expect(
			screen.getByRole('button', { name: themeLabels.dark }).getAttribute('aria-pressed')
		).toBe('false');
	});
});

describe('the four places a theme is declared cannot drift apart', () => {
	// Adding a theme touches themes/<id>.css, THEMES, the pre-paint script
	// in app.html and the picker. A theme missing from any one of them is
	// unselectable, broken, or flashes the wrong palette on load.
	it('every shipped theme file is a selectable theme, and vice versa', () => {
		const files = readdirSync(THEMES_DIR)
			.filter((f) => f.endsWith('.css'))
			.map((f) => f.slice(0, -'.css'.length))
			.sort();

		expect(files).toEqual([...THEMES].sort());
	});

	it('the pre-paint script knows the same themes and the same storage key', () => {
		const html = readFileSync(APP_HTML, 'utf8');
		const script = html.slice(html.indexOf('<script>'), html.indexOf('</script>'));

		expect(script).toMatch(/localStorage\.getItem\('([^']+)'\)/);
		expect(script.match(/localStorage\.getItem\('([^']+)'\)/)?.[1]).toBe(THEME_STORAGE_KEY);

		// Every plain single-quoted identifier in the script, minus the
		// storage key and the attribute it sets, is a theme id.
		const ids = new Set(
			[...script.matchAll(/'([a-z0-9-]+)'/g)]
				.map((m) => m[1])
				.filter((v) => v !== THEME_STORAGE_KEY && v !== 'data-theme')
		);

		expect([...ids].sort()).toEqual([...THEMES].sort());
	});

	it('the pre-paint default follows the system colour scheme', () => {
		const html = readFileSync(APP_HTML, 'utf8');
		const script = html.slice(html.indexOf('<script>'), html.indexOf('</script>'));

		expect(script).toContain("matchMedia('(prefers-color-scheme: light)')");
		// light -> axolotl, anything else -> dark (design.md's open call is
		// about which side axolotl belongs on; the mapping is asserted here
		// so a silent flip cannot happen).
		expect(script.replace(/\s+/g, ' ')).toMatch(/light\)'\)\.matches \? 'axolotl' : 'dark'/);
	});
});
