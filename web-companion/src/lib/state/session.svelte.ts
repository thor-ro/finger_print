import * as auth from '$lib/protocol/auth';
import * as health from '$lib/protocol/health';
import * as ota from '$lib/protocol/ota';
import * as setup from '$lib/protocol/setup';
import * as um from '$lib/protocol/usermgmt';
import type { BleTransport, SdfCharacteristic } from '$lib/transport/ble';

/**
 * The session store: the single owner of connection, auth, wizard, health,
 * user-management and OTA state. The visible pane is DERIVED from this
 * state (see `view`) - nothing toggles display flags imperatively.
 *
 * All device interaction goes through the injected {@link BleTransport};
 * no module outside `lib/transport/` touches Web Bluetooth.
 */

// --- Timing constants (mirroring the firmware's own windows) ---

const LOGIN_DASHBOARD_DELAY_MS = 500;
// The device's own pending-admin-action timeout is 10s
// (SDF_ADMIN_ACTION_TIMEOUT_MS); wait a little longer client-side so the
// device's own timeout-driven denial reply has time to arrive first.
const ADMIN_ACTION_RESPONSE_TIMEOUT_MS = 12000;
const UM_REPLY_TIMEOUT_MS = 15000; // device scan window is 10s
const OTA_RESPONSE_TIMEOUT_MS = 10000;
// Grace period after END during which a `failed` notify or a disconnect
// (the expected successful-commit path) is awaited before the outcome is
// declared ambiguous.
const OTA_COMPLETION_GRACE_MS = 5000;

/**
 * Asks for one scan at a time. The device reports which scan it is waiting
 * for; nothing here advances on a timer, so the prompt can never claim a
 * scan the device has not confirmed.
 */
function scanPrompt(progress: um.EnrollProgress, whose: string): string {
	const { captured, step, total } = progress;
	if (captured === 0) {
		return `Place ${whose} on the sensor - scan ${step} of ${total}.`;
	}
	return `Scan ${captured} of ${total} captured. Lift the finger, then place it again for scan ${step} of ${total}.`;
}

export type ViewId = 'connection' | 'wizard' | 'auth' | 'dashboard';

/**
 * The one thing the Admin-enrolment step is doing right now. The wizard
 * renders exactly one instruction per phase, so the user is never left to
 * infer their next move from a status sentence:
 *
 *   idle      nothing started - explain, offer the button
 *   starting  request sent, device has not acknowledged yet
 *   scanning  device is waiting for a specific scan (captured/expected say which)
 *   success   the finger is enrolled; the user confirms before moving on
 *   failed    it did not work, and `wizardEnrollError` says why
 */
export type WizardEnrollPhase = 'idle' | 'starting' | 'scanning' | 'success' | 'failed';

export interface UmUser {
	id: number;
	name?: string;
	perm: number;
}

export type OtaOutcome = { outcome: 'success' | 'presumed-success' } | { outcome: 'failed'; error?: string } | { outcome: 'ambiguous' };

/** A chunk write was accepted but no device response arrived in time. */
class OtaResponseTimeoutError extends Error {
	constructor() {
		super('Timed out waiting for device response.');
		this.name = 'OtaResponseTimeoutError';
	}
}

// Consecutive no-response retries allowed before the transfer gives up.
const OTA_TIMEOUT_RETRIES = 3;

class SessionStore {
	transport: BleTransport | null = $state(null);

	// --- Connection ---
	connectionStatus = $state('');
	connected = $state(false);
	authenticated = $state(false);

	// --- Setup / wizard ---
	setupState = $state<number | null>(null);
	setupCompleted = $state(true); // true = not in setup phase
	wizardStep = $state<setup.WizardStepId>('enroll');
	wizardIndicator = $state('');
	wizardEnrollPhase = $state<WizardEnrollPhase>('idle');
	/** Why the enrolment failed, in the user's terms. Empty unless failed. */
	wizardEnrollError = $state('');
	/** What the user should do about a failure, when there is something. */
	wizardEnrollHint = $state('');
	wizardEnrollProgressVisible = $state(false);
	wizardEnrollCaptured = $state(0);
	wizardEnrollExpected = $state(1);
	wizardEnrollTotal = $state(um.ENROLL_DEFAULT_SCANS);
	wizardRegisterStatus = $state('');
	wizardNukiStatus = $state('');
	wizardFinishStatus = $state('');
	/**
	 * Milliseconds left of the device's setup deadline as measured from this
	 * browser's connect, or null while no countdown runs. An upper bound, not
	 * a reading of the device's own clock - see setup.SETUP_DEADLINE_MS.
	 */
	wizardDeadlineRemainingMs = $state<number | null>(null);

	// --- Auth view ---
	isRegistering = $state(false);
	authStatus = $state('');
	boundUsername = $state('');

	// --- Dashboard: health ---
	healthReport = $state<health.HealthReport | null>(null);

	// --- Dashboard: config ---
	configEntries = $state<Array<{ key: string; value: boolean | number; isEditable: boolean }>>([]);
	configVisible = $state(false);
	configStatus = $state('');

	// --- Dashboard: enrollment panel ---
	enrollProgressVisible = $state(false);
	enrollCaptured = $state(0);
	enrollExpected = $state(0);
	enrollTotal = $state(um.ENROLL_DEFAULT_SCANS);
	enrollMessage = $state('');
	enrollResultText = $state('');
	enrollResultColor = $state('');

	// --- Dashboard: user management ---
	umUsers = $state<UmUser[]>([]);
	umStatus = $state('');
	// True while umStatus carries a device refusal (a named non-ok outcome).
	// The refusal sentence itself still carries the meaning; the flag only
	// applies the reinforcing --danger/--danger-tint presentation.
	umStatusRefusal = $state(false);

	// --- OTA ---
	otaStatus = $state('');
	otaProgressVisible = $state(false);
	otaProgressPercent = $state(0);

