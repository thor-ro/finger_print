// @vitest-environment jsdom
import { cleanup, render, screen } from '@testing-library/svelte';
import { afterEach, describe, expect, it } from 'vitest';
import UserMgmtSection from '$lib/components/UserMgmtSection.svelte';
import { session } from '$lib/state/session.svelte';

afterEach(cleanup);

describe('user-management rendering', () => {
	it('renders a device-reported name containing markup, quotes literally', () => {
		const hostile = '<script>alert(1)</script>"\'';
		session.umUsers = [{ id: 4, name: hostile, perm: 1 }];
		session.umStatus = '';

		render(UserMgmtSection);

		const cell = screen.getByText(hostile);
		expect(cell.textContent).toBe(hostile); // displayed literally
		expect(document.querySelector('script')).toBeNull(); // never executed
	});

	it('binds per-row handlers without carrying values through markup attributes', () => {
		session.umUsers = [
			{ id: 1, name: '"><img src=x onerror=alert(1)>', perm: 3 },
			{ id: 2, name: 'bob', perm: 1 }
		];
		render(UserMgmtSection);

		// No data-um-* attributes: records reach handlers in scope, not via markup.
		expect(document.querySelector('[data-um-action]')).toBeNull();
		// Two rows, each with their own three action buttons.
		expect(screen.getAllByRole('button', { name: 'Delete' })).toHaveLength(2);
	});
});
