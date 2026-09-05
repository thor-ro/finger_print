import { session } from '$lib/state/session.svelte';
import { INITIAL_CHUNK_SIZE } from '$lib/protocol/ota';

/**
 * Test-only reset for the session singleton. Lives outside the store class
 * so it is never imported by app code and never ships in the bundle (the
 * initial-load budget is tight enough that test helpers must not ride
 * along). Reaches the store's compile-time-private fields via casts: this
 * file is the one sanctioned place that may.
 */
export function resetSessionForTests(): void {
	/* eslint-disable @typescript-eslint/no-explicit-any */
	const s = session as any;
	s.transport = null;
	s.connectionStatus = '';
	s.connected = false;
	s.authenticated = false;
	s.setupState = null;
	s.setupCompleted = true;
	s.wizardStep = 'enroll';
	s.wizardIndicator = '';
	s.wizardEnrollPhase = 'idle';
	s.wizardEnrollError = '';
	s.wizardEnrollHint = '';
	s.wizardEnrollProgressVisible = false;
	s.wizardEnrollCaptured = 0;
	s.wizardEnrollExpected = 1;
	s.wizardEnrollTotal = 3;
	s.wizardRegisterStatus = '';
	s.wizardNukiStatus = '';
	s.wizardFinishStatus = '';
	// Clears the interval as well as the reactive remainder; the explicit
	// assignments below keep this file's per-field coverage checkable.
	s.stopWizardDeadline();
	s.wizardDeadlineRemainingMs = null;
	s.wizardDeadlineTimer = null;
	s.wizardDeadlineEndsAt = 0;
	s.isRegistering = false;
	s.authStatus = '';
	s.boundUsername = '';
	s.healthReport = null;
	s.configEntries = [];
	s.configVisible = false;
	s.configStatus = '';
	s.enrollProgressVisible = false;
	s.enrollCaptured = 0;
	s.enrollExpected = 0;
	s.enrollTotal = 3;
	s.enrollMessage = '';
	s.enrollResultText = '';
	s.enrollResultColor = '';
	s.umUsers = [];
	s.umStatus = '';
	s.umStatusRefusal = false;
	s.otaStatus = '';
	s.otaProgressVisible = false;
	s.otaProgressPercent = 0;
	s.nextUmRequestId = 1;
	s.pendingUmReplies?.clear();
	s.umListParts = [];
	s.pendingAdminAction = null;
	s.lastConfigNotifyRaw = null;
	s.wizardRegisterPending = false;
	s.otaPendingNotification = null;
	s.otaChunkSize = INITIAL_CHUNK_SIZE;
	s.otaResumeState = null;
	/* eslint-enable @typescript-eslint/no-explicit-any */
}
