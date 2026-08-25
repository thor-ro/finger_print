import { describe, expect, it } from 'vitest';
import type { HealthEntry } from './health';
import {
	batteryLineForOtaWarning,
	describeBattery,
	describeLockState,
	formatAge,
	healthRows,
	healthValue,
	parseHealthReport,
	withAgeSuffix
} from './health';

describe('three-valued vocabulary', () => {
	const measured: HealthEntry = { state: 'measured', ready: true };
	const unknown: HealthEntry = { state: 'unknown' };
	const notApplicable: HealthEntry = { state: 'not_applicable' };

	it('distinguishes measured from unknown from not-applicable', () => {
		const fmt = (e: HealthEntry) => (e.ready ? 'Ready' : 'Not responding');
		expect(healthValue(measured, fmt)).toBe('Ready');
		expect(healthValue(unknown, fmt)).toBe('Unknown');
		expect(healthValue(notApplicable, fmt)).toBe('N/A');
		expect(healthValue(undefined, fmt)).toBe('Unknown');
	});

	it('never renders a carried-over value as current', () => {
		// An absent entry (nothing ever reported) is Unknown, not a stale value.
		expect(healthValue(undefined, () => 'Ready')).toBe('Unknown');
	});
});

describe('staleness', () => {
	it('flags old readings with their age instead of passing them as current', () => {
		expect(withAgeSuffix('42%', undefined)).toBe('42%');
		expect(withAgeSuffix('42%', 59000)).toBe('42%');
		expect(withAgeSuffix('42%', 61000)).toMatch(/^42% \(reading \d+ s? ?.*old\)$/);
		expect(withAgeSuffix('42%', 61_000)).toContain('(reading 1 min old)');
	});

	it('formats ages readably', () => {
		expect(formatAge(500)).toBe('500 ms');
		expect(formatAge(30_000)).toBe('30 s');
		expect(formatAge(120_000)).toBe('2 min');
	});
});

describe('report parsing', () => {
	it('parses a full report', () => {
		const report = parseHealthReport(
			JSON.stringify({
				lock: { state: 'locked', source: 'confirmed' },
				battery: { percent: 87 },
				firmware: 'v1.0'
			})
		);
		expect(describeLockState(report)).toBe('locked');
		expect(describeBattery(report)).toBe('87%');
	});

	it('rejects non-object payloads', () => {
		expect(() => parseHealthReport('[1,2]')).toThrow();
	});

	it('lock state shows assumed readings as awaiting confirmation', () => {
		const report = parseHealthReport(
			JSON.stringify({ lock: { state: 'unlocked', source: 'assumed' } })
		);
		expect(describeLockState(report)).toBe('unlocked (awaiting confirmation)');
	});

	it('unknown lock state stays unknown, never an empty string', () => {
		const report = parseHealthReport(JSON.stringify({ lock: { state: 'unknown' } }));
		expect(describeLockState(report)).toBe('Unknown');
		expect(describeLockState(parseHealthReport('{}'))).toBe('Unknown');
	});

	it('missing battery is Unknown, never the configured default', () => {
		expect(describeBattery(parseHealthReport('{}'))).toBe('Unknown');
		expect(describeBattery(parseHealthReport(JSON.stringify({ battery: {} })))).toBe('Unknown');
	});
});

describe('health rows', () => {
	it('renders every subsystem row with the vocabulary applied', () => {
		const rows = healthRows(
			parseHealthReport(
				JSON.stringify({
					lock: { state: 'locked', source: 'confirmed' },
					battery: { percent: 50 },
					fingerprint: { state: 'measured', ready: true },
					nuki: { state: 'not_applicable' },
					zigbee: { state: 'measured', joined: true }
				})
			)
		);
		const byLabel = new Map(rows.map((r) => [r.label, r.value]));
		expect(byLabel.get('Fingerprint sensor')).toBe('Ready');
		expect(byLabel.get('Nuki link')).toBe('N/A');
		expect(byLabel.get('Zigbee network')).toBe('joined');
		expect(byLabel.get('Firmware')).toBe('Unknown');
	});
});

describe('OTA pre-flight battery line', () => {
	it('states the measured level when there is one', () => {
		const report = parseHealthReport(JSON.stringify({ battery: { percent: 33 } }));
		expect(batteryLineForOtaWarning(report)).toContain('33%');
	});

	it('says so when no measurement exists - never implies it was checked', () => {
		expect(batteryLineForOtaWarning(null)).toContain('unknown');
		expect(batteryLineForOtaWarning(parseHealthReport('{}'))).toContain('unknown');
	});
});