	// --- Internal (non-reactive where possible) ---
	private nextUmRequestId = 1;
	private pendingUmReplies = new Map<number, { resolve: (v: um.Notification | null) => void }>();
	private umListParts: UmUser[] = [];
	private pendingAdminAction: { key: string; resolve: (v: boolean | null) => void } | null = null;
	private lastConfigNotifyRaw: um.Notification | null = null;
	private wizardRegisterPending = false;
	private wizardDeadlineTimer: ReturnType<typeof setInterval> | null = null;
	private wizardDeadlineEndsAt = 0;
	private otaPendingNotification: {
		resolve: (data: ota.DeviceStatus) => void;
		reject: (err: Error) => void;
		/** Drops replies that belong to an earlier, already-timed-out write. */
		accept?: (data: ota.DeviceStatus) => boolean;
	} | null = null;
	private otaChunkSize = ota.INITIAL_CHUNK_SIZE;
	private otaResumeState: { file: Blob; imageSize: number } | null = null;

	/**
	 * The visible pane is a function of connection, auth and setup state -
	 * never of imperative calls.
	 */
	get view(): ViewId {
		if (!this.connected) return 'connection';
		if (!this.setupCompleted && this.setupState !== setup.SETUP_COMPLETE) return 'wizard';
		if (this.authenticated) return 'dashboard';
		return 'auth';
	}

	// --- Connection lifecycle ---

	async connect(transportFactory: () => BleTransport): Promise<void> {
		try {
			this.connectionStatus = 'Requesting Bluetooth Device...';
			const transport = transportFactory();
			this.transport = transport;
			transport.onDisconnected(() => this.handleDisconnected());
			await transport.connect();
			this.connectionStatus = 'Connected successfully!';
			this.connected = true;

			for (const ch of ['auth', 'config', 'enroll', 'ota', 'status'] as SdfCharacteristic[]) {
				await transport.subscribe(ch, (data) => this.dispatch(ch, data));
			}

			// Read setup state BEFORE login: the wizard is mandatory for an
			// unclaimed device, and resumes at the step the reported state
			// implies.
			try {
				const v = await transport.read('setup_state');
				this.setupState = setup.decodeSetupState(v);
			} catch {
				console.warn('Could not read setup state');
				this.setupState = setup.SETUP_COMPLETE;
			}
			this.setupCompleted = this.setupState === setup.SETUP_COMPLETE;

			if (this.view === 'wizard') {
				this.enterWizard(this.setupState ?? setup.SETUP_NOT_STARTED);
				this.startWizardDeadline();
			}
			this.connectionStatus = '';
		} catch (error) {
			console.error(error);
			this.connectionStatus = `Error: ${(error as Error).message}`;
		}
	}

	private handleDisconnected(): void {
		const wizardWasActive = this.view === 'wizard';
		if (wizardWasActive && !this.setupCompleted) {
			/* The device disconnected without setup completing. If it stopped
			 * advertising, the setup window elapsed: progress was erased and
			 * the button must be pressed to re-arm before reconnecting. */
			this.wizardFinishStatus =
				'The connection was lost and setup did not complete. If the device ' +
				'is no longer visible in the device picker, its setup window elapsed ' +
				'and all progress was discarded — press the button on the device to ' +
				'start setup again.';
		}
		this.stopWizardDeadline();
		this.connectionStatus = 'Device disconnected.';
		this.connected = false;
		this.authenticated = false;
		this.healthReport = null;
	}

	disconnect(): void {
		this.transport?.disconnect();
	}

	// --- Wizard ---

	/** Resumes at the step the reported setup state implies. */
	private enterWizard(setupStateValue: number): void {
		this.setupCompleted = false;
		const resumed = setup.wizardStepForSetupState(setupStateValue);
		this.wizardStep = resumed.step;
		this.wizardIndicator = resumed.indicator;
	}

	/**
	 * Starts (or restarts) the client-side view of the device's setup
	 * deadline. Measured from this connect, so it is an upper bound on the
	 * time actually left - a reconnect inherits a deadline the device
	 * started earlier, and the device never reports the remainder.
	 */
	private startWizardDeadline(): void {
		this.stopWizardDeadline();
		this.wizardDeadlineEndsAt = Date.now() + setup.SETUP_DEADLINE_MS;
		this.tickWizardDeadline();
		this.wizardDeadlineTimer = setInterval(() => this.tickWizardDeadline(), 1000);
	}

	private stopWizardDeadline(): void {
		if (this.wizardDeadlineTimer !== null) {
			clearInterval(this.wizardDeadlineTimer);
			this.wizardDeadlineTimer = null;
		}
		this.wizardDeadlineRemainingMs = null;
	}

	private tickWizardDeadline(): void {
		const remaining = this.wizardDeadlineEndsAt - Date.now();
		this.wizardDeadlineRemainingMs = Math.max(0, remaining);
		if (remaining > 0) return;
		/* Stop ticking but keep the zero on screen: the device has, by now,
		 * wiped every partial result and disarmed the phase, and the button
		 * press that re-arms it is the only way forward. The connection may
		 * still look alive here - the device-side wipe runs before the
		 * BLE-side termination it queues. */
		if (this.wizardDeadlineTimer !== null) {
			clearInterval(this.wizardDeadlineTimer);
			this.wizardDeadlineTimer = null;
		}
	}

