/**
 * Theme selection: which shipped theme is active, and where the choice
 * lives. Purely local to the browser - a theme change never writes to any
 * BLE characteristic and never reaches the device.
 *
 * The initial value is whatever src/app.html's pre-paint script already
 * applied to <html data-theme='…'> (stored choice, else the system colour
 * scheme); this module only reads it back so the picker can display it.
 */

export const THEMES = ['dark', 'axolotl'] as const;
export type ThemeId = (typeof THEMES)[number];

/** Must stay in sync with the pre-paint script in src/app.html. */
export const THEME_STORAGE_KEY = 'sdf-theme';

export const themeLabels: Record<ThemeId, string> = {
	dark: 'Dark',
	axolotl: 'Axolotl'
};

function isThemeId(value: string | undefined | null): value is ThemeId {
	return (THEMES as readonly string[]).includes(value ?? '');
}

function initialTheme(): ThemeId {
	if (typeof document === 'undefined') return 'dark';
	const applied = document.documentElement.dataset.theme;
	return isThemeId(applied) ? applied : 'dark';
}

export const themeState = $state<{ current: ThemeId }>({ current: initialTheme() });

export function currentTheme(): ThemeId {
	return themeState.current;
}

export function setTheme(theme: ThemeId): void {
	themeState.current = theme;
	document.documentElement.dataset.theme = theme;
	try {
		localStorage.setItem(THEME_STORAGE_KEY, theme);
	} catch {
		/* Storage unavailable (private mode etc.) - choice applies for this
		 * session only. Never falls back to any device-side persistence. */
	}
}
