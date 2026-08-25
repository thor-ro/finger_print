/**
 * OTA chunked-transfer framing (BEGIN 0x01 / CHUNK 0x02 / END 0x03).
 *
 * PURE module: no DOM, no navigator, no SvelteKit imports.
 *
 * Web Bluetooth does not expose the connection's negotiated ATT MTU, so the
 * app uses a conservative fixed chunk payload size safely below the
 * smallest commonly-negotiated MTU (~185 bytes), halving it on an
 * over-MTU write rejection down to MIN_CHUNK_SIZE.
 */

export const OTA_OPCODE_BEGIN = 0x01;
export const OTA_OPCODE_CHUNK = 0x02;
export const OTA_OPCODE_END = 0x03;

export const INITIAL_CHUNK_SIZE = 180;
export const MIN_CHUNK_SIZE = 20;

/** BEGIN payload: [0x01][image size, uint32 little-endian]. */
export function beginPayload(imageSize: number): Uint8Array {
	const payload = new Uint8Array(5);
	payload[0] = OTA_OPCODE_BEGIN;
	new DataView(payload.buffer).setUint32(1, imageSize, true);
	return payload;
}

/** CHUNK payload: [0x02][chunk bytes]. */
export function chunkPayload(chunk: Uint8Array): Uint8Array {
	const payload = new Uint8Array(1 + chunk.length);
	payload[0] = OTA_OPCODE_CHUNK;
	payload.set(chunk, 1);
	return payload;
}

/** END payload: [0x03]. Exactly one byte - there is nothing after it. */
export function endPayload(): Uint8Array {
	return new Uint8Array([OTA_OPCODE_END]);
}

/** Halves a chunk size on an over-MTU write rejection, floored at the minimum. */
export function halveChunkSize(size: number): number {
	return Math.max(MIN_CHUNK_SIZE, Math.floor(size / 2));
}

/** The byte range [offset, end) of the next CHUNK within the image. */
export function nextChunkRange(
	offset: number,
	imageSize: number,
	chunkSize: number
): { start: number; end: number } {
	if (offset >= imageSize) {
		throw new Error('Transfer already complete');
	}
	return { start: offset, end: Math.min(offset + chunkSize, imageSize) };
}

export interface DeviceStatus {
	status?: string;
	offset?: number;
	error?: string;
}

/**
 * Extracts the resume offset from a BEGIN response. The device reports how
 * much of the image it already holds, so a transfer interrupted by a
 * disconnect resumes from that offset rather than restarting.
 */
export function resumeOffsetFromReady(data: DeviceStatus): number {
	if (data.status !== 'ready' || typeof data.offset !== 'number') {
		throw new Error(`Unexpected response to OTA begin: ${JSON.stringify(data)}`);
	}
	return data.offset;
}
