/**
 * Device health report parsing.
 *
 * PURE module: no DOM, no navigator, no SvelteKit imports. Produces display
 * strings from the report the device sends; it never invents values.
 *
 * The dashboard shows only values the device actually reported. Every field
 * of the health report is measured, unknown, or not applicable; unknown is
 * displayed as unknown and not-applicable as N/A - never a number, never a
 * carried-over earlier value. The configured default battery percentage is
 * a setting, not a measurement, and is never shown as the battery level.
 */

export interface HealthEntry {
	state?: 'measured' | 'unknown' | 'not_applicable';
	age_ms?: number;
	ready?: boolean;
	paired?: boolean;
	connected?: boolean;
	joined?: boolean;
}

export interface HealthReport {
	lock?: { state?: string; source?: string; age_ms?: number };
	battery?: { percent?: number; age_ms?: number };
	alarms?: { mask?: number };
	fingerprint?: HealthEntry;
	nuki?: HealthEntry;
	zigbee?: HealthEntry;
	firmware?: string;
	ota?: string;
	setup?: string;
}

/** A reading older than this is surfaced with its age so it cannot pass as current. */
export const HEALTH_STALE_AGE_MS = 60000;

export function parseHealthReport(text: string): HealthReport {
	const parsed = JSON.parse(text) as HealthReport;
	if (typeof parsed !== 'object' || parsed === null || Array.isArray(parsed)) {
		throw new Error('Health report must be a JSON object');
	}
	return parsed;
}

export function formatAge(ageMs: number): string {
	if (ageMs < 1000) return `${ageMs} ms`;
	if (ageMs < 60000) return `${Math.round(ageMs / 1000)} s`;
	return `${Math.round(ageMs / 60000)} min`;
}

export function withAgeSuffix(text: string, ageMs: number | undefined): string {
	if (typeof ageMs !== 'number' || ageMs < HEALTH_STALE_AGE_MS) return text;
	return `${text} (reading ${formatAge(ageMs)} old)`;
}

export function healthValue(
	entry: HealthEntry | undefined,
	formatMeasured: (e: HealthEntry) => string
): string {
	if (!entry || entry.state === 'unknown') return 'Unknown';
	if (entry.state === 'not_applicable') return 'N/A';
	return formatMeasured(entry);
}

/**
 * Lock state for display. An assumed state was derived from a command we
 * sent, not from the lock itself - show it as awaiting confirmation, never
 * as equal to a confirmed reading. Unknown stays unknown.
 */
export function describeLockState(report: HealthReport): string {
	const lock = report.lock;
	if (!lock || !lock.state || lock.state === 'unknown') return 'Unknown';
	let lockText = lock.state.replace(/_/g, ' ');
	if (lock.source === 'assumed') {
		lockText += ' (awaiting confirmation)';
	}
	return withAgeSuffix(lockText, lock.age_ms);
}

/** Battery level for display. Unknown stays unknown - never a default. */
export function describeBattery(report: HealthReport): string {
	const battery = report.battery;
	if (!battery || typeof battery.percent !== 'number') return 'Unknown';
	return withAgeSuffix(`${battery.percent}%`, battery.age_ms);
}

export interface HealthRow {
	label: string;
	value: string;
}

/** The full health table as plain label/value rows. */
export function healthRows(report: HealthReport): HealthRow[] {
	return [
		{ label: 'Lock state', value: describeLockState(report) },
		{ label: 'Battery', value: describeBattery(report) },
		{ label: 'Alarms', value: report.alarms ? `mask ${report.alarms.mask}` : 'Unknown' },
		{
			label: 'Fingerprint sensor',
			value: healthValue(report.fingerprint, (e) => (e.ready ? 'Ready' : 'Not responding'))
		},
		{
			label: 'Nuki link',
			value: healthValue(
				report.nuki,
				(e) => `${e.paired ? 'paired' : 'not paired'}, ${e.connected ? 'connected' : 'disconnected'}`
			)
		},
		{
			label: 'Zigbee network',
			value: healthValue(report.zigbee, (e) => (e.joined ? 'joined' : 'not joined'))
		},
		{ label: 'Firmware', value: report.firmware || 'Unknown' },
		{ label: 'OTA state', value: report.ota || 'Unknown' },
		{ label: 'Setup state', value: report.setup || 'Unknown' }
	];
}

/**
 * The device's reported battery level for the OTA pre-flight warning. When
 * no measurement is available, say so - never imply the level was checked,
 * and never substitute the configured default.
 */
export function batteryLineForOtaWarning(report: HealthReport | null): string {
	if (
		report &&
		report.battery &&
		typeof report.battery.percent === 'number'
	) {
		return `The device reports its battery at ${report.battery.percent}%.`;
	}
	return "The device's battery level is currently unknown.";
}
