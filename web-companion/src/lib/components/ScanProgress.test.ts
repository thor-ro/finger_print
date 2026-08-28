// @vitest-environment jsdom
import { cleanup, render, screen } from '@testing-library/svelte';
import { afterEach, describe, expect, it } from 'vitest';
import ScanProgress from '$lib/components/ScanProgress.svelte';

afterEach(cleanup);

function markerStates(): Array<string | null> {
	return [...document.querySelectorAll('.scan-marker')].map((el) => el.getAttribute('data-state'));
}

describe('scan visualization', () => {
	it('shows one marker per required scan', () => {
		render(ScanProgress, { captured: 0, expected: 1, total: 3, message: '' });
		expect(markerStates()).toHaveLength(3);
	});

	it('marks captured scans, the expected one, and the outstanding rest', () => {
		render(ScanProgress, { captured: 1, expected: 2, total: 3, message: '' });
		expect(markerStates()).toEqual(['captured', 'expected', 'outstanding']);
	});

	it('marks every scan captured once the enrolment succeeded', () => {
		render(ScanProgress, { captured: 3, expected: 0, total: 3, message: '' });
		expect(markerStates()).toEqual(['captured', 'captured', 'captured']);
	});

	it('expects nothing while the device has not asked for a scan', () => {
		render(ScanProgress, { captured: 0, expected: 0, total: 3, message: '' });
		expect(markerStates()).toEqual(['outstanding', 'outstanding', 'outstanding']);
	});

	it('states each marker in text, so the count is not carried by colour alone', () => {
		render(ScanProgress, { captured: 1, expected: 2, total: 3, message: '' });
		expect(screen.getByText('Scan 1 of 3: captured')).toBeTruthy();
		expect(screen.getByText('Scan 2 of 3: waiting for your finger')).toBeTruthy();
		expect(screen.getByText('Scan 3 of 3: not yet taken')).toBeTruthy();
	});

	it('renders the prompt the session supplied', () => {
		render(ScanProgress, {
			captured: 1,
			expected: 2,
			total: 3,
			message: 'Lift the finger, then place it again for scan 2 of 3.'
		});
		expect(screen.getByText(/place it again for scan 2 of 3/)).toBeTruthy();
	});

	it('follows a total the device reports rather than a fixed three', () => {
		render(ScanProgress, { captured: 2, expected: 3, total: 4, message: '' });
		expect(markerStates()).toEqual(['captured', 'captured', 'expected', 'outstanding']);
	});
});
