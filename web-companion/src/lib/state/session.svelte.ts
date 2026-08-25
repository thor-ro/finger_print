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

export type ViewId = 'connection' | 'wizard' | 'auth' | 'dashboard';

export interface UmUser {
	id: number;
	name?: string;
	perm: number;
}

export type OtaOutcome = { outcome: 'success' | 'presumed-success' } | { outcome: 'failed'; error?: string } | { outcome: 'ambiguous' };

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
	wizardEnrollStatus = $state('');
	wizardEnrollProgressVisible = $state(false);
	wizardEnrollStepText = $state('Step 1 of 3');
	wizardEnrollPercent = $state(0);
	wizardEnrollMessage = $state('');
	wizardRegisterStatus = $state('');
	wizardNukiStatus = $state('');
	wizardFinishStatus = $state('');

	// --- Auth view ---
	isRegistering = $state(false);
	authStatus = $state('');
	boundUsername = $state('');

	// --- Dashboard: health ---
	healthReport = $state<health.HealthReport | null>(null);

	// --- Dashboard: config ---
	configEntries = $state<Array<{ key: string; value: boolean | number; isEditable: boolean }>>([]);
	configVisible = $state(false);

	// --- Dashboard: enrollment panel ---
	enrollProgressVisible = $state(false);
	enrollStepText = $state('Step 1 of 3');
	enrollPercent = $state(0);
	enrollMessage = $state('');
	enrollResultText = $state('');
	enrollResultColor = $state('');

	// --- Dashboard: user management ---
	umUsers = $state<UmUser[]>([]);
	umStatus = $state('');

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
	private otaPendingNotification: {
		resolve: (data: ota.DeviceStatus) => void;
		reject: (err: Error) => void;
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

	async wizardEnrollAdmin(): Promise<void> {
		this.wizardEnrollStatus = 'Starting Admin enrolment...';
		const result = await this.sendUmRequest({ verb: 'enroll', user_id: 1, permission: 3 });
		if (result === null) {
			this.wizardEnrollStatus = 'No response received from the device - try again.';
			return;
		}
		if (result.result === 'ok') {
			this.wizardEnrollProgressVisible = true;
			this.wizardEnrollStepText = 'Step 1 of 3';
			this.wizardEnrollPercent = 33;
			this.wizardEnrollMessage =
				'Place the admin finger on the sensor for each of the 3 scans...';
			return; // progress notifications take it from here
		}
		this.wizardEnrollStatus = um.umResultMessage(String(result.result));
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
			this.otaStatus = 'Reading config...';
			const value = await this.transport!.read('config');
			const config = JSON.parse(new TextDecoder().decode(value)) as Record<string, unknown>;
			this.configEntries = Object.entries(config).map(([key, value]) => ({
				key,
				value: value as boolean | number,
				isEditable: !['nuki_target_addr_type', 'nuki_target_addr'].includes(key)
			}));
			this.configVisible = true;
			this.otaStatus = 'Config read successfully';
		} catch (err) {
			console.error(err);
			this.otaStatus = `Error reading config: ${(err as Error).message}`;
		}
	}

	async applyConfig(): Promise<void> {
		try {
			const delta: Record<string, boolean | number> = {};
			for (const entry of this.configEntries) delta[entry.key] = entry.value;
			await this.transport!.write('config', new TextEncoder().encode(JSON.stringify(delta)));
			this.otaStatus = 'Config applied successfully';
		} catch (err) {
			console.error(err);
			this.otaStatus = `Error applying config: ${(err as Error).message}`;
		}
	}

	// --- BLE-triggered admin actions (Nuki re-pair, Enroll-Admin, Zigbee Join) ---

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

	listUsers(): Promise<'ok' | 'no-response'> {
		return (async () => {
			this.umStatus = 'Requesting user list...';
			const result = await this.sendUmRequest({ verb: 'list' });
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
		const result = await this.sendUmRequest({ verb: 'list' });
		if (result !== null) {
			// users applied by the list-part handler
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
				return;
			}
			this.umStatus =
				result.result === 'ok'
					? `User ${userId} deleted.`
					: um.umResultMessage(String(result.result));
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
				return;
			}
			this.umStatus =
				result.result === 'ok'
					? `User ${userId} renamed to "${name}".`
					: um.umResultMessage(String(result.result));
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
				return;
			}
			this.umStatus =
				result.result === 'ok'
					? `Permission updated for user ${userId}.`
					: um.umResultMessage(String(result.result));
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
		this.enrollMessage = 'Waiting for the authorizing Admin scan on the device...';

		const result = await this.sendUmRequest({ verb: 'enroll', user_id: userId, permission });
		if (result === null) {
			this.enrollProgressVisible = false;
			this.enrollResultText = 'No response received - check the device.';
			return false;
		}
		if (result.result === 'ok') {
			// Enrolment started: progress notifications take it from here.
			this.enrollMessage = 'Authorized - follow the prompts: the new user scans three times.';
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
			this.enrollProgressVisible = false;
			this.enrollResultColor = 'var(--ok)';
			this.enrollResultText = `Enrollment successful! User ID: ${parsed.user_id}`;
		} else if (parsed.status === 'failed') {
			this.enrollProgressVisible = false;
			this.enrollResultColor = 'var(--danger)';
			this.enrollResultText = `Enrollment failed at step ${parsed.step}: error ${parsed.error_code}`;
		} else if (parsed.step !== undefined) {
			const step = Number(parsed.step);
			const maxSteps = 3;
			this.enrollStepText = `Step ${step} of ${maxSteps}`;
			this.enrollPercent = Math.round((step / maxSteps) * 100);
			this.enrollMessage = `Place finger for step ${step}...`;
		}
	}

	private handleWizardEnrollNotification(parsed: um.Notification): void {
		if (parsed.status === 'success') {
			this.wizardEnrollProgressVisible = false;
			this.wizardEnrollStatus = 'Admin enrolment successful.';
			this.showWizardStep('register');
			this.wizardIndicator = 'Admin enrolled - register your account below.';
		} else if (parsed.status === 'failed') {
			this.wizardEnrollProgressVisible = false;
			this.wizardEnrollStatus =
				`Enrolment failed at step ${parsed.step} (error ${parsed.error_code}) - try again.`;
		} else if (parsed.step !== undefined) {
			const maxSteps = 3;
			this.wizardEnrollStepText = `Step ${parsed.step} of ${maxSteps}`;
			this.wizardEnrollPercent = Math.round((Number(parsed.step) / maxSteps) * 100);
			this.wizardEnrollMessage = `Place finger for scan ${parsed.step} of ${maxSteps}...`;
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
			OTA_RESPONSE_TIMEOUT_MS
		);
		return ota.resumeOffsetFromReady(data);
	}

	private async sendOtaChunks(file: Blob, imageSize: number, startOffset: number): Promise<void> {
		let offset = startOffset;

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
				if (this.transport?.isConnected() && this.otaChunkSize > ota.MIN_CHUNK_SIZE) {
					// Rejected as over-MTU: halve the chunk size and retry from
					// the same (last confirmed) offset - do not advance offset.
					this.otaChunkSize = ota.halveChunkSize(this.otaChunkSize);
					console.warn(`OTA chunk write rejected; retrying with ${this.otaChunkSize}-byte chunks.`, err);
					continue;
				}
				throw err;
			}

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
		timeoutMs: number
	): Promise<ota.DeviceStatus> {
		const notificationPromise = new Promise<ota.DeviceStatus>((resolve, reject) => {
			const timer = setTimeout(() => {
				this.otaPendingNotification = null;
				reject(new Error('Timed out waiting for device response.'));
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
				}
			};
		});
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
			this.otaPendingNotification.resolve(parsed);
		} else {
			console.warn('Unsolicited OTA notification:', parsed);
		}
	}
}

export const session = new SessionStore();
