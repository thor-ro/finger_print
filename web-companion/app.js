const SDF_SERVICE_UUID = '7d5a0000-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_AUTH_UUID    = '7d5a0001-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_CONFIG_UUID  = '7d5a0002-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_ENROLL_UUID  = '7d5a0003-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_OTA_UUID     = '7d5a0004-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
// Read-only setup-state characteristic (readable on the encrypted-but-
// unauthenticated link, before login). Wire format: 1 byte -
//   0 setup not started | 1 Admin enrolled | 2 Nuki paired | 3 complete
const SDF_SETUP_STATE_UUID = '7d5a0005-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
// Device health report (read + notify). JSON; readable by any authenticated
// session at any permission level. A zero-length notification is a CHANGE
// MARKER: the report did not fit one notification at the negotiated MTU, so
// the client must read the full value (reads are not MTU-bounded).
const SDF_STATUS_UUID = '7d5a0006-5c2b-4f8a-9e3d-1a2b3c4d5e6f';

// Setup-state enumeration (mirrors sdf_services_setup_state_t)
const SETUP_NOT_STARTED = 0;
const SETUP_ADMIN_ENROLLED = 1;
const SETUP_REGISTERED = 2;
const SETUP_NUKI_PAIRED = 3;
const SETUP_COMPLETE = 4;

// Auth characteristic opcodes. LOGIN is a two-round-trip challenge-response
// (LOGIN_INIT then LOGIN_VERIFY) rather than a single message - see
// openspec/changes/ble-companion-login-challenge-response.
//
// The firmware enforces an exact length per command, and rejects any write
// over 65 bytes with an invalid-length error before dispatching on the
// opcode. Usernames are at most 31 bytes on the wire:
//
//   LOGOUT       [0x00]                                        exactly 1
//   REGISTER     [0x02][name_len][name][password_hash(32)]     2 + name_len + 32
//   LOGIN_INIT   [0x03][name_len][name]                        2 + name_len
//   LOGIN_VERIFY [0x04][response(32)]                          exactly 33
//
// This app has no LOGOUT send path today; if one is added it must write a
// single byte - a padded LOGOUT is rejected and does not log the connection
// out.
const SDF_AUTH_OPCODE_LOGOUT       = 0x00;
const SDF_AUTH_OPCODE_REGISTER     = 0x02;
const SDF_AUTH_OPCODE_LOGIN_INIT   = 0x03;
const SDF_AUTH_OPCODE_LOGIN_VERIFY = 0x04;

// Must match SDF_STORAGE_WEB_USER_SALT_LEN / SDF_SERVICES_WEB_AUTH_NONCE_LEN
// / SDF_SERVICES_WEB_AUTH_RESPONSE_LEN in the firmware. LOGIN_INIT's read
// response is [salt(16)][iteration_count(4, little-endian)][nonce(16)].
const SDF_WEB_AUTH_SALT_LEN = 16;
const SDF_WEB_AUTH_NONCE_LEN = 16;
const SDF_WEB_AUTH_RESPONSE_LEN = 32;
const SDF_WEB_AUTH_CHALLENGE_LEN = SDF_WEB_AUTH_SALT_LEN + 4 + SDF_WEB_AUTH_NONCE_LEN;

let bluetoothDevice;
let gattServer;
let sdfService;
let authChar;
let configChar;
let enrollChar;
let otaChar;
let setupStateChar;
let statusChar;

const connectBtn = document.getElementById('btn-connect');
const statusMsg = document.getElementById('connection-status');

connectBtn.addEventListener('click', async () => {
    try {
        statusMsg.textContent = 'Requesting Bluetooth Device...';
        bluetoothDevice = await navigator.bluetooth.requestDevice({
            filters: [{ services: [SDF_SERVICE_UUID] }],
            optionalServices: []
        });

        bluetoothDevice.addEventListener('gattserverdisconnected', onDisconnected);

        statusMsg.textContent = 'Connecting to GATT Server...';
        gattServer = await bluetoothDevice.gatt.connect();

        // Request MTU negotiation (512 bytes)
        try {
            const mtu = await gattServer.requestMTU(512);
            console.log(`MTU negotiated to ${mtu} bytes`);
        } catch (e) {
            console.warn('MTU negotiation not supported:', e);
        }

        statusMsg.textContent = 'Getting Service...';
        sdfService = await gattServer.getPrimaryService(SDF_SERVICE_UUID);

        statusMsg.textContent = 'Getting Characteristics...';
        authChar = await sdfService.getCharacteristic(SDF_AUTH_UUID);
        configChar = await sdfService.getCharacteristic(SDF_CONFIG_UUID);
        enrollChar = await sdfService.getCharacteristic(SDF_ENROLL_UUID);
        otaChar = await sdfService.getCharacteristic(SDF_OTA_UUID);
        setupStateChar = await sdfService.getCharacteristic(SDF_SETUP_STATE_UUID);
        statusChar = await sdfService.getCharacteristic(SDF_STATUS_UUID);

        statusMsg.textContent = 'Connected successfully!';

        authChar.addEventListener('characteristicvaluechanged', handleAuthNotification);
        await authChar.startNotifications();

        // Subscribe to config notifications
        configChar.addEventListener('characteristicvaluechanged', handleConfigNotification);
        await configChar.startNotifications();

        // Subscribe to enrollment notifications
        enrollChar.addEventListener('characteristicvaluechanged', handleEnrollNotification);
        await enrollChar.startNotifications();

        // Subscribe to OTA notifications
        otaChar.addEventListener('characteristicvaluechanged', handleOtaNotification);
        await otaChar.startNotifications();

        // Subscribe to device-health notifications. The device only pushes
        // to authenticated connections; subscribing here means the health
        // view starts updating the moment login succeeds.
        statusChar.addEventListener('characteristicvaluechanged', handleStatusNotification);
        await statusChar.startNotifications();

        /* Read setup state BEFORE login: the wizard is mandatory for an
         * unclaimed device, and resumes at the step the reported state
         * implies. */
        let setupState = SETUP_COMPLETE;
        try {
            const v = await setupStateChar.readValue();
            if (v.byteLength >= 1) {
                setupState = new Uint8Array(v.buffer, v.byteOffset, 1)[0];
            }
        } catch (e) {
            console.warn('Could not read setup state:', e);
        }

        if (setupState !== SETUP_COMPLETE) {
            enterWizard(setupState);
        } else {
            switchView('auth-view');
        }
    } catch (error) {
        console.error(error);
        statusMsg.textContent = `Error: ${error.message}`;
    }
});

function onDisconnected(event) {
    statusMsg.textContent = 'Device disconnected.';
    const wizardWasActive = document.getElementById('wizard-view').classList.contains('active');
    if (wizardWasActive && !setupCompleted) {
        /* The device disconnected without setup completing. If it stopped
         * advertising, the setup window elapsed: progress was erased and the
         * button must be pressed to re-arm before reconnecting. */
        wizardStatus.textContent =
            'The connection was lost and setup did not complete. If the device ' +
            'is no longer visible in the device picker, its setup window elapsed ' +
            'and all progress was discarded — press the button on the device to ' +
            'start setup again.';
    }
    switchView('connection-view');
    bluetoothDevice = null;
    gattServer = null;
    sdfService = null;
    authChar = null;
    configChar = null;
    enrollChar = null;
    otaChar = null;
    setupStateChar = null;
    document.getElementById('status-cards').style.display = 'none';
}

// --- View Management ---

function switchView(viewId) {
    document.querySelectorAll('.view').forEach(el => el.classList.remove('active'));
    document.getElementById(viewId).classList.add('active');
}