	/**
	 * The single writer of the Admin-enrolment step's state. Phase and the
	 * legacy status/visibility fields move together here so no caller can
	 * leave the two disagreeing - the mismatch that let a failed request sit
	 * under an opening status line forever.
	 */
	private setWizardEnrollPhase(
		phase: WizardEnrollPhase,
		detail: { error?: string; hint?: string; progress?: um.EnrollProgress } = {}
	): void {
		this.wizardEnrollPhase = phase;
		this.wizardEnrollError = detail.error ?? '';
		this.wizardEnrollHint = detail.hint ?? '';
		this.wizardEnrollProgressVisible = phase === 'starting' || phase === 'scanning';

		switch (phase) {
			case 'idle':
				this.wizardEnrollCaptured = 0;
				this.wizardEnrollExpected = 0;
				return;
			case 'starting':
				this.wizardEnrollCaptured = 0;
				this.wizardEnrollExpected = 0;
				this.wizardEnrollTotal = um.ENROLL_DEFAULT_SCANS;
				return;
			case 'scanning': {
				const progress = detail.progress ?? {
					captured: 0,
					step: 1,
					total: um.ENROLL_DEFAULT_SCANS
				};
				this.wizardEnrollCaptured = progress.captured;
				this.wizardEnrollExpected = progress.step;
				this.wizardEnrollTotal = progress.total;
				return;
			}
			case 'success':
				// Every marker filled before the step is left, so the count
				// never ends mid-way.
				this.wizardEnrollCaptured = this.wizardEnrollTotal;
				this.wizardEnrollExpected = 0;
				return;
			case 'failed':
				this.wizardEnrollExpected = 0;
				return;
		}
	}

	async wizardEnrollAdmin(): Promise<void> {
		this.setWizardEnrollPhase('starting');
		let result: um.Notification | null;
		try {
			result = await this.sendUmRequest({ verb: 'enroll', user_id: 1, permission: 3 });
		} catch (err) {
			/* The device answers an unadmitted enrolment write with an ATT
			 * error rather than a reply, which is what a lapsed setup phase
			 * looks like from here. Reported rather than swallowed: this
			 * handler is invoked as `void`, so an escaping rejection left the
			 * opening status line on screen with nothing else happening. */
			this.setWizardEnrollPhase('failed', {
				error: `The device refused to start enrolment (${(err as Error).message}).`,
				hint:
					'Its setup window has most likely lapsed. Press the button on the ' +
					'device to re-arm setup, then reconnect and start again.'
			});
			return;
		}
		if (result === null) {
			this.setWizardEnrollPhase('failed', {
				error: 'The device did not answer.',
				hint: 'Check that it is still powered and in range, then try again.'
			});
			return;
		}
		if (result.result === 'ok') {
			// Started. The device reports each captured scan from here; this
			// is only the opening prompt, and it is also what a device that
			// reports no progress at all leaves on screen.
			this.setWizardEnrollPhase('scanning');
			return;
		}
		this.setWizardEnrollPhase('failed', {
			error: um.umResultMessage(String(result.result)),
			hint: 'Nothing was stored on the device. You can try again.'
		});
	}

	/** Leaves the confirmed success screen for the next wizard step. */
	wizardEnrollContinue(): void {
		this.setWizardEnrollPhase('idle');
		this.showWizardStep('register');
		this.wizardIndicator = 'Admin enrolled - register your account below.';
	}

	/** Clears a failure so the step offers a clean retry. */
	wizardEnrollRetry(): void {
		this.setWizardEnrollPhase('idle');
	}

	async wizardRegister(username: string, password: string): Promise<void> {
		// Same ownership + replace warning as the dashboard register form:
		// the account will belong to whichever admin's finger confirms it,
		// and a confirming admin who already holds an account has it replaced.
		if (
			!window.confirm(
				`The account will belong to the admin who confirms it with a ` +
					`fingerprint scan. If that admin already holds a password, it ` +
					`will be replaced. Continue?`
			)
		) {
			this.wizardRegisterStatus = 'Registration cancelled - no existing password was changed.';
			return;
		}
		try {
			this.wizardRegisterStatus = 'Submitting registration...';
			const payload = auth.encodeRegister(username, await auth.hashPassword(password));
			this.wizardRegisterPending = true;
			await this.transport!.write('auth', payload);
			this.wizardRegisterStatus = 'Please scan the Admin Finger on the device to confirm.';
		} catch (err) {
			this.wizardRegisterPending = false;
			this.wizardRegisterStatus = `Error: ${(err as Error).message}`;
		}
	}

	async wizardNukiPair(): Promise<void> {
		try {
			this.wizardNukiStatus = 'Pairing with the Nuki lock...';
			const resultPromise = this.waitForAdminActionResult(
				'setup_nuki_pair',
				ADMIN_ACTION_RESPONSE_TIMEOUT_MS
			);
			await this.transport!.write(
				'config',
				new TextEncoder().encode(JSON.stringify({ action: 'setup_nuki_pair' }))
			);
			const paired = await resultPromise;
			if (paired === true) {
				this.wizardNukiStatus = 'Nuki pairing succeeded.';
				this.showWizardStep('finish');
				this.wizardIndicator = 'Nuki paired - finish below.';
			} else {
				this.wizardNukiStatus =
					'Nuki pairing did not succeed. Make sure the lock is in pairing mode and try again.';
			}
		} catch (err) {
			this.wizardNukiStatus = `Pairing request rejected: ${(err as Error).message}`;
		}
	}

	async wizardFinish(): Promise<void> {
		try {
			this.wizardFinishStatus = 'Finishing setup...';
			const resultPromise = this.waitForAdminActionResult(
				'finish_setup',
				ADMIN_ACTION_RESPONSE_TIMEOUT_MS
			);
			await this.transport!.write(
				'config',
				new TextEncoder().encode(JSON.stringify({ action: 'finish_setup' }))
			);
			const result = await resultPromise;
			if (result === true) {
				this.setupCompleted = true;
				this.stopWizardDeadline();
				this.wizardFinishStatus =
					'Setup complete! The device is now claimed and paired to this browser: it has ' +
					'switched to filtered advertising and will only accept reconnections from this companion.';
				this.wizardIndicator = '';
			} else if (result === null || result === undefined) {
				this.wizardFinishStatus = 'No response received - check the device.';
			} else {
				// Rejected: the device reports which step is still outstanding,
				// or 'internal_error' for a fault that is not a wizard step at
				// all. Sending the user back to redo a finished step would be
				// misleading, so offer a retry on the spot instead.
				const rawStep = this.lastConfigNotifyRaw?.step;
				const step = typeof rawStep === 'string' ? rawStep : null;
				if (step === 'internal_error') {
					this.wizardFinishStatus =
						'The device could not finish setup because of an internal error. ' +
						'No progress was lost - press Finish to try again.';
				} else {
					this.gotoOutstandingStep(step);
					this.wizardFinishStatus = `Setup cannot be finished yet: ${setup.describeStep(step)} is still outstanding.`;
				}
			}
		} catch (err) {
			this.wizardFinishStatus = `Finish request rejected: ${(err as Error).message}`;
		}
	}

