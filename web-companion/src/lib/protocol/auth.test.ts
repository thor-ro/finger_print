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

// Vectors captured from the legacy app's derivation so the port is provably
// unchanged: PBKDF2-HMAC-SHA256 stretch then HMAC-SHA256(stretched, nonce).
const VECTOR = {
	password: 'correct horse',
	salt: Uint8Array.from([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]),
	iterations: 1000,
	nonce: Uint8Array.from([255, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 244, 243, 242, 241, 240]),
	stretchedHex: 'c914cc4f06cc6e8f46d157e3a1b5aa7abceebb17bb0444cd4c4ac16ca2ae9864',
	responseHex: '6472866d04315dabfa2910ebc5d18d59a50e925bdede4e9562cd89a6fa4fc9f4',
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