// --- First-Time Setup Wizard ---

const wizardStatus = document.getElementById('wizard-finish-status');
let setupCompleted = false;

function showWizardStep(stepElId) {
    document.querySelectorAll('#wizard-view .wizard-step').forEach(el => {
        el.style.display = 'none';
    });
    document.getElementById(stepElId).style.display = 'block';
}

// Resumes at the step the reported setup state implies, so a user who
// reconnects mid-setup does not repeat completed steps within one phase.
function enterWizard(setupState) {
    setupCompleted = false;
    switchView('wizard-view');
    if (setupState === SETUP_NOT_STARTED) {
        showWizardStep('wizard-step-enroll');
        document.getElementById('wizard-step-indicator').textContent =
            'Setup state: not started — begin with Admin enrolment.';
    } else if (setupState === SETUP_ADMIN_ENROLLED) {
        showWizardStep('wizard-step-register');
        document.getElementById('wizard-step-indicator').textContent =
            'Setup state: Admin enrolled — continue with account registration.';
    } else if (setupState === SETUP_REGISTERED) {
        showWizardStep('wizard-step-nuki');
        document.getElementById('wizard-step-indicator').textContent =
            'Setup state: account registered — continue with Nuki pairing.';
    } else if (setupState === SETUP_NUKI_PAIRED) {
        showWizardStep('wizard-step-finish');
        document.getElementById('wizard-step-indicator').textContent =
            'Setup state: Nuki paired — finish to claim the device.';
    } else {
        showWizardStep('wizard-step-enroll');
        document.getElementById('wizard-step-indicator').textContent = '';
    }
}

function wizardIsActive() {
    return document.getElementById('wizard-view').classList.contains('active');
}

// Step 1: Admin enrolment - the user-management enrolment verb on the
// Enrollment characteristic, user_id=1 (the first admin-permission user),
// permission=3. During first-time setup no admin exists yet to scan, so
// this request is admitted without an authorizing fingerprint (the device
// refuses it the moment any user is enrolled).
document.getElementById('btn-wizard-enroll-admin').addEventListener('click', async () => {
    const statusEl = document.getElementById('wizard-enroll-status');
    try {
        document.getElementById('btn-wizard-enroll-admin').disabled = true;
        statusEl.textContent = 'Starting Admin enrolment...';
        const result = await sendUmRequest({ verb: 'enroll', user_id: 1, permission: 3 });
        if (result === null) {
            statusEl.textContent =
                'No response received from the device - try again.';
            document.getElementById('btn-wizard-enroll-admin').disabled = false;
            return;
        }
        if (result.result === 'ok') {
            document.getElementById('wizard-enroll-progress').style.display = 'block';
            document.getElementById('wizard-enroll-step-text').textContent = 'Step 1 of 3';
            document.getElementById('wizard-enroll-progress-bar').style.width = '33%';
            document.getElementById('wizard-enroll-message').textContent =
                'Place the admin finger on the sensor for each of the 3 scans...';
            return; // progress notifications take it from here
        }
        statusEl.textContent = umResultMessage(result.result);
        document.getElementById('btn-wizard-enroll-admin').disabled = false;
    } catch (err) {
        console.error(err);
        statusEl.textContent = `Could not start enrolment: ${err.message}`;
        document.getElementById('btn-wizard-enroll-admin').disabled = false;
    }
});

// Step 3: initial Nuki pairing via the setup-phase-only Config action.
document.getElementById('btn-wizard-nuki-pair').addEventListener('click', async () => {
    const statusEl = document.getElementById('wizard-nuki-status');
    try {
        document.getElementById('btn-wizard-nuki-pair').disabled = true;
        statusEl.textContent = 'Pairing with the Nuki lock...';
        const resultPromise = waitForBleAdminActionResult('setup_nuki_pair', BLE_ADMIN_ACTION_RESPONSE_TIMEOUT_MS);
        const payload = new TextEncoder().encode(JSON.stringify({ action: 'setup_nuki_pair' }));
        await configChar.writeValue(payload);
        const paired = await resultPromise;
        if (paired === true) {
            statusEl.textContent = 'Nuki pairing succeeded.';
            showWizardStep('wizard-step-finish');
            document.getElementById('wizard-step-indicator').textContent =
                'Nuki paired - finish below.';
        } else {
            statusEl.textContent = 'Nuki pairing did not succeed. Make sure the lock is in pairing mode and try again.';
        }
    } catch (err) {
        console.error(err);
        statusEl.textContent = `Pairing request rejected: ${err.message}`;
    } finally {
        document.getElementById('btn-wizard-nuki-pair').disabled = false;
    }
});

// Step 4: explicit completion over the authenticated session.
document.getElementById('btn-wizard-finish').addEventListener('click', async () => {
    const statusEl = document.getElementById('wizard-finish-status');
    try {
        document.getElementById('btn-wizard-finish').disabled = true;
        statusEl.textContent = 'Finishing setup...';
        const resultPromise = waitForBleAdminActionResult('finish_setup', BLE_ADMIN_ACTION_RESPONSE_TIMEOUT_MS);
        const payload = new TextEncoder().encode(JSON.stringify({ action: 'finish_setup' }));
        await configChar.writeValue(payload);
        const result = await resultPromise;
        if (result === true) {
            setupCompleted = true;
            statusEl.textContent =
                'Setup complete! The device is now claimed and paired to this browser: it has ' +
                'switched to filtered advertising and will only accept reconnections from this companion.';
            document.getElementById('wizard-step-indicator').textContent = '';
        } else if (result === null || result === undefined) {
            statusEl.textContent = 'No response received - check the device.';
        } else {
            // Rejected: the device reports which step is still outstanding,
            // or 'internal_error' for a fault that is not a wizard step at
            // all (a failed NVS write, the connection dropping mid-request).
            // Sending the user back to redo a finished step in that case
            // would be misleading, so offer a retry on the spot instead.
            let step = null;
            if (lastConfigNotifyRaw && lastConfigNotifyRaw.step) {
                step = lastConfigNotifyRaw.step;
            }
            if (step === 'internal_error') {
                statusEl.textContent =
                    'The device could not finish setup because of an internal error. ' +
                    'No progress was lost - press Finish to try again.';
            } else {
                reportOutstandingStep(step);
                statusEl.textContent = `Setup cannot be finished yet: ${describeStep(step)} is still outstanding.`;
            }
        }
    } catch (err) {
        console.error(err);
        statusEl.textContent = `Finish request rejected: ${err.message}`;
    } finally {
        document.getElementById('btn-wizard-finish').disabled = false;
    }
});

function describeStep(step) {
    switch (step) {
        case 'admin_enrollment': return 'Admin fingerprint enrolment';
        case 'registration': return 'account registration';
        case 'nuki_pairing': return 'Nuki pairing';
        case 'internal_error': return 'An internal device error';
        default: return 'A setup step';
    }
}

function reportOutstandingStep(step) {
    if (step === 'registration') {
        showWizardStep('wizard-step-register');
    } else if (step === 'nuki_pairing') {
        showWizardStep('wizard-step-nuki');
    } else if (step === 'admin_enrollment' || step === null) {
        showWizardStep('wizard-step-enroll');
    }
}

// --- Auth (Login / Register) ---

const tabLogin = document.getElementById('tab-login');
const tabRegister = document.getElementById('tab-register');
const authSubmit = document.getElementById('btn-auth-submit');
const authStatus = document.getElementById('auth-status');
const registerNote = document.getElementById('register-note');
let isRegistering = false;