	showWizardStep(step: setup.WizardStepId): void {
		this.wizardStep = step;
	}

	private gotoOutstandingStep(step: string | null): void {
		const target = setup.outstandingStepToWizardStep(step);
		if (target) this.showWizardStep(target);
	}

	// --- Auth (login / register) ---

	setRegistering(registering: boolean): void {
		this.isRegistering = registering;
	}

	async submitLogin(username: string, password: string): Promise<void> {
		try {
			this.authStatus = 'Authenticating...';
			try {
				await this.transport!.write('auth', auth.encodeLoginInit(username));
			} catch {
				this.protocolMismatchMessage();
				return;
			}

			let challengeBytes: Uint8Array;
			try {
				challengeBytes = await this.transport!.read('auth');
			} catch {
				this.protocolMismatchMessage();
				return;
			}
			let challenge: auth.LoginChallenge;
			try {
				challenge = auth.parseChallenge(challengeBytes);
			} catch {
				this.protocolMismatchMessage();
				return;
			}

			const stretched = await auth.stretchPassword(password, challenge.salt, challenge.iterations);
			const response = await auth.computeLoginResponse(stretched, challenge.nonce);

			try {
				await this.transport!.write('auth', auth.encodeLoginVerify(response));
			} catch {
				// Wrong username/password: the device rejects the write itself
				// (BLE_ATT_ERR_INSUFFICIENT_AUTHEN), so there's no success
				// notification to wait for. No information about whether the
				// username exists is revealed either way.
				this.authStatus = 'Incorrect username or password.';
				return;
			}

			this.boundUsername = username;
			// Success is confirmed asynchronously via the Auth notification.
			this.authStatus = 'Waiting for device...';
		} catch (err) {
			this.authStatus = `Error: ${(err as Error).message}`;
		}
	}

	private protocolMismatchMessage(): void {
		this.authStatus =
			'Login could not start - please make sure the companion app and device firmware are both up to date, then try again.';
	}

	async submitRegister(username: string, password: string): Promise<void> {
		// Re-registration replaces the confirming admin's existing credential
		// in place (companion-identity). Warn before submitting so this is a
		// deliberate choice, not an accidental overwrite.
		const confirmed = window.confirm(
			`Registering will bind the account to the admin whose fingerprint ` +
				`confirms it. If that admin already has a password, it will be ` +
				`replaced (this is the password-reset path). Continue?`
		);
		if (!confirmed) {
			this.authStatus = 'Registration cancelled - no existing password was changed.';
			return;
		}
		try {
			this.authStatus = 'Authenticating...';
			const payload = auth.encodeRegister(username, await auth.hashPassword(password));
			await this.transport!.write('auth', payload);
			this.boundUsername = username;
			this.authStatus = 'Please scan the Admin Finger on the device to confirm.';
		} catch (err) {
			this.authStatus = `Error: ${(err as Error).message}`;
		}
	}

	private handleAuthNotification(value: Uint8Array): void {
		const status = value[0];
		if (status === 0x01) {
			// Wizard registration: a successful REGISTER advances the wizard
			// instead of opening the dashboard.
			if (this.view === 'wizard' && this.wizardRegisterPending) {
				this.wizardRegisterPending = false;
				this.wizardRegisterStatus = 'Registration confirmed by the Admin fingerprint.';
				this.showWizardStep('nuki');
				this.wizardIndicator = 'Account registered - pair your Nuki lock.';
				return;
			}
			this.authStatus = 'Success!';
			setTimeout(() => {
				this.authenticated = true;
				void this.refreshDeviceHealth();
				void this.resumeOtaTransferIfPending();
			}, LOGIN_DASHBOARD_DELAY_MS);
		} else if (status === 0x02) {
			if (this.view !== 'wizard') this.authStatus = 'Pending admin authorization on device...';
		} else {
			if (this.view !== 'wizard') this.authStatus = 'Authentication failed or logged out.';
		}
	}

	// --- Notification routing ---

	private dispatch(characteristic: SdfCharacteristic, data: Uint8Array): void {
		switch (characteristic) {
			case 'auth':
				this.handleAuthNotification(data);
				break;
			case 'config':
				this.handleConfigNotification(data);
				break;
			case 'enroll':
				this.handleEnrollNotification(data);
				break;
			case 'ota':
				this.handleOtaNotification(data);
				break;
			case 'status':
				void this.handleStatusNotification(data);
				break;
		}
	}

	private async handleStatusNotification(data: Uint8Array): Promise<void> {
		// A zero-length notification is a CHANGE MARKER: the report did not
		// fit one notification at the negotiated MTU, so read the full value.
		if (data.byteLength === 0) {
			await this.refreshDeviceHealth();
			return;
		}
		try {
			this.healthReport = health.parseHealthReport(new TextDecoder().decode(data));
		} catch {
			console.warn('Status notification not valid JSON');
		}
	}

	async refreshDeviceHealth(): Promise<void> {
		try {
			const value = await this.transport!.read('status');
			this.healthReport = health.parseHealthReport(new TextDecoder().decode(value));
		} catch (err) {
			console.warn('Could not read device health:', err);
		}
	}

	// --- Config ---

	async readConfig(): Promise<void> {
		try {
			this.configStatus = 'Reading config...';
			const value = await this.transport!.read('config');
			const config = JSON.parse(new TextDecoder().decode(value)) as Record<string, unknown>;
			this.configEntries = Object.entries(config).map(([key, value]) => ({
				key,
				value: value as boolean | number,
				isEditable: !['nuki_target_addr_type', 'nuki_target_addr'].includes(key)
			}));
			this.configVisible = true;
			this.configStatus = 'Config read successfully';
		} catch (err) {
			console.error(err);
			this.configStatus = `Error reading config: ${(err as Error).message}`;
		}
	}

