const SDF_SERVICE_UUID = '7d5a0000-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_AUTH_UUID    = '7d5a0001-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_CONFIG_UUID  = '7d5a0002-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_ENROLL_UUID  = '7d5a0003-5c2b-4f8a-9e3d-1a2b3c4d5e6f';
const SDF_OTA_UUID     = '7d5a0004-5c2b-4f8a-9e3d-1a2b3c4d5e6f';

let bluetoothDevice;
let gattServer;
let sdfService;
let authChar;

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

        statusMsg.textContent = 'Getting Service...';
        sdfService = await gattServer.getPrimaryService(SDF_SERVICE_UUID);

        statusMsg.textContent = 'Getting Characteristics...';
        authChar = await sdfService.getCharacteristic(SDF_AUTH_UUID);

        statusMsg.textContent = 'Connected successfully!';
        
        authChar.addEventListener('characteristicvaluechanged', handleAuthNotification);
        await authChar.startNotifications();
        
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
        setTimeout(() => switchView('dashboard-view'), 500);
    } else if (status === 0x02) {
        authStatus.textContent = 'Pending admin authorization on device...';
    } else {
        authStatus.textContent = 'Authentication failed or logged out.';
    }
}

// --- Dashboard (Config & OTA) ---

document.getElementById('btn-disconnect').addEventListener('click', () => {
    if (bluetoothDevice && bluetoothDevice.gatt.connected) {
        bluetoothDevice.gatt.disconnect();
    }
});

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
        
        const payloadObj = { ssid, password, firmwareUrl };
        const jsonStr = JSON.stringify(payloadObj);
        const payload = new TextEncoder().encode(jsonStr);
        
        const otaChar = await sdfService.getCharacteristic(SDF_OTA_UUID);
        await otaChar.writeValue(payload);
        
        otaStatus.textContent = 'OTA triggered successfully. Check device status.';
    } catch (err) {
        otaStatus.textContent = `Error: ${err.message}`;
    }
});
