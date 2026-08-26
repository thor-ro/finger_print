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
	s.wizardEnrollStatus = '';
	s.wizardEnrollProgressVisible = false;
	s.wizardEnrollStepText = 'Step 1 of 3';
	s.wizardEnrollPercent = 0;
	s.wizardEnrollMessage = '';
	s.wizardRegisterStatus = '';
	s.wizardNukiStatus = '';
	s.wizardFinishStatus = '';
	s.isRegistering = false;
	s.authStatus = '';
	s.boundUsername = '';
	s.healthReport = null;
	s.configEntries = [];
	s.configVisible = false;
	s.configStatus = '';
	s.enrollProgressVisible = false;
	s.enrollStepText = 'Step 1 of 3';
	s.enrollPercent = 0;
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