	async applyConfig(): Promise<void> {
		try {
			const delta: Record<string, boolean | number> = {};
			for (const entry of this.configEntries) delta[entry.key] = entry.value;
			await this.transport!.write('config', new TextEncoder().encode(JSON.stringify(delta)));
			this.configStatus = 'Config applied successfully';
		} catch (err) {
			console.error(err);
			this.configStatus = `Error applying config: ${(err as Error).message}`;
		}
	}

	// --- BLE-triggered admin actions (Nuki re-pair, Zigbee Join) ---

	/**
	 * Writes {"action":key} and waits for the matching {key:true|false} reply
	 * (or the client-side timeout). Only one such request can be pending at
	 * a time (mirrors the device's single-pending-admin-action invariant).
	 */
	requestAdminAction(
		key: string,
		pendingMessage: string,
		authorizedMessage: string,
		statusSetter: (msg: string) => void
	): Promise<void> {
		statusSetter(pendingMessage);
		const resultPromise = this.waitForAdminActionResult(key, ADMIN_ACTION_RESPONSE_TIMEOUT_MS);
		return (async () => {
			try {
				await this.transport!.write(
					'config',
					new TextEncoder().encode(JSON.stringify({ action: key }))
				);
				const authorized = await resultPromise;
				if (authorized === true) {
					statusSetter(authorizedMessage);
				} else if (authorized === false) {
					statusSetter('Request denied or timed out.');
				} else {
					statusSetter('No response received - check the device.');
				}
			} catch (err) {
				console.error(err);
				this.pendingAdminAction = null;
				statusSetter(`Request rejected: ${(err as Error).message}.`);
			}
		})();
	}

	private waitForAdminActionResult(key: string, timeoutMs: number): Promise<boolean | null> {
		return new Promise((resolve) => {
			const timer = setTimeout(() => {
				this.pendingAdminAction = null;
				resolve(null); // no response in time - treated as ambiguous, not denied
			}, timeoutMs);

			this.pendingAdminAction = {
				key,
				resolve(authorized) {
					clearTimeout(timer);
					resolve(authorized);
				}
			};
		});
	}

	private handleConfigNotification(data: Uint8Array): void {
		const jsonStr = new TextDecoder().decode(data);
		try {
			const config = JSON.parse(jsonStr) as um.Notification;
			if (
				this.pendingAdminAction &&
				config[this.pendingAdminAction.key] !== undefined
			) {
				// Full JSON body kept so callers can read extra fields
				// (e.g. finish_setup's "step") beyond the boolean value.
				this.lastConfigNotifyRaw = config;
				this.pendingAdminAction.resolve(config[this.pendingAdminAction.key] === true);
				this.pendingAdminAction = null;
				return;
			}
		} catch {
			console.warn('Config notification not valid JSON:', jsonStr);
		}
	}

	// --- Companion user management ---

	private sendUmRequest(request: um.UmRequest): Promise<um.Notification | null> {
		const req = this.nextUmRequestId++;
		const payload = um.encodeUmRequest(req, request);
		const replyPromise = new Promise<um.Notification | null>((resolve) => {
			const timer = setTimeout(() => {
				this.pendingUmReplies.delete(req);
				resolve(null); // timed out client-side: ambiguous, not denied
			}, UM_REPLY_TIMEOUT_MS);
			this.pendingUmReplies.set(req, {
				resolve: (result) => {
					clearTimeout(timer);
					this.pendingUmReplies.delete(req);
					resolve(result);
				}
			});
		});
		return this.transport!.write('enroll', payload).then(() => replyPromise);
	}

	listUsers(): Promise<'ok' | 'no-response' | 'error'> {
		return (async () => {
			this.umStatus = 'Requesting user list...';
			this.umStatusRefusal = false;
			let result: um.Notification | null;
			try {
				result = await this.sendUmRequest({ verb: 'list' });
			} catch (err) {
				/* A rejected write never reached the device - distinct from
				 * the silence a timeout reports. Callers invoke this as
				 * `void`, so an escaping rejection would leave the request
				 * line on screen with nothing to follow it. */
				console.error(err);
				this.umStatus = `The device rejected the request: ${(err as Error).message}`;
				this.umStatusRefusal = true;
				return 'error';
			}
			// The terminal signal is the final list part's end marker; the
			// resolved value is null only on a client-side timeout.
			if (result === null) {
				this.umStatus = 'No response received - check the device.';
				return 'no-response';
			}
			this.umStatus = `Listed ${this.umUsers.length} user(s).`;
			return 'ok';
		})();
	}

	private async refreshUmUsersSilently(): Promise<void> {
		/* Self-contained: this runs after a change has already reported its
		 * own outcome, so a failed refresh must not reject into the caller's
		 * catch and overwrite that outcome with an error. The list simply
		 * stays as it was, and the user can refresh it. */
		try {
			await this.sendUmRequest({ verb: 'list' });
			// users applied by the list-part handler
		} catch (err) {
			console.error(err);
		}
	}

	/** An admin may demote or delete themselves; warn first. */
	warnIfSelf(targetId: number, what: string): boolean {
		const self = this.umUsers.find((u) => u.id === targetId);
		if (self && self.name && self.name === this.boundUsername) {
			return window.confirm(
				`You are about to ${what} your OWN user (${self.name}). Your ` +
					`session will lose its authority immediately. Continue?`
			);
		}
		return true;
	}

	/**
	 * Renders a user-management terminal outcome: the ok message on
	 * success, otherwise the outcome's own named refusal sentence, marked
	 * as a refusal so every theme presents it differently from ordinary
	 * status text.
	 */
	private setUmOutcome(result: string, okMessage: string): void {
		if (result === 'ok') {
			this.umStatus = okMessage;
			this.umStatusRefusal = false;
		} else {
			this.umStatus = um.umResultMessage(result);
			this.umStatusRefusal = true;
		}
	}

