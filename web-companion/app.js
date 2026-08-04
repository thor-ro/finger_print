const SDF_SERVICE_UUID = '7d5a0000-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_AUTH_UUID    = '7d5a0001-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_CONFIG_UUID  = '7d5a0002-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_ENROLL_UUID  = '7d5a0003-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_OTA_UUID     = '7d5a0004-5c2b-4f8a-9e3d-1a2b3c4d5e6f';

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

document.getElementById('auth-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;
    
    try {
        authStatus.textContent = 'Authenticating...';
        const encoder = new TextEncoder();
        const userBytes = encoder.encode(username);
        const hashBytes = await hashPassword(password);
        
        // Command format: [CMD, USER_LEN, USERNAME..., HASH...]
        // CMD: 1 = Login, 2 = Register
        const cmd = isRegistering ? 0x02 : 0x01;
        const payload = new Uint8Array(2 + userBytes.length + hashBytes.length);
        payload[0] = cmd;
        payload[1] = userBytes.length;
        payload.set(userBytes, 2);
        payload.set(hashBytes, 2 + userBytes.length);
        
        await authChar.writeValue(payload);
        authStatus.textContent = isRegistering ? 'Please scan the Admin Finger on the device to confirm.' : 'Waiting for device...';
    } catch (err) {
        authStatus.textContent = `Error: ${err.message}`;
    }
});

function handleAuthNotification(event) {
    const value = new Uint8Array(event.target.value.buffer);
    const status = value[0];
    
    if (status === 0x01) {
        authStatus.textContent = 'Success!';
        setTimeout(() => {
            switchView('dashboard-view');
            document.getElementById('status-cards').style.display = 'flex';
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

// OTA
const otaStatus = document.getElementById('ota-status');

document.getElementById('ota-form').addEventListener('submit', async (e) => {
    e.preventDefault();
    
    const ssid = document.getElementById('wifi-ssid').value;
    const password = document.getElementById('wifi-password').value;
    const firmwareUrl = document.getElementById('firmware-url').value;
    
    if (!firmwareUrl.startsWith('https://')) {
        otaStatus.textContent = 'Error: Firmware URL must use HTTPS.';
        return;
    }
    
    try {
        otaStatus.textContent = 'Requesting OTA...';
        document.getElementById('ota-progress').style.display = 'block';
        document.getElementById('ota-progress').value = 0;
        
        const payloadObj = { ssid, password, firmwareUrl };
        const jsonStr = JSON.stringify(payloadObj);
        const payload = new TextEncoder().encode(jsonStr);
        
        await otaChar.writeValue(payload);
        
        otaStatus.textContent = 'OTA triggered successfully. Waiting for progress...';
    } catch (err) {
        otaStatus.textContent = `Error: ${err.message}`;
        document.getElementById('ota-progress').style.display = 'none';
    }
});

function handleOtaNotification(event) {
    const value = new Uint8Array(event.target.value.buffer);
    const decoder = new TextDecoder();
    const jsonStr = decoder.decode(value);
    
    try {
        const data = JSON.parse(jsonStr);
        const progressEl = document.getElementById('ota-progress');
        
        if (data.status === 'wifi_connecting') {
            otaStatus.textContent = 'Connecting to Wi-Fi...';
            progressEl.value = 0;
        } else if (data.status === 'wifi_connected') {
            otaStatus.textContent = 'Wi-Fi connected. Starting download...';
            progressEl.value = 5;
        } else if (data.status === 'downloading') {
            const progress = data.progress || 0;
            otaStatus.textContent = `Downloading... ${progress}%`;
            progressEl.value = 5 + Math.round(progress * 0.8);
        } else if (data.status === 'verifying') {
            otaStatus.textContent = 'Verifying firmware...';
            progressEl.value = 90;
        } else if (data.status === 'success') {
            otaStatus.textContent = 'OTA update successful! Device will reboot.';
            progressEl.value = 100;
        } else if (data.status === 'failed') {
            otaStatus.textContent = `OTA failed: ${data.error || 'unknown error'}`;
            progressEl.style.display = 'none';
        }
    } catch (e) {
        console.warn('OTA notification not valid JSON:', jsonStr);
    }
}