function setRegistering(registering) {
    isRegistering = registering;
    tabLogin.classList.toggle('active', !registering);
    tabRegister.classList.toggle('active', registering);
    authSubmit.textContent = registering ? 'Register' : 'Login';
    // Re-registration is the password-reset path: make the replace warning
    // visible whenever the register form is offered (companion-identity).
    registerNote.style.display = registering ? 'block' : 'none';
}

tabLogin.addEventListener('click', () => setRegistering(false));

tabRegister.addEventListener('click', () => setRegistering(true));

async function hashPassword(password) {
    const encoder = new TextEncoder();
    const data = encoder.encode(password);
    const hashBuffer = await crypto.subtle.digest('SHA-256', data);
    return new Uint8Array(hashBuffer);
}

// PBKDF2-HMAC-SHA256 credential stretching, run client-side at LOGIN so the
// device never has to spend the (expensive, tunable) stretching cost on its
// own CPU per login attempt - only once, server-side, at REGISTER. Mirrors
// sdf_services_web_auth_stretch_credential() in the firmware.
async function stretchPassword(password, salt, iterationCount) {
    const encoder = new TextEncoder();
    const passwordKey = await crypto.subtle.importKey(
        'raw', encoder.encode(password), { name: 'PBKDF2' }, false, ['deriveBits']
    );
    const bits = await crypto.subtle.deriveBits(
        { name: 'PBKDF2', salt, iterations: iterationCount, hash: 'SHA-256' },
        passwordKey,
        SDF_WEB_AUTH_RESPONSE_LEN * 8
    );
    return new Uint8Array(bits);
}

// HMAC-SHA256(stretched_credential, nonce) - the LOGIN_VERIFY response.
// Mirrors sdf_services_web_auth_verify_response()'s expected-response
// computation in the firmware.
async function computeLoginResponse(stretchedCredential, nonce) {
    const hmacKey = await crypto.subtle.importKey(
        'raw', stretchedCredential, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']
    );
    const signature = await crypto.subtle.sign('HMAC', hmacKey, nonce);
    return new Uint8Array(signature);
}

document.getElementById('auth-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;

    if (isRegistering) {
        await submitRegister(username, password);
    } else {
        await submitLogin(username, password);
    }
});

async function submitRegister(username, password) {
    // Re-registration replaces the confirming admin's existing credential
    // in place (companion-identity). Warn before submitting so this is a
    // deliberate choice, not an accidental overwrite.
    const confirmed = window.confirm(
        `Registering will bind the account to the admin whose fingerprint ` +
        `confirms it. If that admin already has a password, it will be ` +
        `replaced (this is the password-reset path). Continue?`
    );
    if (!confirmed) {
        authStatus.textContent = 'Registration cancelled - no existing password was changed.';
        return;
    }
    try {
        authStatus.textContent = 'Authenticating...';
        const encoder = new TextEncoder();
        const userBytes = encoder.encode(username);
        const hashBytes = await hashPassword(password);

        // Command format: [CMD, USER_LEN, USERNAME..., HASH...]. Unchanged -
        // the device salts and stretches server-side, once, only after an
        // admin fingerprint confirms the REGISTER. See
        // openspec/changes/ble-companion-login-challenge-response.
        const payload = new Uint8Array(2 + userBytes.length + hashBytes.length);
        payload[0] = SDF_AUTH_OPCODE_REGISTER;
        payload[1] = userBytes.length;
        payload.set(userBytes, 2);
        payload.set(hashBytes, 2 + userBytes.length);

        await authChar.writeValue(payload);
        authStatus.textContent = 'Please scan the Admin Finger on the device to confirm.';
    } catch (err) {
        authStatus.textContent = `Error: ${err.message}`;
    }
}

// Challenge-response LOGIN: write LOGIN_INIT, read back {salt,
// iteration_count, nonce}, stretch the password client-side and compute
// HMAC-SHA256(stretched, nonce), then write LOGIN_VERIFY. A rejected
// LOGIN_INIT means the device doesn't speak this protocol at all (out-of-date
// app or firmware) - surfaced distinctly from a rejected LOGIN_VERIFY, which
// means the credentials themselves were wrong. See design.md Risks.
async function submitLogin(username, password) {
    try {
        authStatus.textContent = 'Authenticating...';
        const encoder = new TextEncoder();
        const userBytes = encoder.encode(username);

        const initPayload = new Uint8Array(2 + userBytes.length);
        initPayload[0] = SDF_AUTH_OPCODE_LOGIN_INIT;
        initPayload[1] = userBytes.length;
        initPayload.set(userBytes, 2);

        try {
            await authChar.writeValue(initPayload);
        } catch (err) {
            authStatus.textContent = 'Login could not start - please make sure the companion app and device firmware are both up to date, then try again.';
            return;
        }

        const challengeView = await authChar.readValue();
        if (challengeView.byteLength !== SDF_WEB_AUTH_CHALLENGE_LEN) {
            authStatus.textContent = 'Login could not start - please make sure the companion app and device firmware are both up to date, then try again.';
            return;
        }
        const challengeBytes = new Uint8Array(challengeView.buffer, challengeView.byteOffset, challengeView.byteLength);
        const salt = challengeBytes.slice(0, SDF_WEB_AUTH_SALT_LEN);
        const iterationCount = challengeView.getUint32(SDF_WEB_AUTH_SALT_LEN, true);
        const nonce = challengeBytes.slice(SDF_WEB_AUTH_SALT_LEN + 4, SDF_WEB_AUTH_CHALLENGE_LEN);

        const stretched = await stretchPassword(password, salt, iterationCount);
        const response = await computeLoginResponse(stretched, nonce);

        const verifyPayload = new Uint8Array(1 + response.length);
        verifyPayload[0] = SDF_AUTH_OPCODE_LOGIN_VERIFY;
        verifyPayload.set(response, 1);

        try {
            await authChar.writeValue(verifyPayload);
        } catch (err) {
            // Wrong username/password: the device rejects the write itself
            // (BLE_ATT_ERR_INSUFFICIENT_AUTHEN), so there's no success
            // notification to wait for.
            authStatus.textContent = 'Incorrect username or password.';
            return;
        }

        // Success is confirmed asynchronously via the Auth characteristic
        // notification - see handleAuthNotification() below.
        authStatus.textContent = 'Waiting for device...';
    } catch (err) {
        authStatus.textContent = `Error: ${err.message}`;
    }
}

function handleAuthNotification(event) {
    const value = new Uint8Array(event.target.value.buffer);
    const status = value[0];

    if (status === 0x01) {
        // Wizard registration: a successful REGISTER advances the wizard
        // instead of opening the dashboard.
        if (wizardIsActive() && wizardRegisterPending) {
            wizardRegisterPending = false;
            document.getElementById('wizard-register-status').textContent =
                'Registration confirmed by the Admin fingerprint.';
            showWizardStep('wizard-step-nuki');
            document.getElementById('wizard-step-indicator').textContent =
                'Account registered - pair your Nuki lock.';
            return;
        }
        authStatus.textContent = 'Success!';
        setTimeout(() => {
            switchView('dashboard-view');
            document.getElementById('status-cards').style.display = 'flex';
            refreshDeviceHealth();
            resumeOtaTransferIfPending();
        }, 500);
    } else if (status === 0x02) {
        if (!wizardIsActive()) authStatus.textContent = 'Pending admin authorization on device...';
    } else {
        if (!wizardIsActive()) authStatus.textContent = 'Authentication failed or logged out.';
    }
}

// --- Dashboard ---