	async deleteUser(userId: number): Promise<void> {
		if (!this.warnIfSelf(userId, 'delete')) {
			this.umStatus = 'Cancelled.';
			return;
		}
		try {
			this.umStatus =
				`Requesting delete of user ${userId}... an Admin must scan ` +
				'their fingerprint on the device to authorize it.';
			const result = await this.sendUmRequest({ verb: 'delete', user_id: userId });
			if (result === null) {
				this.umStatus = 'No response received - check the device.';
				this.umStatusRefusal = false;
				return;
			}
			this.setUmOutcome(String(result.result), `User ${userId} deleted.`);
			await this.refreshUmUsersSilently();
		} catch (err) {
			console.error(err);
			this.umStatus = `Error: ${(err as Error).message}`;
		}
	}

	async renameUser(userId: number, currentName: string): Promise<void> {
		const name = window.prompt('New name (must be unique - it is the login identifier):', currentName || '');
		if (!name) return;
		try {
			this.umStatus =
				`Requesting rename of user ${userId}... an Admin must scan ` +
				'their fingerprint on the device to authorize it.';
			const result = await this.sendUmRequest({ verb: 'rename', user_id: userId, name });
			if (result === null) {
				this.umStatus = 'No response received - check the device.';
				this.umStatusRefusal = false;
				return;
			}
			this.setUmOutcome(String(result.result), `User ${userId} renamed to "${name}".`);
			await this.refreshUmUsersSilently();
		} catch (err) {
			console.error(err);
			this.umStatus = `Error: ${(err as Error).message}`;
		}
	}

	async changePermission(userId: number, currentPerm: number): Promise<void> {
		const value = window.prompt('New permission: 1 = Standard, 3 = Admin', String(currentPerm));
		const perm = parseInt(value ?? '', 10);
		if (perm !== 1 && perm !== 3) {
			this.umStatus = 'Only Standard (1) and Admin (3) can be assigned.';
			return;
		}
		if (!this.warnIfSelf(userId, 'demote')) {
			this.umStatus = 'Cancelled.';
			return;
		}
		try {
			this.umStatus =
				`Requesting permission change for user ${userId}... an Admin ` +
				'must scan their fingerprint on the device to authorize it ' +
				'(this can take up to ~15 seconds).';
			const result = await this.sendUmRequest({
				verb: 'set_permission',
				user_id: userId,
				permission: perm
			});
			if (result === null) {
				this.umStatus = 'No response received - check the device.';
				this.umStatusRefusal = false;
				return;
			}
			this.setUmOutcome(String(result.result), `Permission updated for user ${userId}.`);
			await this.refreshUmUsersSilently();
		} catch (err) {
			console.error(err);
			this.umStatus = `Error: ${(err as Error).message}`;
		}
	}

	/** Dashboard enrollment panel. Four scans: one admin + three enrolment. */
	async enroll(userId: number, permission: number): Promise<boolean> {
		if (userId < 1 || userId > 10) {
			this.enrollResultColor = '';
			this.enrollResultText = 'User ID must be 1-10';
			return false;
		}
		if (permission !== 1 && permission !== 3) {
			this.enrollResultColor = '';
			this.enrollResultText = 'Only Standard (1) and Admin (3) can be assigned.';
			return false;
		}

		this.enrollResultText = '';
		this.enrollProgressVisible = true;
		this.enrollCaptured = 0;
		// No scan of the new user's finger is expected yet - the authorizing
		// Admin scan comes first, and it is not one of the three.
		this.enrollExpected = 0;
		this.enrollTotal = um.ENROLL_DEFAULT_SCANS;
		this.enrollMessage = 'Waiting for the authorizing Admin scan on the device...';

		let result: um.Notification | null;
		try {
			result = await this.sendUmRequest({ verb: 'enroll', user_id: userId, permission });
		} catch (err) {
			/* Same hole the wizard's enrolment had: invoked as `void`, so an
			 * escaping rejection froze the panel on its opening message. */
			console.error(err);
			this.enrollProgressVisible = false;
			this.enrollExpected = 0;
			this.enrollResultColor = 'var(--danger)';
			this.enrollResultText = `The device rejected the request: ${(err as Error).message}`;
			return false;
		}
		if (result === null) {
			this.enrollProgressVisible = false;
			this.enrollResultText = 'No response received - check the device.';
			return false;
		}
		if (result.result === 'ok') {
			// Enrolment started: progress notifications take it from here.
			// This wording is also what a device that reports no progress
			// leaves on screen, so it states the scan count itself.
			this.enrollExpected = 1;
			this.enrollMessage = `Authorized - the new user now scans ${this.enrollTotal} times.`;
			return true;
		}
		this.enrollProgressVisible = false;
		this.enrollResultColor = 'var(--danger)';
		this.enrollResultText = um.umResultMessage(String(result.result));
		return false;
	}

	private handleEnrollNotification(data: Uint8Array): void {
		const parsed = um.decodeNotification(data);
		if (!parsed) {
			console.warn('Enroll notification not valid JSON');
			return;
		}

		// Terminal replies and list parts are correlated by request id.
		if (um.isListPart(parsed)) {
			this.handleUmListPart(parsed);
			return;
		}
		if (um.isUmReply(parsed)) {
			const req = um.requestIdOf(parsed);
			const pending = req !== undefined ? this.pendingUmReplies.get(req) : undefined;
			if (pending) pending.resolve(parsed);
			else console.warn('UM reply for unknown request id:', req);
			return;
		}

		// Wizard step 1 uses the same notifications as the dashboard panel.
		if (this.view === 'wizard') {
			this.handleWizardEnrollNotification(parsed);
			return;
		}

		if (parsed.status === 'success') {
			this.enrollCaptured = this.enrollTotal;
			this.enrollExpected = 0;
			this.enrollProgressVisible = false;
			this.enrollResultColor = 'var(--ok)';
			this.enrollResultText = `Enrollment successful! User ID: ${parsed.user_id}`;
		} else if (parsed.status === 'failed') {
			this.enrollProgressVisible = false;
			this.enrollExpected = 0;
			this.enrollResultColor = 'var(--danger)';
			this.enrollResultText = `Enrollment failed at step ${parsed.step}: error ${parsed.error_code}`;
		} else if (um.isEnrollProgress(parsed)) {
			const progress = um.enrollProgressOf(parsed);
			this.enrollProgressVisible = true;
			this.enrollCaptured = progress.captured;
			this.enrollExpected = progress.step;
			this.enrollTotal = progress.total;
			this.enrollMessage = scanPrompt(progress, "the new user's finger");
		}
	}

