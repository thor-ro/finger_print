/**
 * Companion user-management protocol over the Enrollment characteristic.
 *
 * PURE module: no DOM, no navigator, no SvelteKit imports.
 *
 * Every write carries a client-supplied request id ("req") and produces
 * exactly one terminal reply carrying that id - even when refused before
 * any work starts. Replies are correlated by request id (a Map of pending
 * resolvers, not a single slot), so a reply arriving ten seconds after its
 * request - e.g. after the admin walked over and scanned - is attributable.
 *
 * Wire shapes (see doc/sdf_sas.md):
 *   request:   {"req":N,"verb":"list"|"enroll"|"delete"|"set_permission"|"rename",...}
 *   reply:     {"req":N,"result":"<outcome>"}
 *   list part: {"req":N,"verb":"list","part":i,"end":true|false,"users":[...]}
 *   progress:  {"status":"success"|"failed","user_id":..,"step":..,"error_code":..}
 *              plus "req":N when started by a user-management request.
 */

export interface UmRequest {
	verb: 'list' | 'enroll' | 'delete' | 'set_permission' | 'rename';
	user_id?: number;
	permission?: number;
	name?: string;
}

/** Encodes a user-management request with its correlation id. */
export function encodeUmRequest(req: number, request: UmRequest): Uint8Array {
	return new TextEncoder().encode(JSON.stringify({ req, ...request }));
}

export type Notification = Record<string, unknown>;

/** Decodes an Enrollment-characteristic notification; null if not valid JSON. */
export function decodeNotification(bytes: Uint8Array): Notification | null {
	try {
		return JSON.parse(new TextDecoder().decode(bytes));
	} catch {
		return null;
	}
}

export function isListPart(data: Notification): boolean {
	return Array.isArray(data.users);
}

export function isUmReply(data: Notification): boolean {
	return data.result !== undefined && !isListPart(data);
}

export function requestIdOf(data: Notification): number | undefined {
	return typeof data.req === 'number' ? data.req : undefined;
}

/** The users carried by a list part (already validated as an array). */
export function usersOf(data: Notification): Array<{ id: number; name?: string; perm: number }> {
	return data.users as Array<{ id: number; name?: string; perm: number }>;
}

/**
 * Renders each named refusal specifically rather than as a generic failure.
 * Each reason maps to its own message; none collapse into a shared text.
 */
export const UM_RESULT_MESSAGES: Record<string, string> = {
	ok: 'Completed.',
	not_found: 'No such enrolled user.',
	id_occupied: 'That user ID is already enrolled.',
	last_admin: 'Refused: this would leave the device without any admin.',
	name_taken: 'That name is already used by another user.',
	busy: 'Device busy with another action - try again shortly.',
	denied: 'Denied: the fingerprint scanned was not an admin finger.',
	timeout: 'Timed out: no admin fingerprint was scanned on the device.',
	invalid: 'The device rejected the request as malformed.'
};

const GENERIC_FAILURE_PREFIX = 'Request failed';

export function umResultMessage(result: string): string {
	return UM_RESULT_MESSAGES[result] ?? `${GENERIC_FAILURE_PREFIX} (${result}).`;
}

/**
 * The permission picker offers only Admin and Standard: level 2 stays a
 * reserved placeholder (companion-identity).
 */
export function umPermissionName(p: number): string {
	if (p === 3) return 'Admin';
	if (p === 1) return 'Standard';
	return `Level ${p}`;
}
