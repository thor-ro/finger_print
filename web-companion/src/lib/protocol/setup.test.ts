import { describe, expect, it } from 'vitest';
import {
	SETUP_ADMIN_ENROLLED,
	SETUP_COMPLETE,
	SETUP_NUKI_PAIRED,
	SETUP_NOT_STARTED,
	SETUP_REGISTERED,
	decodeSetupState,
	describeStep,
	outstandingStepToWizardStep,
	wizardStepForSetupState
} from './setup';

describe('setup-state decoding', () => {
	it('decodes the single-byte characteristic value', () => {
		expect(decodeSetupState(Uint8Array.of(SETUP_NOT_STARTED))).toBe(SETUP_NOT_STARTED);
		expect(decodeSetupState(Uint8Array.of(SETUP_ADMIN_ENROLLED))).toBe(SETUP_ADMIN_ENROLLED);
		expect(decodeSetupState(Uint8Array.of(SETUP_COMPLETE))).toBe(SETUP_COMPLETE);
	});

	it('returns null for an unreadable value', () => {
		expect(decodeSetupState(new Uint8Array(0))).toBeNull();
	});
});

describe('wizard resume cases', () => {
	it('not started begins with Admin enrolment', () => {
		const r = wizardStepForSetupState(SETUP_NOT_STARTED);
		expect(r.step).toBe('enroll');
		expect(r.indicator).toContain('not started');
	});

	it('Admin enrolled resumes at registration', () => {
		const r = wizardStepForSetupState(SETUP_ADMIN_ENROLLED);
		expect(r.step).toBe('register');
		expect(r.indicator).toContain('Admin enrolled');
	});

	it('account registered resumes at Nuki pairing', () => {
		const r = wizardStepForSetupState(SETUP_REGISTERED);
		expect(r.step).toBe('nuki');
		expect(r.indicator).toContain('registered');
	});

	it('Nuki paired resumes at finish', () => {
		const r = wizardStepForSetupState(SETUP_NUKI_PAIRED);
		expect(r.step).toBe('finish');
		expect(r.indicator).toContain('finish');
	});

	it('an unknown state falls back to the first step', () => {
		const r = wizardStepForSetupState(99);
		expect(r.step).toBe('enroll');
	});
});

describe('outstanding-step reporting', () => {
	it('maps each device-reported step back to its wizard step', () => {
		expect(outstandingStepToWizardStep('admin_enrollment')).toBe('enroll');
		expect(outstandingStepToWizardStep('registration')).toBe('register');
		expect(outstandingStepToWizardStep('nuki_pairing')).toBe('nuki');
		expect(outstandingStepToWizardStep(null)).toBe('enroll');
	});

	it('internal error is not a wizard step - retry on the spot instead', () => {
		expect(outstandingStepToWizardStep('internal_error')).toBeNull();
	});
});

describe('step descriptions', () => {
	it('describes every named step in plain language', () => {
		expect(describeStep('admin_enrollment')).toContain('enrolment');
		expect(describeStep('registration')).toContain('registration');
		expect(describeStep('nuki_pairing')).toContain('Nuki');
		expect(describeStep('internal_error')).toContain('internal');
		expect(describeStep('mystery')).toBe('A setup step');
	});
});
