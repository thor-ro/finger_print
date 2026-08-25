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