document.getElementById('btn-disconnect').addEventListener('click', () => {
    if (bluetoothDevice && bluetoothDevice.gatt.connected) {
        bluetoothDevice.gatt.disconnect();
    }
});

// Config read
document.getElementById('btn-read-config').addEventListener('click', async () => {
    try {
        const statusMsg = document.getElementById('ota-status');
        statusMsg.textContent = 'Reading config...';
        
        const value = await configChar.readValue();
        const decoder = new TextDecoder();
        const jsonStr = decoder.decode(value);
        const config = JSON.parse(jsonStr);
        
        displayConfig(config);
        statusMsg.textContent = 'Config read successfully';
    } catch (err) {
        console.error(err);
        document.getElementById('ota-status').textContent = `Error reading config: ${err.message}`;
    }
});

function displayConfig(config) {
    const display = document.getElementById('config-display');
    const applyBtn = document.getElementById('btn-apply-config');
    
    let html = '<table class="config-table">';
    for (const [key, value] of Object.entries(config)) {
        const isEditable = !['nuki_target_addr_type', 'nuki_target_addr'].includes(key);
        const inputType = typeof value === 'boolean' ? 'checkbox' : 'number';
        const checked = typeof value === 'boolean' && value ? 'checked' : '';
        
        if (typeof value === 'boolean') {
            html += `
                <tr>
                    <td><label>${escapeHtml(key)}</label></td>
                    <td><input type="checkbox" data-key="${escapeHtml(key)}" ${checked} ${!isEditable ? 'disabled' : ''}></td>
                </tr>
            `;
        } else {
            html += `
                <tr>
                    <td><label>${escapeHtml(key)}</label></td>
                    <td><input type="number" data-key="${escapeHtml(key)}" value="${escapeHtml(value)}" ${!isEditable ? 'disabled' : ''}></td>
                </tr>
            `;
        }
    }
    html += '</table>';
    
    display.innerHTML = html;
    display.style.display = 'block';
    applyBtn.style.display = 'block';
}

// Config write
document.getElementById('btn-apply-config').addEventListener('click', async () => {
    try {
        const display = document.getElementById('config-display');
        const inputs = display.querySelectorAll('input');
        const delta = {};
        
        inputs.forEach(input => {
            const key = input.dataset.key;
            if (input.type === 'checkbox') {
                delta[key] = input.checked;
            } else {
                delta[key] = parseInt(input.value, 10);
            }
        });
        
        const jsonStr = JSON.stringify(delta);
        const payload = new TextEncoder().encode(jsonStr);
        
        await configChar.writeValue(payload);
        document.getElementById('ota-status').textContent = 'Config applied successfully';
    } catch (err) {
        console.error(err);
        document.getElementById('ota-status').textContent = `Error applying config: ${err.message}`;
    }
});

function handleConfigNotification(event) {
    const value = new Uint8Array(event.target.value.buffer);
    const decoder = new TextDecoder();
    const jsonStr = decoder.decode(value);

    try {
        const config = JSON.parse(jsonStr);

        if (pendingBleAdminAction && config[pendingBleAdminAction.key] !== undefined) {
            lastConfigNotifyRaw = config;
            pendingBleAdminAction.resolve(config[pendingBleAdminAction.key] === true);
            return;
        }
    } catch (e) {
        console.warn('Config notification not valid JSON:', jsonStr);
    }
}

// Full JSON body of the most recent action-reply notify - lets callers read
// extra fields (e.g. finish_setup's "step") beyond the boolean value.
let lastConfigNotifyRaw = null;

// --- Device health view (companion-device-health) ---------------------------
//
// The dashboard shows only values the device actually reported. Every field
// of the health report is measured, unknown, or not applicable; unknown is
// displayed as unknown and not-applicable as N/A - never a number, never a
// carried-over earlier value. The configured default battery percentage is a
// setting, not a measurement, and is never shown as the battery level.