	private handleWizardEnrollNotification(parsed: um.Notification): void {
		if (parsed.status === 'success') {
			/* Deliberately does NOT advance the wizard: the confirmation is
			 * the point, and auto-advancing replaced it with the next step's
			 * form before it could be read. wizardEnrollContinue() moves on. */
			this.setWizardEnrollPhase('success');
		} else if (parsed.status === 'failed') {
			this.setWizardEnrollPhase('failed', {
				error: `Enrolment failed at scan ${parsed.step} of ${this.wizardEnrollTotal}.`,
				hint:
					'Place the same finger flat over the whole sensor, hold still ' +
					'until it confirms, and lift between scans.'
			});
		} else if (um.isEnrollProgress(parsed)) {
			this.setWizardEnrollPhase('scanning', { progress: um.enrollProgressOf(parsed) });
		}
	}

	private handleUmListPart(data: um.Notification): void {
		this.umListParts.push(...um.usersOf(data));
		if (data.end === true) {
			this.umUsers = this.umListParts;
			this.umListParts = [];
			// The final part IS the list verb's terminal reply.
			const req = um.requestIdOf(data);
			const pending = req !== undefined ? this.pendingUmReplies.get(req) : undefined;
			if (pending) pending.resolve({ req: req!, result: 'ok' });
		}
	}

	// --- OTA ---

	startOta(file: File): Promise<void> {
		return (async () => {
			if (this.otaResumeState) {
				this.otaStatus = 'A firmware transfer is already in progress.';
				return;
			}

			// State the device's REPORTED battery level alongside the warning.
			// When no measurement is available, say so - never imply the level
			// was checked, and never substitute the configured default.
			const batteryLine = health.batteryLineForOtaWarning(this.healthReport);
			const confirmed = window.confirm(
				`${batteryLine} Ensure your battery is above 20%. OTA transfer over ` +
					'Bluetooth draws significant power and firmware is large — keep the ' +
					'app open and the device nearby until it completes.\n\n' +
					'Start the firmware update now?'
			);
			if (!confirmed) return;

			await this.performOtaTransfer(file, file.size);
		})();
	}

	private async performOtaTransfer(file: Blob, imageSize: number): Promise<void> {
		this.otaResumeState = { file, imageSize };
		this.otaChunkSize = ota.INITIAL_CHUNK_SIZE;

		try {
			this.otaStatus = 'Starting OTA transfer...';
			this.otaProgressVisible = true;
			this.otaProgressPercent = 0;

			const startOffset = await this.beginOtaTransfer(imageSize);
			await this.sendOtaChunks(file, imageSize, startOffset);
			const result = await this.endOtaTransferAndAwaitOutcome();
			this.otaResumeState = null;

			if (result.outcome === 'failed') {
				this.otaStatus = `OTA failed: ${result.error || 'unknown error'}`;
				this.otaProgressVisible = false;
			} else if (result.outcome === 'ambiguous') {
				this.otaStatus =
					'OTA outcome unknown — no confirmation received before the timeout. Check the device.';
			} else {
				// 'success' or 'presumed-success'
				this.otaProgressPercent = 100;
				this.otaStatus =
					'OTA transfer complete. Reconnect once the device restarts to confirm the new firmware version.';
			}
		} catch (err) {
			if (this.transport?.isConnected()) {
				// A real, non-disconnect failure: no point auto-resuming.
				this.otaResumeState = null;
				this.otaStatus = `Error: ${(err as Error).message}`;
				this.otaProgressVisible = false;
			} else {
				// Connection lost mid-transfer: keep resume state so the
				// transfer continues once the user reconnects and re-authenticates.
				this.otaStatus =
					'Connection lost during firmware transfer. Reconnect and log in again to resume.';
			}
		}
	}

	private async resumeOtaTransferIfPending(): Promise<void> {
		if (!this.otaResumeState) return;
		const { file, imageSize } = this.otaResumeState;
		this.otaStatus = 'Resuming firmware transfer...';
		await this.performOtaTransfer(file, imageSize);
	}

	private async beginOtaTransfer(imageSize: number): Promise<number> {
		const data = await this.writeOtaOpcodeAndAwaitNotification(
			ota.beginPayload(imageSize),
			OTA_RESPONSE_TIMEOUT_MS,
			// A chunk acknowledgement arriving now is the late reply to a write
			// that already timed out - never this BEGIN's answer.
			(d) => d.status !== 'chunk_ack'
		);
		return ota.resumeOffsetFromReady(data);
	}

