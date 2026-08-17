const SDF_SERVICE_UUID = '7d5a0000-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_AUTH_UUID    = '7d5a0001-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_CONFIG_UUID  = '7d5a0002-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_ENROLL_UUID  = '7d5a0003-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_OTA_UUID     = '7d5a0004-5c2b-4f8a-9e3d-1a2b3c4d5e6f';

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
        
        switchView('auth-view');
    } catch (error) {
        console.error(error);
        statusMsg.textContent = `Error: ${error.message}`;
    }
});

function onDisconnected(event) {
    statusMsg.textContent = 'Device disconnected.';
    switchView('connection-view');
    bluetoothDevice = null;
    gattServer = null;
    sdfService = null;
    authChar = null;
    configChar = null;
    enrollChar = null;
    otaChar = null;
    document.getElementById('status-cards').style.display = 'none';
}

// --- View Management ---

function switchView(viewId) {
    document.querySelectorAll('.view').forEach(el => el.classList.remove('active'));
    document.getElementById(viewId).classList.add('active');
}

// --- Auth (Login / Register) ---

const tabLogin = document.getElementById('tab-login');
const tabRegister = document.getElementById('tab-register');
const authSubmit = document.getElementById('btn-auth-submit');
const authStatus = document.getElementById('auth-status');
let isRegistering = false;

tabLogin.addEventListener('click', () => {
    isRegistering = false;
    tabLogin.classList.add('active');
    tabRegister.classList.remove('active');
    authSubmit.textContent = 'Login';
});

tabRegister.addEventListener('click', () => {
    isRegistering = true;
    tabRegister.classList.add('active');
    tabLogin.classList.remove('active');
    authSubmit.textContent = 'Register';
});

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
        authStatus.textContent = 'Success!';
        setTimeout(() => {
            switchView('dashboard-view');
            document.getElementById('status-cards').style.display = 'flex';
            resumeOtaTransferIfPending();
        }, 500);
    } else if (status === 0x02) {
        authStatus.textContent = 'Pending admin authorization on device...';
    } else {
        authStatus.textContent = 'Authentication failed or logged out.';
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
        
        // Update status cards
        updateStatusCards(config);
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
                    <td><label>${key}</label></td>
                    <td><input type="checkbox" data-key="${key}" ${checked} ${!isEditable ? 'disabled' : ''}></td>
                </tr>
            `;
        } else {
            html += `
                <tr>
                    <td><label>${key}</label></td>
                    <td><input type="number" data-key="${key}" value="${value}" ${!isEditable ? 'disabled' : ''}></td>
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
            pendingBleAdminAction.resolve(config[pendingBleAdminAction.key] === true);
            return;
        }

        updateStatusCards(config);
    } catch (e) {
        console.warn('Config notification not valid JSON:', jsonStr);
    }
}

function updateStatusCards(config) {
    if (config.battery_default_percent !== undefined) {
        document.getElementById('battery-percent').textContent = `${config.battery_default_percent}%`;
    }
    // Lock state would come from other notifications
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

// Enrollment
document.getElementById('btn-enroll').addEventListener('click', async () => {
    try {
        const userId = parseInt(document.getElementById('enroll-user-id').value, 10);
        const permission = parseInt(document.getElementById('enroll-permission').value, 10);
        
        if (userId < 1 || userId > 10) {
            document.getElementById('enroll-result').textContent = 'User ID must be 1-10';
            document.getElementById('enroll-result').style.display = 'block';
            return;
        }
        
        const payloadObj = { user_id: userId, permission };
        const jsonStr = JSON.stringify(payloadObj);
        const payload = new TextEncoder().encode(jsonStr);
        
        await enrollChar.writeValue(payload);
        
        document.getElementById('enroll-progress').style.display = 'block';
        document.getElementById('enroll-result').style.display = 'none';
        document.getElementById('enroll-message').textContent = 'Enrollment started...';
        document.getElementById('enroll-step-text').textContent = 'Step 1 of 3';
        document.getElementById('enroll-progress-bar').style.width = '33%';
    } catch (err) {
        console.error(err);
        document.getElementById('enroll-result').textContent = `Error: ${err.message}`;
        document.getElementById('enroll-result').style.display = 'block';
    }
});

function handleEnrollNotification(event) {
    const value = new Uint8Array(event.target.value.buffer);
    const decoder = new TextDecoder();
    const jsonStr = decoder.decode(value);
    
    try {
        const data = JSON.parse(jsonStr);
        const progressDiv = document.getElementById('enroll-progress');
        const resultDiv = document.getElementById('enroll-result');
        const stepText = document.getElementById('enroll-step-text');
        const progressBar = document.getElementById('enroll-progress-bar');
        const messageDiv = document.getElementById('enroll-message');
        
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
            stepText.textContent = `Step ${step} of ${maxSteps}`;
            progressBar.style.width = `${(step / maxSteps) * 100}%`;
            messageDiv.textContent = `Place finger for step ${step}...`;
        }
    } catch (e) {
        console.warn('Enroll notification not valid JSON:', jsonStr);
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

    const confirmed = window.confirm(
        'Ensure your battery is above 20%. OTA transfer over Bluetooth draws significant power and firmware is large — keep the app open and the device nearby until it completes.\n\nStart the firmware update now?'
    );
    if (!confirmed) {
        return;
    }

    await performOtaTransfer(file, file.size);
});