// Anything the device sends that a person chose - user names above all -
// is markup until proven otherwise. Escape before it reaches innerHTML.
function escapeHtml(value) {
    return String(value ?? '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

let latestHealthReport = null;

// A reading older than this is surfaced with its age so it cannot pass as
// current.
const HEALTH_STALE_AGE_MS = 60000;

function formatAge(ageMs) {
    if (ageMs < 1000) return `${ageMs} ms`;
    if (ageMs < 60000) return `${Math.round(ageMs / 1000)} s`;
    return `${Math.round(ageMs / 60000)} min`;
}

function healthValue(entry, formatMeasured) {
    if (!entry || entry.state === 'unknown') return 'Unknown';
    if (entry.state === 'not_applicable') return 'N/A';
    return formatMeasured(entry);
}

function withAgeSuffix(text, ageMs) {
    if (typeof ageMs !== 'number' || ageMs < HEALTH_STALE_AGE_MS) return text;
    return `${text} (reading ${formatAge(ageMs)} old)`;
}

function renderHealthReport(report) {
    latestHealthReport = report;

    // Status cards: lock state and battery come from the report now.
    const lockCard = document.getElementById('lock-state');
    const batteryCard = document.getElementById('battery-percent');

    if (report.lock && report.lock.state && report.lock.state !== 'unknown') {
        let lockText = report.lock.state.replace(/_/g, ' ');
        // An assumed state was derived from a command we sent, not from the
        // lock itself - show it as awaiting confirmation, never as equal to
        // a confirmed reading.
        if (report.lock.source === 'assumed') {
            lockText += ' (awaiting confirmation)';
        }
        lockCard.textContent = withAgeSuffix(lockText, report.lock.age_ms);
    } else {
        lockCard.textContent = 'Unknown';
    }

    if (report.battery && typeof report.battery.percent === 'number') {
        batteryCard.textContent = withAgeSuffix(`${report.battery.percent}%`,
                                                report.battery.age_ms);
    } else {
        batteryCard.textContent = 'Unknown';
    }

    const rows = [
        ['Lock state', lockCard.textContent],
        ['Battery', batteryCard.textContent],
        ['Alarms', report.alarms ? `mask ${report.alarms.mask}` : 'Unknown'],
        ['Fingerprint sensor', healthValue(
            report.fingerprint,
            e => e.ready ? 'Ready' : 'Not responding')],
        ['Nuki link', healthValue(
            report.nuki,
            e => `${e.paired ? 'paired' : 'not paired'}, ` +
                 `${e.connected ? 'connected' : 'disconnected'}`)],
        ['Zigbee network', healthValue(
            report.zigbee,
            e => e.joined ? 'joined' : 'not joined')],
        ['Firmware', report.firmware || 'Unknown'],
        ['OTA state', report.ota || 'Unknown'],
        ['Setup state', report.setup || 'Unknown'],
    ];

    const tbody = rows.map(([k, v]) =>
        `<tr><td><label>${escapeHtml(k)}</label></td><td>${escapeHtml(v)}</td></tr>`).join('');
    document.getElementById('health-table').innerHTML =
        `<table class="config-table">${tbody}</table>`;
}

document.getElementById('btn-refresh-health').addEventListener('click', refreshDeviceHealth);

async function refreshDeviceHealth() {
    try {
        const value = await statusChar.readValue();
        const report = JSON.parse(new TextDecoder().decode(value));
        renderHealthReport(report);
    } catch (err) {
        console.warn('Could not read device health:', err);
    }
}

// Notification from the Status characteristic: either a full report, or an
// empty change marker meaning "the report changed but did not fit one
// notification at this MTU" - resolve it by reading the full value.
async function handleStatusNotification(event) {
    if (event.target.value.byteLength === 0) {
        await refreshDeviceHealth();
        return;
    }
    try {
        const report = JSON.parse(new TextDecoder().decode(event.target.value));
        renderHealthReport(report);
    } catch (err) {
        console.warn('Status notification not valid JSON');
    }
}

// --- BLE-triggered admin actions (Nuki re-pair, Enroll-Admin, Zigbee Join) ---
// All three reuse the Config characteristic: a write of {"action":"<key>"}
// asks the device to enter the admin-fingerprint-gated flow for that action
// (see sdf_ble_companion_config_access() / ble-companion-service spec). The
// device only ever notifies Config with {"<key>": true|false} in response to
// its own matching request (see sdf_ble_companion_reply_admin_action) -
// handleConfigNotification() above resolves pendingBleAdminAction from that
// notify instead of treating it as a general config broadcast. Only one such
// request can be pending at a time (mirrors the device's own
// single-pending-admin-action invariant), so a single pending slot - rather
// than one per action - covers all three.
let pendingBleAdminAction = null; // { key, resolve } for the next {"<key>":...} notify

// The device's own pending-admin-action timeout is 10s
// (SDF_ADMIN_ACTION_TIMEOUT_MS); wait a little longer client-side so the
// device's own timeout-driven denial reply has time to arrive first.
const BLE_ADMIN_ACTION_RESPONSE_TIMEOUT_MS = 12000;

function waitForBleAdminActionResult(key, timeoutMs) {
    return new Promise((resolve) => {
        const timer = setTimeout(() => {
            pendingBleAdminAction = null;
            resolve(null); // no response in time - treated as ambiguous, not denied
        }, timeoutMs);

        pendingBleAdminAction = {
            key,
            resolve(authorized) {
                clearTimeout(timer);
                pendingBleAdminAction = null;
                resolve(authorized);
            }
        };
    });
}

// Wires up a "request <action>" button: writes {"action":key}, waits for the
// matching {key:true|false} reply (or the client-side timeout), and shows a
// three-way authorized / denied-or-timeout / no-response status message.
// Shared by all three BLE-triggered admin action triggers below rather than
// duplicated per action.
function wireBleAdminActionButton(button, statusEl, key, pendingMessage, authorizedMessage, rejectionHint) {
    button.addEventListener('click', async () => {
        try {
            button.disabled = true;
            statusEl.textContent = pendingMessage;

            const resultPromise = waitForBleAdminActionResult(key, BLE_ADMIN_ACTION_RESPONSE_TIMEOUT_MS);
            const payload = new TextEncoder().encode(JSON.stringify({ action: key }));
            await configChar.writeValue(payload);

            const authorized = await resultPromise;
            if (authorized === true) {
                statusEl.textContent = authorizedMessage;
            } else if (authorized === false) {
                statusEl.textContent = 'Request denied or timed out.';
            } else {
                statusEl.textContent = 'No response received - check the device.';
            }
        } catch (err) {
            console.error(err);
            pendingBleAdminAction = null;
            statusEl.textContent = `Request rejected: ${err.message} (${rejectionHint}).`;
        } finally {
            button.disabled = false;
        }
    });
}

wireBleAdminActionButton(
    document.getElementById('btn-nuki-repair'),
    document.getElementById('nuki-repair-status'),
    'nuki_repair',
    'Requesting Nuki re-pair... scan the Admin fingerprint on the device.',
    'Nuki pairing started on the device.',
    "setup may not be complete yet, or the connection isn't authenticated"
);

wireBleAdminActionButton(
    document.getElementById('btn-enroll-admin'),
    document.getElementById('enroll-admin-status'),
    'enroll_admin',
    'Requesting Enroll-Admin... scan the Admin fingerprint on the device.',
    'Admin enrollment started on the device - follow the fingerprint prompts.',
    "the connection isn't authenticated, or another admin action is already pending"
);

wireBleAdminActionButton(
    document.getElementById('btn-zb-join'),
    document.getElementById('zb-join-status'),
    'zb_join',
    'Requesting Zigbee Join window... scan the Admin fingerprint on the device.',
    'Zigbee join window opened on the device.',
    "the connection isn't authenticated, or another admin action is already pending"
);

// --- Companion user management (Enrollment characteristic) ---------------
// The Enrollment characteristic carries a request/reply protocol: every
// write carries a client-supplied request id ("req") and produces exactly
// one terminal reply carrying that id - even when refused before any work
// starts. Replies are correlated by request id (a Map of pending resolvers,
// not a single slot), so a reply arriving ten seconds after its request -
// e.g. after the admin walked over and scanned - is attributable.
//
// Wire shapes (see doc/sdf_sas.md):
//   request:   {"req":N,"verb":"list"|"enroll"|"delete"|"set_permission"|"rename",...}
//   reply:     {"req":N,"result":"<outcome>"}
//   list part: {"req":N,"verb":"list","part":i,"end":true|false,"users":[...]}
//   progress:  {"status":"success"|"failed","user_id":..,"step":..,"error_code":..}
//              plus "req":N when started by a user-management request.

let umNextRequestId = 1;
const umPendingReplies = new Map(); // req -> { resolve }

const UM_REPLY_TIMEOUT_MS = 15000; // device scan window is 10s

function sendUmRequest(request) {
    const req = umNextRequestId++;
    const payload = new TextEncoder().encode(JSON.stringify({ req, ...request }));
    const replyPromise = new Promise((resolve) => {
        const timer = setTimeout(() => {
            umPendingReplies.delete(req);
            resolve(null); // timed out client-side: ambiguous, not denied
        }, UM_REPLY_TIMEOUT_MS);
        umPendingReplies.set(req, {
            resolve(result) {
                clearTimeout(timer);
                umPendingReplies.delete(req);
                resolve(result);
            }
        });
    });
    return enrollChar.writeValue(payload).then(() => replyPromise);
}

// Renders each named refusal specifically rather than as a generic failure.
const UM_RESULT_MESSAGES = {
    ok: 'Completed.',
    not_found: 'No such enrolled user.',
    id_occupied: 'That user ID is already enrolled.',
    last_admin: 'Refused: this would leave the device without any admin.',
    name_taken: 'That name is already used by another user.',
    busy: 'Device busy with another action - try again shortly.',
    denied: 'Denied: the fingerprint scanned was not an admin finger.',
    timeout: 'Timed out: no admin fingerprint was scanned on the device.',
    invalid: 'The device rejected the request as malformed.',
};

function umResultMessage(result) {
    return UM_RESULT_MESSAGES[result] || `Request failed (${result}).`;
}

// The permission picker offers only Admin and Standard: level 2 stays a
// reserved placeholder (companion-identity).
function umPermissionName(p) {
    if (p === 3) return 'Admin';
    if (p === 1) return 'Standard';
    return `Level ${p}`;
}

let umUsers = []; // last received complete user list

function renderUmUsers() {
    const container = document.getElementById('um-users');
    if (!container) return;
    if (umUsers.length === 0) {
        container.innerHTML = '<p class="status-msg">No users enrolled.</p>';
        return;
    }
    let html = '<table><tr><th>ID</th><th>Name</th><th>Permission</th><th>Actions</th></tr>';
    for (const u of umUsers) {
        const id = Number(u.id);
        const perm = Number(u.perm);
        html += `<tr>
            <td>${id}</td>
            <td>${u.name ? escapeHtml(u.name) : '<em>—</em>'}</td>
            <td>${escapeHtml(umPermissionName(u.perm))}</td>
            <td>
                <button class="secondary-btn" data-um-action="rename" data-um-id="${id}">Rename</button>
                <button class="secondary-btn" data-um-action="permission" data-um-id="${id}" data-um-perm="${perm}">Permission</button>
                <button class="danger-btn" data-um-action="delete" data-um-id="${id}">Delete</button>
            </td>
        </tr>`;
    }
    html += '</table>';
    container.innerHTML = html;

    // Names are passed from the row's own record rather than baked into an
    // onclick attribute, so no name ever has to survive a trip through
    // HTML-attribute and JavaScript-string parsing.
    for (const btn of container.querySelectorAll('[data-um-action]')) {
        const id = Number(btn.dataset.umId);
        const user = umUsers.find(u => Number(u.id) === id);
        btn.addEventListener('click', () => {
            switch (btn.dataset.umAction) {
            case 'rename':
                umPromptRename(id, user && user.name ? user.name : '');
                break;
            case 'permission':
                umPromptPermission(id, Number(btn.dataset.umPerm));
                break;
            case 'delete':
                umDelete(id);
                break;
            }
        });
    }
}

function umStatus(message) {
    const el = document.getElementById('um-status');
    el.textContent = message;
}

document.getElementById('btn-um-refresh').addEventListener('click', async () => {
    try {
        umStatus('Requesting user list...');
        const result = await sendUmRequest({ verb: 'list' });
        // The terminal signal is the final list part's end marker; the
        // resolved value is null only on a client-side timeout.
        if (result === null) {
            umStatus('No response received - check the device.');
            return;
        }
        renderUmUsers();
        umStatus(`Listed ${umUsers.length} user(s).`);
    } catch (err) {
        console.error(err);
        umStatus(`Error: ${err.message}`);
    }
});

// An admin may demote or delete themselves; the firmware refuses only the
// LAST admin. Warn first because the acting session loses its own authority
// at its next restricted access - which looks like an abrupt logout.
function umWarnIfSelf(targetId, what) {
    const self = umUsers.find(u => u.id === targetId);
    const boundName = document.getElementById('username').value;
    if (self && self.name && self.name === boundName) {
        return window.confirm(
            `You are about to ${what} your OWN user (${boundName}). Your ` +
            `session will lose its authority immediately. Continue?`);
    }
    return true;
}

window.umDelete = async function (userId) {
    if (!umWarnIfSelf(userId, 'delete')) {
        umStatus('Cancelled.');
        return;
    }
    try {
        umStatus(`Requesting delete of user ${userId}... an Admin must scan ` +
                 'their fingerprint on the device to authorize it.');
        const result = await sendUmRequest({ verb: 'delete', user_id: userId });
        if (result === null) {
            umStatus('No response received - check the device.');
            return;
        }
        if (result.result === 'ok') {
            umStatus(`User ${userId} deleted.`);
        } else {
            umStatus(umResultMessage(result.result));
        }
        await refreshUmUsersSilently();
    } catch (err) {
        console.error(err);
        umStatus(`Error: ${err.message}`);
    }
};

async function refreshUmUsersSilently() {
    const result = await sendUmRequest({ verb: 'list' });
    if (result !== null) renderUmUsers();
}

window.umPromptRename = async function (userId, currentName) {
    const name = window.prompt('New name (must be unique - it is the login identifier):', currentName || '');
    if (!name) return;
    try {
        umStatus(`Requesting rename of user ${userId}... an Admin must scan ` +
                 'their fingerprint on the device to authorize it.');
        const result = await sendUmRequest({ verb: 'rename', user_id: userId, name });
        if (result === null) {
            umStatus('No response received - check the device.');
            return;
        }
        umStatus(result.result === 'ok'
            ? `User ${userId} renamed to "${name}".`
            : umResultMessage(result.result));
        await refreshUmUsersSilently();
    } catch (err) {
        console.error(err);
        umStatus(`Error: ${err.message}`);
    }
};

window.umPromptPermission = async function (userId, currentPerm) {
    const value = window.prompt(
        'New permission: 1 = Standard, 3 = Admin', String(currentPerm));
    const perm = parseInt(value, 10);
    if (perm !== 1 && perm !== 3) {
        umStatus('Only Standard (1) and Admin (3) can be assigned.');
        return;
    }
    if (!umWarnIfSelf(userId, 'demote')) {
        umStatus('Cancelled.');
        return;
    }
    try {
        umStatus(`Requesting permission change for user ${userId}... an Admin ` +
                 'must scan their fingerprint on the device to authorize it ' +
                 '(this can take up to ~15 seconds).');
        const result = await sendUmRequest({ verb: 'set_permission', user_id: userId, permission: perm });
        if (result === null) {
            umStatus('No response received - check the device.');
            return;
        }
        umStatus(result.result === 'ok'
            ? `Permission updated for user ${userId}.`
            : umResultMessage(result.result));
        await refreshUmUsersSilently();
    } catch (err) {
        console.error(err);
        umStatus(`Error: ${err.message}`);
    }
};

// Enrollment (dashboard panel). An enrolment now costs FOUR scans: one
// authorizing admin scan, then the three enrolment scans by the new user.
document.getElementById('btn-enroll').addEventListener('click', async () => {
    try {
        const userId = parseInt(document.getElementById('enroll-user-id').value, 10);
        const permission = parseInt(document.getElementById('enroll-permission').value, 10);

        if (userId < 1 || userId > 10) {
            document.getElementById('enroll-result').textContent = 'User ID must be 1-10';
            document.getElementById('enroll-result').style.display = 'block';
            return;
        }
        if (permission !== 1 && permission !== 3) {
            document.getElementById('enroll-result').textContent =
                'Only Standard (1) and Admin (3) can be assigned.';
            document.getElementById('enroll-result').style.display = 'block';
            return;
        }

        document.getElementById('enroll-progress').style.display = 'block';
        document.getElementById('enroll-result').style.display = 'none';
        document.getElementById('enroll-message').textContent =
            'Waiting for the authorizing Admin scan on the device...';

        const result = await sendUmRequest({ verb: 'enroll', user_id: userId, permission });
        if (result === null) {
            document.getElementById('enroll-progress').style.display = 'none';
            document.getElementById('enroll-result').textContent =
                'No response received - check the device.';
            document.getElementById('enroll-result').style.display = 'block';
            return;
        }
        if (result.result === 'ok') {
            // Enrolment started: progress notifications (carrying this
            // request's id) take it from here.
            document.getElementById('enroll-message').textContent =
                'Authorized - follow the prompts: the new user scans three times.';
        } else {
            document.getElementById('enroll-progress').style.display = 'none';
            document.getElementById('enroll-result').textContent =
                umResultMessage(result.result);
            document.getElementById('enroll-result').style.color = '#ef4444';
            document.getElementById('enroll-result').style.display = 'block';
        }
    } catch (err) {
        console.error(err);
        document.getElementById('enroll-result').textContent = `Error: ${err.message}`;
        document.getElementById('enroll-result').style.display = 'block';
    }
});

// Set while the wizard's registration form is awaiting its admin-scan
// confirmation, so the success auth-notification advances the wizard.
let wizardRegisterPending = false;

document.getElementById('wizard-register-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const statusEl = document.getElementById('wizard-register-status');
    // Same ownership + replace warning as the dashboard register form: the
    // account will belong to whichever admin's finger confirms it, and a
    // confirming admin who already holds an account has it replaced.
    if (!window.confirm(
            `The account will belong to the admin who confirms it with a ` +
            `fingerprint scan. If that admin already holds a password, it ` +
            `will be replaced. Continue?`)) {
        statusEl.textContent = 'Registration cancelled - no existing password was changed.';
        return;
    }
    try {
        const username = document.getElementById('wizard-username').value;
        const password = document.getElementById('wizard-password').value;

        statusEl.textContent = 'Submitting registration...';
        const encoder = new TextEncoder();
        const userBytes = encoder.encode(username);
        const hashBytes = await hashPassword(password);

        const payload = new Uint8Array(2 + userBytes.length + hashBytes.length);
        payload[0] = SDF_AUTH_OPCODE_REGISTER;
        payload[1] = userBytes.length;
        payload.set(userBytes, 2);
        payload.set(hashBytes, 2 + userBytes.length);

        wizardRegisterPending = true;
        await authChar.writeValue(payload);
        statusEl.textContent = 'Please scan the Admin Finger on the device to confirm.';
    } catch (err) {
        wizardRegisterPending = false;
        statusEl.textContent = `Error: ${err.message}`;
    }
});

function handleEnrollNotification(event) {
    const value = new Uint8Array(event.target.value.buffer);
    const decoder = new TextDecoder();
    const jsonStr = decoder.decode(value);

    let data;
    try {
        data = JSON.parse(jsonStr);
    } catch (e) {
        console.warn('Enroll notification not valid JSON:', jsonStr);
        return;
    }

    // Terminal replies and list parts are correlated by request id.
    if (data.users !== undefined) {
        handleUmListPart(data);
        return;
    }
    if (data.result !== undefined) {
        handleUmReply(data);
        return;
    }

    // Wizard step 1 uses the same enrollment notifications as the dashboard
    // panel - route them to the wizard UI while the wizard is active.
    if (wizardIsActive()) {
        handleWizardEnrollNotification(data);
        return;
    }

    const progressDiv = document.getElementById('enroll-progress');
    const resultDiv = document.getElementById('enroll-result');

    if (data.status === 'success') {
        progressDiv.style.display = 'none';
        resultDiv.textContent = `Enrollment successful! User ID: ${data.user_id}`;
        resultDiv.style.color = '#22c55e';
        resultDiv.style.display = 'block';
    } else if (data.status === 'failed') {
        progressDiv.style.display = 'none';
        resultDiv.textContent = `Enrollment failed at step ${data.step}: error ${data.error_code}`;
        resultDiv.style.color = '#ef4444';
        resultDiv.style.display = 'block';
    } else if (data.step !== undefined) {
        const step = data.step;
        const maxSteps = 3;
        document.getElementById('enroll-step-text').textContent = `Step ${step} of ${maxSteps}`;
        document.getElementById('enroll-progress-bar').style.width = `${(step / maxSteps) * 100}%`;
        document.getElementById('enroll-message').textContent =
            `Place finger for step ${step}...`;
    }
}

// --- User-management reply/list-part routing ------------------------------

let umListParts = []; // accumulated users of an in-flight list reply

function handleUmReply(data) {
    const pending = umPendingReplies.get(data.req);
    if (pending) {
        pending.resolve(data);
    } else {
        console.warn('UM reply for unknown request id:', data.req);
    }
}

function handleUmListPart(data) {
    if (!Array.isArray(data.users)) return;
    umListParts.push(...data.users);
    if (data.end === true) {
        umUsers = umListParts;
        umListParts = [];
        renderUmUsers();
        // The final part IS the list verb's terminal reply.
        const pending = umPendingReplies.get(data.req);
        if (pending) pending.resolve({ req: data.req, result: 'ok' });
    }
}

// Wizard-scoped mirror of the dashboard enrollment notification handling.
// `data` is already parsed by handleEnrollNotification.
function handleWizardEnrollNotification(data) {
    const statusEl = document.getElementById('wizard-enroll-status');
    const progressDiv = document.getElementById('wizard-enroll-progress');
    const stepText = document.getElementById('wizard-enroll-step-text');
    const progressBar = document.getElementById('wizard-enroll-progress-bar');
    const messageDiv = document.getElementById('wizard-enroll-message');

    if (data.status === 'success') {
        progressDiv.style.display = 'none';
        document.getElementById('btn-wizard-enroll-admin').disabled = false;
        statusEl.textContent = 'Admin enrolment successful.';
        showWizardStep('wizard-step-register');
        document.getElementById('wizard-step-indicator').textContent =
            'Admin enrolled - register your account below.';
    } else if (data.status === 'failed') {
        progressDiv.style.display = 'none';
        document.getElementById('btn-wizard-enroll-admin').disabled = false;
        statusEl.textContent =
            `Enrolment failed at step ${data.step} (error ${data.error_code}) - try again.`;
    } else if (data.step !== undefined) {
        const maxSteps = 3;
        stepText.textContent = `Step ${data.step} of ${maxSteps}`;
        progressBar.style.width = `${(data.step / maxSteps) * 100}%`;
        messageDiv.textContent = `Place finger for scan ${data.step} of ${maxSteps}...`;
    }
}

// OTA (BLE chunked firmware transfer: BEGIN 0x01 / CHUNK 0x02 / END 0x03)
const otaStatus = document.getElementById('ota-status');
const otaProgress = document.getElementById('ota-progress');
const otaFileInput = document.getElementById('firmware-file');

const SDF_OTA_OPCODE_BEGIN = 0x01;
const SDF_OTA_OPCODE_CHUNK = 0x02;
const SDF_OTA_OPCODE_END = 0x03;

// Web Bluetooth does not expose the connection's negotiated ATT MTU, so the
// app uses a conservative fixed chunk payload size safely below the
// smallest commonly-negotiated MTU (~185 bytes), halving it on an
// over-MTU write rejection down to OTA_MIN_CHUNK_SIZE.
const OTA_INITIAL_CHUNK_SIZE = 180;
const OTA_MIN_CHUNK_SIZE = 20;

// How long to wait for a `ready` / `chunk_ack` notification after a write.
const OTA_RESPONSE_TIMEOUT_MS = 10000;
// Grace period after END during which a `failed` notify or a disconnect
// (the expected successful-commit path) is awaited before the outcome is
// declared ambiguous.
const OTA_COMPLETION_GRACE_MS = 5000;

let otaChunkSize = OTA_INITIAL_CHUNK_SIZE;
let otaPendingNotification = null; // { resolve, reject } for the next OTA notify
let otaResumeState = null; // { file, imageSize } while a transfer is open device-side

function waitForOtaNotification(timeoutMs) {
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
            otaPendingNotification = null;
            reject(new Error('Timed out waiting for device response.'));
        }, timeoutMs);

        otaPendingNotification = {
            resolve(data) {
                clearTimeout(timer);
                otaPendingNotification = null;
                resolve(data);
            },
            reject(err) {
                clearTimeout(timer);
                otaPendingNotification = null;
                reject(err);
            }
        };
    });
}

