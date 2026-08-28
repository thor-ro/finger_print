import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import {
	ENROLL_DEFAULT_SCANS,
	UM_RESULT_MESSAGES,
	decodeNotification,
	encodeUmRequest,
	enrollProgressOf,
	isEnrollProgress,
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

/**
 * The outcome names sdf_services_um_outcome_name() can put on the wire, read
 * from the firmware source so the companion's table cannot silently fall
 * behind it.
 */
function firmwareOutcomeNames(): string[] {
	const source = resolve(
		dirname(fileURLToPath(import.meta.url)),
		'../../../../firmware/components/sdf_services/src/sdf_services.c'
	);
	const text = readFileSync(source, 'utf8');
	const start = text.indexOf('const char *sdf_services_um_outcome_name');
	if (start < 0) throw new Error('sdf_services_um_outcome_name() not found in the firmware source');
	const body = text.slice(start, text.indexOf('\n}', start));
	return [...new Set([...body.matchAll(/return "(\w+)";/g)].map((match) => match[1]))];
}

describe('refusal reasons map to distinct messages', () => {
	// Read from the firmware source rather than copied by hand: a new outcome
	// there must fail this test instead of reaching users as a raw token.
	const reasons = firmwareOutcomeNames().filter((name) => name !== 'ok');

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


describe('enrolment progress notifications', () => {
	it('recognises a progress notification', () => {
		expect(isEnrollProgress({ status: 'progress', captured: 1, step: 2, total: 3 })).toBe(true);
	});

	it('does not mistake an outcome or a reply for progress', () => {
		expect(isEnrollProgress({ status: 'success', user_id: 1 })).toBe(false);
		expect(isEnrollProgress({ status: 'failed', step: 2, error_code: 1 })).toBe(false);
		expect(isEnrollProgress({ req: 4, result: 'ok' })).toBe(false);
		expect(isEnrollProgress({ req: 4, users: [] })).toBe(false);
	});

	it('is not classified as a terminal reply, so it resolves nothing', () => {
		const progress = { status: 'progress', captured: 1, step: 2, total: 3, req: 9 };
		expect(isUmReply(progress)).toBe(false);
		expect(isListPart(progress)).toBe(false);
	});

	it('carries the request id without it being a reply', () => {
		expect(requestIdOf({ status: 'progress', step: 1, req: 12 })).toBe(12);
	});

	it('reads captured, expected scan and total', () => {
		expect(enrollProgressOf({ status: 'progress', captured: 2, step: 3, total: 3 })).toEqual({
			captured: 2,
			step: 3,
			total: 3
		});
	});

	it('defaults the total to the usual scan count', () => {
		expect(enrollProgressOf({ status: 'progress', captured: 1, step: 2 }).total).toBe(
			ENROLL_DEFAULT_SCANS
		);
	});

	it('derives the captured count from the expected scan when it is absent', () => {
		expect(enrollProgressOf({ status: 'progress', step: 3 }).captured).toBe(2);
	});

	it('clamps values a device should never send', () => {
		expect(enrollProgressOf({ status: 'progress', captured: 9, step: 9, total: 3 })).toEqual({
			captured: 3,
			step: 3,
			total: 3
		});
		expect(enrollProgressOf({ status: 'progress', captured: -1, step: 0, total: 3 })).toEqual({
			captured: 0,
			step: 1,
			total: 3
		});
	});

	it('decodes a device progress frame end to end', () => {
		const frame = new TextEncoder().encode('{"status":"progress","captured":1,"step":2,"total":3}');
		const parsed = decodeNotification(frame)!;
		expect(isEnrollProgress(parsed)).toBe(true);
		expect(enrollProgressOf(parsed)).toEqual({ captured: 1, step: 2, total: 3 });
	});
});
