import { describe, expect, it } from 'vitest';
import {
	INITIAL_CHUNK_SIZE,
	MIN_CHUNK_SIZE,
	OTA_OPCODE_BEGIN,
	OTA_OPCODE_CHUNK,
	OTA_OPCODE_END,
	beginPayload,
	chunkPayload,
	endPayload,
	halveChunkSize,
	nextChunkRange,
	resumeOffsetFromReady
} from './ota';

describe('framing', () => {
	it('BEGIN is [0x01][uint32 LE size]', () => {
		const payload = beginPayload(123456);
		expect(payload.length).toBe(5);
		expect(payload[0]).toBe(OTA_OPCODE_BEGIN);
		expect(new DataView(payload.buffer).getUint32(1, true)).toBe(123456);
	});

	it('CHUNK is [0x02][bytes]', () => {
		const data = Uint8Array.from([1, 2, 3]);
		const payload = chunkPayload(data);
		expect(payload.length).toBe(4);
		expect(payload[0]).toBe(OTA_OPCODE_CHUNK);
		expect([...payload.slice(1)]).toEqual([1, 2, 3]);
	});

	it('END is exactly [0x03]', () => {
		const payload = endPayload();
		expect([...payload]).toEqual([OTA_OPCODE_END]);
	});
});

describe('chunk boundaries', () => {
	it('first chunk starts at the resume offset and honours the chunk size', () => {
		expect(nextChunkRange(0, 1000, INITIAL_CHUNK_SIZE)).toEqual({
			start: 0,
			end: INITIAL_CHUNK_SIZE
		});
	});

	it('final chunk stops at the image size', () => {
		expect(nextChunkRange(950, 1000, 180)).toEqual({ start: 950, end: 1000 });
	});

	it('walks the image without overlap or gap', () => {
		let offset = 37; // simulate resuming mid-image
		const seen: Array<[number, number]> = [];
		while (offset < 1000) {
			const range = nextChunkRange(offset, 1000, 180);
			seen.push([range.start, range.end]);
			offset = range.end;
		}
		expect(seen[0][0]).toBe(37);
		expect(seen[seen.length - 1][1]).toBe(1000);
		for (let i = 1; i < seen.length; i++) {
			expect(seen[i][0]).toBe(seen[i - 1][1]);
		}
	});

	it('refuses to chunk past the end', () => {
		expect(() => nextChunkRange(1000, 1000, 180)).toThrow();
	});
});

describe('chunk sizing', () => {
	it('halves on rejection down to the floor', () => {
		expect(halveChunkSize(INITIAL_CHUNK_SIZE)).toBe(90);
		expect(halveChunkSize(90)).toBe(45);
		expect(halveChunkSize(45)).toBe(Math.max(MIN_CHUNK_SIZE, 22));
	});

	it('never drops below the minimum once there', () => {
		expect(halveChunkSize(MIN_CHUNK_SIZE)).toBe(MIN_CHUNK_SIZE);
		expect(halveChunkSize(MIN_CHUNK_SIZE + 1)).toBeGreaterThanOrEqual(MIN_CHUNK_SIZE);
	});
});

describe('resume offset', () => {
	it('is taken from a ready response', () => {
		expect(resumeOffsetFromReady({ status: 'ready', offset: 512 })).toBe(512);
	});

	it('rejects an unexpected BEGIN response', () => {
		expect(() => resumeOffsetFromReady({ status: 'failed', error: 'no session' })).toThrow();
		expect(() => resumeOffsetFromReady({ status: 'ready' })).toThrow();
	});
});