	private async sendOtaChunks(file: Blob, imageSize: number, startOffset: number): Promise<void> {
		let offset = startOffset;
		let timeoutRetries = 0;

		while (offset < imageSize) {
			const range = ota.nextChunkRange(offset, imageSize, this.otaChunkSize);
			const chunkBytes = new Uint8Array(await file.slice(range.start, range.end).arrayBuffer());

			let ack: ota.DeviceStatus;
			try {
				ack = await this.writeOtaOpcodeAndAwaitNotification(
					ota.chunkPayload(chunkBytes),
					OTA_RESPONSE_TIMEOUT_MS
				);
			} catch (err) {
				if (err instanceof OtaResponseTimeoutError) {
					// No response in time: NOT an over-MTU rejection, so halving the
					// chunk would not help. Nor can the chunk simply be re-sent: it
					// may have been written and only its acknowledgement lost, and a
					// CHUNK carries no offset, so re-sending would append the same
					// bytes twice. Only the device knows how much it holds - re-issue
					// BEGIN, which a size-matching device answers as a resume with its
					// confirmed offset, and continue from there.
					timeoutRetries++;
					if (timeoutRetries > OTA_TIMEOUT_RETRIES) {
						throw new Error(
							`No response after ${OTA_TIMEOUT_RETRIES} retries at offset ${offset}. Reconnect and start again - the transfer resumes from the reported offset.`
						);
					}
					this.otaStatus = `No response - resyncing with the device (${timeoutRetries}/${OTA_TIMEOUT_RETRIES})...`;
					offset = await this.beginOtaTransfer(imageSize);
					continue;
				}
				if (this.transport?.isConnected() && this.otaChunkSize > ota.MIN_CHUNK_SIZE) {
					// Rejected as over-MTU: halve the chunk size and retry from
					// the same (last confirmed) offset - do not advance offset.
					this.otaChunkSize = ota.halveChunkSize(this.otaChunkSize);
					console.warn(`OTA chunk write rejected; retrying with ${this.otaChunkSize}-byte chunks.`, err);
					continue;
				}
				throw err;
			}

			timeoutRetries = 0;
			if (ack.status === 'failed') {
				throw new Error(ack.error || 'Device reported an OTA chunk write failure.');
			}
			if (ack.status !== 'chunk_ack' || typeof ack.offset !== 'number') {
				throw new Error(`Unexpected response to OTA chunk write: ${JSON.stringify(ack)}`);
			}

			offset = ack.offset;
			this.otaProgressPercent = Math.round((offset / imageSize) * 100);
			this.otaStatus = `Uploading firmware... ${this.otaProgressPercent}%`;
		}
	}

	/**
	 * After END, races a `failed` notify against a disconnect within a bounded
	 * grace window. The device's successful-commit path reboots immediately
	 * after committing, so it never sends a `success` notify - a clean
	 * disconnect with no `failed` notify in the window IS the expected
	 * success signal, not an error.
	 */
	private endOtaTransferAndAwaitOutcome(): Promise<OtaOutcome> {
		const transport = this.transport!;
		const payload = ota.endPayload();

		this.otaStatus = 'Verifying and installing — the device will restart...';

		return new Promise<OtaOutcome>((outerResolve, outerReject) => {
			let settled = false;
			let graceTimer: ReturnType<typeof setTimeout> | null = null;
			let unsubscribe: (() => void) | null = null;

			const finishResolve = (arg: OtaOutcome) => {
				if (settled) return;
				settled = true;
				if (graceTimer) clearTimeout(graceTimer);
				unsubscribe?.();
				this.otaPendingNotification = null;
				outerResolve(arg);
			};
			const finishReject = (err: Error) => {
				if (settled) return;
				settled = true;
				if (graceTimer) clearTimeout(graceTimer);
				unsubscribe?.();
				this.otaPendingNotification = null;
				outerReject(err);
			};

			unsubscribe = transport.onDisconnected(() => {
				this.connectionStatus =
					'Device disconnected after OTA end-transfer — this is expected on success. Reconnect to confirm the new firmware version.';
				finishResolve({ outcome: 'presumed-success' });
			});

			this.otaPendingNotification = {
				resolve: (data) => {
					if (data.status === 'failed') {
						finishResolve({ outcome: 'failed', error: data.error });
					} else {
						// Unreachable in practice - the device reboots before a
						// `success` notify can be sent - but honor it if seen.
						finishResolve({ outcome: 'success' });
					}
				},
				reject: () => {
					/* no-op: the writeValue().catch() below handles write errors */
				}
			};

			graceTimer = setTimeout(() => {
				finishResolve({ outcome: 'ambiguous' });
			}, OTA_COMPLETION_GRACE_MS);

			transport.write('ota', payload).catch((err) => finishReject(err as Error));
		});
	}

	private async writeOtaOpcodeAndAwaitNotification(
		payload: Uint8Array,
		timeoutMs: number,
		accept?: (data: ota.DeviceStatus) => boolean
	): Promise<ota.DeviceStatus> {
		const notificationPromise = new Promise<ota.DeviceStatus>((resolve, reject) => {
			const timer = setTimeout(() => {
				this.otaPendingNotification = null;
				reject(new OtaResponseTimeoutError());
			}, timeoutMs);

			this.otaPendingNotification = {
				resolve: (data) => {
					clearTimeout(timer);
					this.otaPendingNotification = null;
					resolve(data);
				},
				reject: (err) => {
					clearTimeout(timer);
					this.otaPendingNotification = null;
					reject(err);
				},
				accept
			};
		});
		// The write's catch below can reject this promise before the caller's
		// await attaches a handler; a no-op catch here keeps that rejection
		// from being reported as unhandled (the real await still sees it).
		notificationPromise.catch(() => {});
		try {
			await this.transport!.write('ota', payload);
		} catch (err) {
			// Malformed, oversized, or out-of-session writes never produce a
			// notify at all, so a rejected write must reject the pending wait
			// rather than leave it hanging.
			this.otaPendingNotification?.reject(err as Error);
			throw err;
		}
		return notificationPromise;
	}

	private handleOtaNotification(data: Uint8Array): void {
		const parsed = um.decodeNotification(data);
		if (!parsed) {
			console.warn('OTA notification not valid JSON');
			return;
		}
		if (this.otaPendingNotification) {
			if (this.otaPendingNotification.accept?.(parsed) === false) {
				// Stale reply to an earlier write: dropping it keeps the pending
				// wait paired with its own response instead of running a chunk
				// behind for the rest of the transfer.
				console.warn('Discarding stale OTA notification:', parsed);
				return;
			}
			this.otaPendingNotification.resolve(parsed);
		} else {
			console.warn('Unsolicited OTA notification:', parsed);
		}
	}
	/**
	 * Test isolation: the store is a module singleton, so component tests
	 * must reset it between tests. Implemented in a test-only helper
	 * (src/lib/testing/session-reset.ts) so it never ships in the bundle.
	 */
}

export const session = new SessionStore();
