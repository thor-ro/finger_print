import { describe, expect, it } from 'vitest';
import {
	CHALLENGE_LEN,
	RESPONSE_LEN,
	AUTH_OPCODE_REGISTER,
	AUTH_OPCODE_LOGIN_INIT,
	AUTH_OPCODE_LOGIN_VERIFY,
	computeLoginResponse,
	encodeLoginInit,
	encodeLoginVerify,
	encodeRegister,
	hashPassword,
	parseChallenge,
	stretchPassword
} from './auth';

// Vectors computed from the FIRMWARE's definition of the credential:
// PBKDF2-HMAC-SHA256(SHA-256(password), salt, iterations), then
// HMAC-SHA256(stretched, nonce).
//
// These previously held vectors "captured from the legacy app's derivation",
// which stretched the raw password instead. That pinned the client to its own
// behaviour rather than to sdf_services_web_auth_stretch_credential(), so a
// derivation that could never match the device passed its tests and every
// login failed on hardware. Derive expectations from the contract, never from
// what this module happens to do.
const VECTOR = {
	password: 'correct horse',
	salt: Uint8Array.from([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]),
	iterations: 1000,
	nonce: Uint8Array.from([255, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 244, 243, 242, 241, 240]),
	stretchedHex: 'b094fa8ecd7e2c0bf520b0f0382584810a3adad51f0a9356c667cd82c1504b8f',
	responseHex: '284d7dca4687f5565aecf6aba8bad349441c9f67f9620290af37c3694082c435',
	sha256Hex: '4104d36f8da2c254349f85836793ebe029e0c957063a34c91c2e9203187b5631'
};

function hex(bytes: Uint8Array): string {
	return Buffer.from(bytes).toString('hex');
}

describe('auth challenge parsing', () => {
	it('parses salt(16) + iterations(4 LE) + nonce(16)', () => {
		const bytes = new Uint8Array(CHALLENGE_LEN);
		bytes.set(VECTOR.salt, 0);
		new DataView(bytes.buffer).setUint32(16, VECTOR.iterations, true);
		bytes.set(VECTOR.nonce, 20);

		const challenge = parseChallenge(bytes);
		expect(hex(challenge.salt)).toBe(hex(VECTOR.salt));
		expect(challenge.iterations).toBe(VECTOR.iterations);
		expect(hex(challenge.nonce)).toBe(hex(VECTOR.nonce));
	});

	it('rejects a wrong-length read', () => {
		expect(() => parseChallenge(new Uint8Array(CHALLENGE_LEN - 1))).toThrow();
		expect(() => parseChallenge(new Uint8Array(0))).toThrow();
	});
});

describe('credential derivation', () => {
	it('stretches the password to the captured vector', async () => {
		const stretched = await stretchPassword(VECTOR.password, VECTOR.salt, VECTOR.iterations);
		expect(stretched.length).toBe(RESPONSE_LEN);
		expect(hex(stretched)).toBe(VECTOR.stretchedHex);
	});

	it('computes the login response as HMAC(stretched, nonce)', async () => {
		const stretched = await stretchPassword(VECTOR.password, VECTOR.salt, VECTOR.iterations);
		const response = await computeLoginResponse(stretched, VECTOR.nonce);
		expect(response.length).toBe(RESPONSE_LEN);
		expect(hex(response)).toBe(VECTOR.responseHex);
	});

	it('derivation is deterministic and key-dependent', async () => {
		const a = await computeLoginResponse(
			await stretchPassword(VECTOR.password, VECTOR.salt, VECTOR.iterations),
			VECTOR.nonce
		);
		const b = await computeLoginResponse(
			await stretchPassword('other password', VECTOR.salt, VECTOR.iterations),
			VECTOR.nonce
		);
		expect(hex(a)).not.toBe(hex(b));
	});

	it('hashes the password for REGISTER', async () => {
		expect(hex(await hashPassword(VECTOR.password))).toBe(VECTOR.sha256Hex);
	});
});

describe('command encoding', () => {
	it('encodes REGISTER as [0x02][len][name][sha256]', async () => {
		const hash = await hashPassword(VECTOR.password);
		const payload = encodeRegister('alice', hash);
		expect(payload[0]).toBe(AUTH_OPCODE_REGISTER);
		expect(payload[1]).toBe(5);
		expect(new TextDecoder().decode(payload.slice(2, 7))).toBe('alice');
		expect(hex(payload.slice(7))).toBe(VECTOR.sha256Hex);
		expect(payload.length).toBe(2 + 5 + 32);
	});

	it('encodes LOGIN_INIT as [0x03][len][name]', () => {
		const payload = encodeLoginInit('bob');
		expect(payload[0]).toBe(AUTH_OPCODE_LOGIN_INIT);
		expect(payload[1]).toBe(3);
		expect(new TextDecoder().decode(payload.slice(2))).toBe('bob');
	});

	it('encodes LOGIN_VERIFY as [0x04][response]', async () => {
		const response = await computeLoginResponse(
			await stretchPassword(VECTOR.password, VECTOR.salt, VECTOR.iterations),
			VECTOR.nonce
		);
		const payload = encodeLoginVerify(response);
		expect(payload[0]).toBe(AUTH_OPCODE_LOGIN_VERIFY);
		expect(payload.length).toBe(33);
		expect(hex(payload.slice(1))).toBe(VECTOR.responseHex);
	});

	it('rejects names over the 31-byte wire limit', () => {
		expect(() => encodeLoginInit('x'.repeat(32))).toThrow();
	});
});

describe('the stretch key material matches what REGISTER sent', () => {
	/* Independent re-derivation, so this asserts the firmware's contract
	 * rather than the implementation's current behaviour. */
	async function pbkdf2(keyMaterial: Uint8Array): Promise<string> {
		const key = await crypto.subtle.importKey(
			'raw',
			keyMaterial as unknown as BufferSource,
			{ name: 'PBKDF2' },
			false,
			['deriveBits']
		);
		const bits = await crypto.subtle.deriveBits(
			{
				name: 'PBKDF2',
				salt: VECTOR.salt as unknown as BufferSource,
				iterations: VECTOR.iterations,
				hash: 'SHA-256'
			},
			key,
			RESPONSE_LEN * 8
		);
		return hex(new Uint8Array(bits));
	}

	it('stretches SHA-256(password) - the bytes REGISTER puts on the wire', async () => {
		const stretched = await stretchPassword(VECTOR.password, VECTOR.salt, VECTOR.iterations);
		expect(hex(stretched)).toBe(await pbkdf2(await hashPassword(VECTOR.password)));
	});

	it('does NOT stretch the raw password', async () => {
		// The device never receives the raw password, so a credential derived
		// from it cannot match the one stored at REGISTER.
		const stretched = await stretchPassword(VECTOR.password, VECTOR.salt, VECTOR.iterations);
		expect(hex(stretched)).not.toBe(await pbkdf2(new TextEncoder().encode(VECTOR.password)));
	});
});
