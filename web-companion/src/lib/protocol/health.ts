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
	/**
	 * Which vocabulary condition the value is in. The rendered TEXT carries
	 * the distinction; `kind` only lets the view apply the reinforcing
	 * theme token (--unknown / --not-applicable / --assumed). Never used
	 * instead of the text.
	 */
	kind?: 'unknown' | 'not_applicable' | 'assumed';
}

/** Vocabulary kind of a generic health entry (pure - no rendering). */
export function healthEntryKind(entry: HealthEntry | undefined): 'unknown' | 'not_applicable' | 'measured' {
	if (!entry || entry.state === 'unknown') return 'unknown';
	if (entry.state === 'not_applicable') return 'not_applicable';
	return 'measured';
}

/** Vocabulary kind of the lock reading, including the assumed case. */
export function lockKind(report: HealthReport): 'unknown' | 'assumed' | 'measured' {
	const lock = report.lock;
	if (!lock || !lock.state || lock.state === 'unknown') return 'unknown';
	return lock.source === 'assumed' ? 'assumed' : 'measured';
}

/** The full health table as plain label/value rows. */
export function healthRows(report: HealthReport): HealthRow[] {
	const vocab = (k: 'unknown' | 'not_applicable' | 'assumed' | 'measured') =>
		k === 'measured' ? undefined : k;
	const lockK = lockKind(report);
	return [
		{ label: 'Lock state', value: describeLockState(report), kind: vocab(lockK) },
		{
			label: 'Battery',
			value: describeBattery(report),
			kind:
				report.battery && typeof report.battery.percent === 'number'
					? undefined
					: 'unknown'
		},
		{
			label: 'Alarms',
			value: report.alarms ? `mask ${report.alarms.mask}` : 'Unknown',
			kind: report.alarms ? undefined : 'unknown'
		},
		{
			label: 'Fingerprint sensor',
			value: healthValue(report.fingerprint, (e) => (e.ready ? 'Ready' : 'Not responding')),
			kind: vocab(healthEntryKind(report.fingerprint))
		},
		{
			label: 'Nuki link',
			value: healthValue(
				report.nuki,
				(e) => `${e.paired ? 'paired' : 'not paired'}, ${e.connected ? 'connected' : 'disconnected'}`
			),
			kind: vocab(healthEntryKind(report.nuki))
		},
		{
			label: 'Zigbee network',
			value: healthValue(report.zigbee, (e) => (e.joined ? 'joined' : 'not joined')),
			kind: vocab(healthEntryKind(report.zigbee))
		},
		{ label: 'Firmware', value: report.firmware || 'Unknown', kind: report.firmware ? undefined : 'unknown' },
		{ label: 'OTA state', value: report.ota || 'Unknown', kind: report.ota ? undefined : 'unknown' },
		{ label: 'Setup state', value: report.setup || 'Unknown', kind: report.setup ? undefined : 'unknown' }
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
