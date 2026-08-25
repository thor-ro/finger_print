/**
 * Login / registration protocol codec and credential derivation.
 *
 * PURE module: no DOM, no navigator, no SvelteKit imports. Takes bytes in,
 * returns bytes out. `crypto.subtle` is used through `globalThis.crypto`,
 * which exists in browsers and in Node's test environment alike.
 *
 * Auth characteristic opcodes. LOGIN is a two-round-trip
 * challenge-response (LOGIN_INIT then LOGIN_VERIFY) rather than a single
 * message.
 *
 * The firmware enforces an exact length per command, and rejects any write
 * over 65 bytes with an invalid-length error before dispatching on the
 * opcode. Usernames are at most 31 bytes on the wire:
 *
 *   LOGOUT       [0x00]                                        exactly 1
 *   REGISTER     [0x02][name_len][name][password_hash(32)]     2 + name_len + 32
 *   LOGIN_INIT   [0x03][name_len][name]                        2 + name_len
 *   LOGIN_VERIFY [0x04][response(32)]                          exactly 33
 */

export const AUTH_OPCODE_LOGOUT = 0x00;
export const AUTH_OPCODE_REGISTER = 0x02;
export const AUTH_OPCODE_LOGIN_INIT = 0x03;
export const AUTH_OPCODE_LOGIN_VERIFY = 0x04;

// Must match SDF_STORAGE_WEB_USER_SALT_LEN / SDF_SERVICES_WEB_AUTH_NONCE_LEN
// / SDF_SERVICES_WEB_AUTH_RESPONSE_LEN in the firmware. LOGIN_INIT's read
// response is [salt(16)][iteration_count(4, little-endian)][nonce(16)].
export const SALT_LEN = 16;
export const NONCE_LEN = 16;
export const RESPONSE_LEN = 32;
export const CHALLENGE_LEN = SALT_LEN + 4 + NONCE_LEN;

export interface LoginChallenge {
	salt: Uint8Array;
	iterations: number;
	nonce: Uint8Array;
}

/** Widens a byte view to BufferSource for crypto.subtle (same memory). */
function buf(bytes: Uint8Array): BufferSource {
	return bytes as unknown as BufferSource;
}

/** Parses a LOGIN_INIT read response into salt / iterations / nonce. */
export function parseChallenge(bytes: Uint8Array): LoginChallenge {
	if (bytes.byteLength !== CHALLENGE_LEN) {
		throw new Error(`Challenge must be ${CHALLENGE_LEN} bytes, got ${bytes.byteLength}`);
	}
	const salt = bytes.slice(0, SALT_LEN);
	const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
	const iterations = view.getUint32(SALT_LEN, true);
	const nonce = bytes.slice(SALT_LEN + 4, CHALLENGE_LEN);
	return { salt, iterations, nonce };
}

/** REGISTER command payload: [0x02][name_len][name][sha256(password)]. */
export function encodeRegister(name: string, passwordHash: Uint8Array): Uint8Array {
	const nameBytes = new TextEncoder().encode(name);
	if (nameBytes.length > 31) {
		throw new Error('Username is limited to 31 bytes on the wire');
	}
	if (passwordHash.length !== RESPONSE_LEN) {
		throw new Error('Password hash must be 32 bytes');
	}
	const payload = new Uint8Array(2 + nameBytes.length + passwordHash.length);
	payload[0] = AUTH_OPCODE_REGISTER;
	payload[1] = nameBytes.length;
	payload.set(nameBytes, 2);
	payload.set(passwordHash, 2 + nameBytes.length);
	return payload;
}

/** LOGIN_INIT command payload: [0x03][name_len][name]. */
export function encodeLoginInit(name: string): Uint8Array {
	const nameBytes = new TextEncoder().encode(name);
	if (nameBytes.length > 31) {
		throw new Error('Username is limited to 31 bytes on the wire');
	}
	const payload = new Uint8Array(2 + nameBytes.length);
	payload[0] = AUTH_OPCODE_LOGIN_INIT;
	payload[1] = nameBytes.length;
	payload.set(nameBytes, 2);
	return payload;
}

/** LOGIN_VERIFY command payload: [0x04][response(32)]. */
export function encodeLoginVerify(response: Uint8Array): Uint8Array {
	if (response.length !== RESPONSE_LEN) {
		throw new Error('Login response must be 32 bytes');
	}
	const payload = new Uint8Array(1 + response.length);
	payload[0] = AUTH_OPCODE_LOGIN_VERIFY;
	payload.set(response, 1);
	return payload;
}

/** SHA-256 of the UTF-8 password — what REGISTER carries on the wire. */
export async function hashPassword(password: string): Promise<Uint8Array> {
	const hashBuffer = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(password));
	return new Uint8Array(hashBuffer);
}

/**
 * PBKDF2-HMAC-SHA256 credential stretching, run client-side at LOGIN so the
 * device never has to spend the (expensive, tunable) stretching cost on its
 * own CPU per login attempt - only once, server-side, at REGISTER. Mirrors
 * sdf_services_web_auth_stretch_credential() in the firmware.
 */
export async function stretchPassword(
	password: string,
	salt: Uint8Array,
	iterationCount: number
): Promise<Uint8Array> {
	const passwordKey = await crypto.subtle.importKey(
		'raw',
		new TextEncoder().encode(password),
		{ name: 'PBKDF2' },
		false,
		['deriveBits']
	);
	const bits = await crypto.subtle.deriveBits(
		{ name: 'PBKDF2', salt: buf(salt), iterations: iterationCount, hash: 'SHA-256' },
		passwordKey,
		RESPONSE_LEN * 8
	);
	return new Uint8Array(bits);
}

/**
 * HMAC-SHA256(stretched_credential, nonce) - the LOGIN_VERIFY response.
 * Mirrors sdf_services_web_auth_verify_response()'s expected-response
 * computation in the firmware.
 */
export async function computeLoginResponse(
	stretchedCredential: Uint8Array,
	nonce: Uint8Array
): Promise<Uint8Array> {
	const hmacKey = await crypto.subtle.importKey(
		'raw',
		buf(stretchedCredential),
		{ name: 'HMAC', hash: 'SHA-256' },
		false,
		['sign']
	);
	const signature = await crypto.subtle.sign('HMAC', hmacKey, buf(nonce));
	return new Uint8Array(signature);
}