function handleOtaNotification(event) {
    const value = new Uint8Array(event.target.value.buffer);
    const decoder = new TextDecoder();
    const jsonStr = decoder.decode(value);

    let data;
    try {
        data = JSON.parse(jsonStr);
    } catch (e) {
        console.warn('OTA notification not valid JSON:', jsonStr);
        return;
    }

    if (otaPendingNotification) {
        otaPendingNotification.resolve(data);
    } else {
        console.warn('Unsolicited OTA notification:', data);
    }
}

// Writes an OTA opcode and resolves with the next OTA notification. Guards
// against the write itself being rejected at the GATT layer -- malformed,
// oversized, or out-of-session writes never produce a notify at all (per
// sdf_ble_companion_ota.c), so a rejected writeValue() must reject the
// pending notification wait rather than leave it hanging.
async function writeOtaOpcodeAndAwaitNotification(payload, timeoutMs) {
    const notificationPromise = waitForOtaNotification(timeoutMs);
    try {
        await otaChar.writeValue(payload);
    } catch (err) {
        if (otaPendingNotification) {
            otaPendingNotification.reject(err);
        }
        throw err;
    }
    return notificationPromise;
}

function isBluetoothConnected() {
    return !!(bluetoothDevice && bluetoothDevice.gatt && bluetoothDevice.gatt.connected && otaChar);
}

