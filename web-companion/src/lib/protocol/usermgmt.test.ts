import { describe, expect, it } from 'vitest';
import {
	UM_RESULT_MESSAGES,
	decodeNotification,
	encodeUmRequest,
	isListPart,
	isUmReply,
	requestIdOf,
	umPermissionName,
	umResultMessage
} from './usermgmt';

describe('request encoding', () => {
	it('embeds the correlation id and the verb', () => {
		const bytes = encodeUmRequest(7, { verb: 'delete', user_id: 3 });
		expect(JSON.parse(new TextDecoder().decode(bytes))).toEqual({
			req: 7,
			verb: 'delete',
			user_id: 3
		});
	});
});

describe('reply decoding and classification', () => {
	it('decodes JSON notifications', () => {
		expect(decodeNotification(new TextEncoder().encode('{"req":1,"result":"ok"}'))).toEqual({
			req: 1,
			result: 'ok'
		});
	});

	it('returns null for non-JSON notifications', () => {
		expect(decodeNotification(new TextEncoder().encode('<html>'))).toBeNull();
	});

	it('classifies replies, list parts and progress apart', () => {
		const reply = { req: 1, result: 'ok' };
		const part = { req: 2, users: [{ id: 1, perm: 3 }], end: true };
		const progress = { status: 'success', user_id: 4 };

		expect(isUmReply(reply)).toBe(true);
		expect(isListPart(reply)).toBe(false);
		expect(isListPart(part)).toBe(true);
		expect(isUmReply(progress)).toBe(false);
		expect(requestIdOf(part)).toBe(2);
	});
});

describe('refusal reasons map to distinct messages', () => {
	// Every outcome sdf_services_um_outcome_name() can emit on the wire.
	const reasons = [
		'not_found',
		'id_occupied',
		'last_admin',
		'name_taken',
		'busy',
		'denied',
		'timeout',
		'invalid',
		'failed',
		'unavailable'
	];

	it('every named refusal has its own message', () => {
		for (const reason of reasons) {
			const message = umResultMessage(reason);
			expect(message).toBeTruthy();
			expect(message).not.toBe(`Request failed (${reason}).`);
		}
	});

	it('no device outcome reaches the user as a raw token', () => {
		for (const reason of reasons) {
			expect(umResultMessage(reason)).not.toMatch(/\(\w+\)\.$/);
		}
	});

	it('no two refusals collapse into the same message', () => {
		const messages = new Set(reasons.map((r) => umResultMessage(r)));
		expect(messages.size).toBe(reasons.length);
	});

	it('unknown reasons fall back to a generic failure naming them', () => {
		expect(umResultMessage('something_new')).toBe('Request failed (something_new).');
	});

	it('the declared table covers exactly the firmware outcomes plus ok', () => {
		expect(Object.keys(UM_RESULT_MESSAGES).sort()).toEqual([...reasons, 'ok'].sort());
	});
});

describe('permission names', () => {
	it('names Admin and Standard; level 2 stays a reserved placeholder', () => {
		expect(umPermissionName(3)).toBe('Admin');
		expect(umPermissionName(1)).toBe('Standard');
		expect(umPermissionName(2)).toBe('Level 2');
	});
});
