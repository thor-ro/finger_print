/**
 * Setup-state decoding and wizard resume logic.
 *
 * PURE module: no DOM, no navigator, no SvelteKit imports.
 *
 * The setup-state characteristic reports one byte (mirrors
 * sdf_services_setup_state_t):
 *   0 setup not started | 1 Admin enrolled | 2 account registered
 *   3 Nuki paired       | 4 complete
 *
 * The wizard resumes at the step the reported state implies, so a user who
 * reconnects mid-setup does not repeat completed steps within one phase.
 */

export const SETUP_NOT_STARTED = 0;
export const SETUP_ADMIN_ENROLLED = 1;
export const SETUP_REGISTERED = 2;
export const SETUP_NUKI_PAIRED = 3;
export const SETUP_COMPLETE = 4;

export type WizardStepId = 'enroll' | 'register' | 'nuki' | 'finish';

/**
 * Mirrors the firmware's SDF_SETUP_DEADLINE_MS: the hard bound on the whole
 * setup phase, started by the device at its FIRST accepted connection and
 * never extended by activity, disconnection or reconnection. When it fires
 * the device erases every partial result - templates, accounts, admission
 * records - and disarms the phase.
 *
 * The device does not report how much of it is left, so a client-side
 * countdown is an upper bound: it can only be measured from this browser's
 * own connect, and a reconnect within the same phase inherits a deadline
 * that started earlier. Present it as "at most", never as exact.
 */
export const SETUP_DEADLINE_MS = 600_000;

/** `m:ss` for a remaining-milliseconds value; clamped at zero. */
export function formatRemaining(ms: number): string {
	const total = Math.max(0, Math.ceil(ms / 1000));
	const minutes = Math.floor(total / 60);
	const seconds = total % 60;
	return `${minutes}:${String(seconds).padStart(2, '0')}`;
}

/** Decodes the setup-state characteristic value; null if unreadable. */
export function decodeSetupState(bytes: Uint8Array): number | null {
	if (bytes.byteLength < 1) return null;
	return bytes[0];
}

/**
 * The wizard step a given reported setup state resumes at, plus the
 * indicator line explaining it. Unknown states fall back to the first step.
 */
export function wizardStepForSetupState(setupState: number): {
	step: WizardStepId;
	indicator: string;
} {
	switch (setupState) {
		case SETUP_NOT_STARTED:
			return { step: 'enroll', indicator: 'Setup state: not started — begin with Admin enrolment.' };
		case SETUP_ADMIN_ENROLLED:
			return {
				step: 'register',
				indicator: 'Setup state: Admin enrolled — continue with account registration.'
			};
		case SETUP_REGISTERED:
			return {
				step: 'nuki',
				indicator: 'Setup state: account registered — continue with Nuki pairing.'
			};
		case SETUP_NUKI_PAIRED:
			return { step: 'finish', indicator: 'Setup state: Nuki paired — finish to claim the device.' };
		default:
			return { step: 'enroll', indicator: '' };
	}
}

/** Human name of an outstanding setup step as the device reports it. */
export function describeStep(step: string | null): string {
	switch (step) {
		case 'admin_enrollment':
			return 'Admin fingerprint enrolment';
		case 'registration':
			return 'account registration';
		case 'nuki_pairing':
			return 'Nuki pairing';
		case 'internal_error':
			return 'An internal device error';
		default:
			return 'A setup step';
	}
}

/**
 * Maps a rejected finish-setup's outstanding step to the wizard step to
 * show. `internal_error` is not a wizard step at all - the caller keeps the
 * user on the finish step for an on-the-spot retry.
 */
export function outstandingStepToWizardStep(step: string | null): WizardStepId | null {
	if (step === 'registration') return 'register';
	if (step === 'nuki_pairing') return 'nuki';
	if (step === 'admin_enrollment' || step === null) return 'enroll';
	return null;
}

/** What the Admin-enrolment step shows, derived from its phase alone. */
export interface EnrolPresentation {
	/** The one line the user reads first. */
	headline: string;
	/** The supporting line under it. */
	sub: string;
	/** 'none' renders as an instruction; 'ok'/'bad' as an outcome panel. */
	tone: 'none' | 'ok' | 'bad';
	/** Empty when the step is waiting on the device, not the user. */
	action: string;
	/** Whether the scan markers belong on screen. */
	markers: boolean;
}

/**
 * The single mapping from enrolment phase to what is on screen. Pure, so
 * the wizard never assembles wording inline and no phase can end up with
 * two competing messages.
 */
export function presentEnrol(state: {
	phase: 'idle' | 'starting' | 'scanning' | 'success' | 'failed';
	captured: number;
	expected: number;
	total: number;
	error: string;
}): EnrolPresentation {
	switch (state.phase) {
		case 'starting':
			return {
				headline: 'Asking the device to start…',
				sub: 'Do not touch the sensor yet.',
				tone: 'none',
				action: '',
				markers: false
			};
		case 'scanning':
			return {
				headline:
					state.captured === 0
						? "Place the admin's finger on the sensor"
						: 'Lift your finger, then place it again',
				sub: `Scan ${state.expected} of ${state.total} · hold still until the device confirms`,
				tone: 'none',
				action: '',
				markers: true
			};
		case 'success':
			return {
				headline: 'Admin fingerprint enrolled',
				sub: `All ${state.total} scans accepted.`,
				tone: 'ok',
				action: 'Continue to account registration',
				markers: true
			};
		case 'failed':
			return {
				headline: 'That did not work',
				sub: state.error,
				tone: 'bad',
				action: 'Try again',
				markers: false
			};
		default:
			// No headline: the section heading already names the step, and a
			// second copy of it above the button reads as a stutter.
			return {
				headline: '',
				sub: `This finger authorises every later step. You will place the same finger on the sensor ${state.total} times, one scan at a time.`,
				tone: 'none',
				action: 'Start enrolment',
				markers: false
			};
	}
}