async function beginOtaTransfer(imageSize) {
    const payload = new Uint8Array(5);
    payload[0] = SDF_OTA_OPCODE_BEGIN;
    new DataView(payload.buffer).setUint32(1, imageSize, true); // little-endian

    const data = await writeOtaOpcodeAndAwaitNotification(payload, OTA_RESPONSE_TIMEOUT_MS);
    if (data.status !== 'ready' || typeof data.offset !== 'number') {
        throw new Error(`Unexpected response to OTA begin: ${JSON.stringify(data)}`);
    }
    return data.offset;
}

async function sendOtaChunks(file, imageSize, startOffset) {
    let offset = startOffset;

    while (offset < imageSize) {
        const end = Math.min(offset + otaChunkSize, imageSize);
        const chunkBytes = new Uint8Array(await file.slice(offset, end).arrayBuffer());
        const payload = new Uint8Array(1 + chunkBytes.length);
        payload[0] = SDF_OTA_OPCODE_CHUNK;
        payload.set(chunkBytes, 1);

        let ack;
        try {
            ack = await writeOtaOpcodeAndAwaitNotification(payload, OTA_RESPONSE_TIMEOUT_MS);
        } catch (err) {
            if (isBluetoothConnected() && otaChunkSize > OTA_MIN_CHUNK_SIZE) {
                // Rejected as over-MTU: halve the chunk size and retry from
                // the same (last confirmed) offset -- do not advance offset.
                otaChunkSize = Math.max(OTA_MIN_CHUNK_SIZE, Math.floor(otaChunkSize / 2));
                console.warn(`OTA chunk write rejected; retrying with a smaller chunk size (${otaChunkSize} bytes).`, err);
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
        const percent = Math.round((offset / imageSize) * 100);
        otaProgress.value = percent;
        otaStatus.textContent = `Uploading firmware... ${percent}%`;
    }
}

// After END, races a `failed` notify against a disconnect within a bounded
// grace window. The device's successful-commit path reboots immediately
// after committing (sdf_ota_verify_and_commit() never returns), so it never
// gets to send a `success` notify -- a clean disconnect with no `failed`
// notify in the window IS the expected success signal, not an error. See
// sdf_ble_companion_ota.c's sdf_ble_ota_handle_end.
function endOtaTransferAndAwaitOutcome() {
    const deviceRef = bluetoothDevice;
    const payload = new Uint8Array([SDF_OTA_OPCODE_END]);

    otaStatus.textContent = 'Verifying and installing — the device will restart...';

    return new Promise((resolve, reject) => {
        let settled = false;
        let graceTimer = null;

        function onOtaDisconnect() {
            const connStatus = document.getElementById('connection-status');
            if (connStatus) {
                connStatus.textContent = 'Device disconnected after OTA end-transfer — this is expected on success. Reconnect to confirm the new firmware version.';
            }
            finish(resolve, { outcome: 'presumed-success' });
        }

        function finish(fn, arg) {
            if (settled) {
                return;
            }
            settled = true;
            clearTimeout(graceTimer);
            if (deviceRef) {
                deviceRef.removeEventListener('gattserverdisconnected', onOtaDisconnect);
            }
            otaPendingNotification = null;
            fn(arg);
        }

        otaPendingNotification = {
            resolve(data) {
                if (data.status === 'failed') {
                    finish(resolve, { outcome: 'failed', error: data.error });
                } else {
                    // Unreachable in practice -- the device reboots before a
                    // `success` notify can be sent -- but honor it if seen.
                    finish(resolve, { outcome: 'success' });
                }
            },
            reject() {
                /* no-op: the writeValue().catch() below handles write errors */
            }
        };

        if (deviceRef) {
            deviceRef.addEventListener('gattserverdisconnected', onOtaDisconnect);
        }

        graceTimer = setTimeout(() => {
            finish(resolve, { outcome: 'ambiguous' });
        }, OTA_COMPLETION_GRACE_MS);

        otaChar.writeValue(payload).catch((err) => finish(reject, err));
    });
}

async function performOtaTransfer(file, imageSize) {
    otaResumeState = { file, imageSize };
    otaChunkSize = OTA_INITIAL_CHUNK_SIZE;

    try {
        otaStatus.textContent = 'Starting OTA transfer...';
        otaProgress.style.display = 'block';
        otaProgress.value = 0;

        const startOffset = await beginOtaTransfer(imageSize);
        await sendOtaChunks(file, imageSize, startOffset);
        const result = await endOtaTransferAndAwaitOutcome();
        otaResumeState = null;

        if (result.outcome === 'failed') {
            otaStatus.textContent = `OTA failed: ${result.error || 'unknown error'}`;
            otaProgress.style.display = 'none';
        } else if (result.outcome === 'ambiguous') {
            otaStatus.textContent = 'OTA outcome unknown — no confirmation received before the timeout. Check the device.';
        } else {
            // 'success' or 'presumed-success'
            otaProgress.value = 100;
            otaStatus.textContent = 'OTA transfer complete. Reconnect once the device restarts to confirm the new firmware version.';
        }
    } catch (err) {
        if (isBluetoothConnected()) {
            // A real, non-disconnect failure: no point auto-resuming.
            otaResumeState = null;
            otaStatus.textContent = `Error: ${err.message}`;
            otaProgress.style.display = 'none';
        } else {
            // Connection lost mid-transfer: keep resume state so the
            // transfer continues once the user reconnects and re-authenticates.
            otaStatus.textContent = 'Connection lost during firmware transfer. Reconnect and log in again to resume.';
        }
    }
}

async function resumeOtaTransferIfPending() {
    if (!otaResumeState) {
        return;
    }
    const { file, imageSize } = otaResumeState;
    otaStatus.textContent = 'Resuming firmware transfer...';
    await performOtaTransfer(file, imageSize);
}

document.getElementById('ota-form').addEventListener('submit', async (e) => {
    e.preventDefault();

    if (otaResumeState) {
        otaStatus.textContent = 'A firmware transfer is already in progress.';
        return;
    }

    const file = otaFileInput.files[0];
    if (!file) {
        otaStatus.textContent = 'Select a firmware (.bin) file first.';
        return;
    }

    // State the device's REPORTED battery level alongside the warning. When
    // no measurement is available, say so - never imply the level was
    // checked, and never substitute the configured default.
    let batteryLine;
    if (latestHealthReport && latestHealthReport.battery &&
        typeof latestHealthReport.battery.percent === 'number') {
        batteryLine = `The device reports its battery at ` +
            `${latestHealthReport.battery.percent}%.`;
    } else {
        batteryLine = 'The device\'s battery level is currently unknown.';
    }

    const confirmed = window.confirm(
        `${batteryLine} Ensure your battery is above 20%. OTA transfer over ` +
        'Bluetooth draws significant power and firmware is large — keep the ' +
        'app open and the device nearby until it completes.\n\n' +
        'Start the firmware update now?'
    );
    if (!confirmed) {
        return;
    }

    await performOtaTransfer(file, file.size);
});