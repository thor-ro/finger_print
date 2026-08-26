#!/usr/bin/env node
/**
 * Theme gate (themes/CONTRACT.md).
 *
 * For every shipped theme file in themes/*.css this checks:
 *
 *   purity         the file contains exactly one rule whose selector is
 *                  :root[data-theme='<file-name>'] holding ONLY
 *                  custom-property declarations - no element/class
 *                  selectors, no layout/positioning/sizing/visibility,
 *                  no at-rules. A theme cannot style or hide anything.
 *
 *   completeness   every token in the contract is declared. A theme
 *                  missing one fails instead of silently inheriting.
 *
 *   contrast       WCAG contrast ratios for the pairs the app actually
 *                  renders: 4.5:1 for body/status/vocabulary/on-accent
 *                  text, 3:1 for borders and the focus ring. Translucent
 *                  surfaces are COMPOSITED over the background beneath
 *                  them (chained: tint -> panel -> page background), and
 *                  gradients are measured at EVERY stop - never an average
 *                  and never skipped for not being a single colour.
 *
 *                  A token value this check cannot read is a FAILURE, not
 *                  a skipped pair: an unmeasured pair once reported
 *                  "contrast OK" for a 1:1 gradient button.
 *
 * Also verifies themes/CONTRACT.md still names every contract token, so
 * the documented contract cannot drift from the enforced one.
 *
 * Exits non-zero on any violation.
 */
import { readdirSync, readFileSync } from 'node:fs';
import { join, relative } from 'node:path';
import process from 'node:process';

const ROOT = new URL('..', import.meta.url).pathname;
const THEMES_DIR = join(ROOT, 'themes');

/** The contract. Mirrored in themes/CONTRACT.md - keep both in step. */
const CONTRACT = [
	// Surfaces
	'--bg',
	'--bg-image',
	'--panel',
	'--panel-2',
	'--surface-blur',
	// Lines
	'--border',
	'--shadow',
	// Text
	'--text',
	'--muted',
	'--text-on-accent',
	// Accent
	'--accent',
	'--accent-strong',
	'--accent-gradient',
	// Status
	'--ok',
	'--warn',
	'--danger',
	'--info',
	'--warn-tint',
	'--danger-tint',
	'--info-tint',
	// Device vocabulary
	'--unknown',
	'--not-applicable',
	'--assumed',
	// Shape
	'--radius',
	'--radius-lg',
	'--radius-pill',
	// Type
	'--font-body',
	// Rendering intent
	'--color-scheme'
];

const BODY_RATIO = 4.5;
const EDGE_RATIO = 3;

let failures = 0;
function fail(message) {
	failures++;
	console.error(`theme error  ${message}`);
}

// --- colour math ---------------------------------------------------------

function parseColor(text) {
	const t = text.trim();
	let m = t.match(/^#([0-9a-fA-F]{3,8})$/);
	if (m) {
		const hex = m[1];
		const n = hex.length;
		const at = (i, len = 2) => parseInt(hex.slice(i, i + len), 16);
		if (n === 3 || n === 4)
			return {
				r: at(0, 1) * 17,
				g: at(1, 1) * 17,
				b: at(2, 1) * 17,
				a: n === 4 ? at(3, 1) / 15 : 1
			};
		if (n === 6 || n === 8)
			return { r: at(0), g: at(2), b: at(4), a: n === 8 ? at(6) / 255 : 1 };
		return null;
	}
	m = t.match(/^rgba?\(\s*([\d.]+)[\s,]+([\d.]+)[\s,]+([\d.]+)(?:[\s,/]+([\d.%]+))?\s*\)$/i);
	if (m) {
		return { r: +m[1], g: +m[2], b: +m[3], a: alphaOf(m[4]) };
	}
	m = t.match(
		/^hsla?\(\s*([\d.-]+)(?:deg)?[\s,]+([\d.]+)%[\s,]+([\d.]+)%(?:[\s,/]+([\d.%]+))?\s*\)$/i
	);
	if (m) {
		return { ...hslToRgb(+m[1], +m[2] / 100, +m[3] / 100), a: alphaOf(m[4]) };
	}
	return null;
}

function alphaOf(text) {
	if (text === undefined) return 1;
	return text.endsWith('%') ? parseFloat(text) / 100 : parseFloat(text);
}

function hslToRgb(hDeg, s, l) {
	const h = (((hDeg % 360) + 360) % 360) / 60;
	const c = (1 - Math.abs(2 * l - 1)) * s;
	const x = c * (1 - Math.abs((h % 2) - 1));
	const [r, g, b] =
		h < 1 ? [c, x, 0]
		: h < 2 ? [x, c, 0]
		: h < 3 ? [0, c, x]
		: h < 4 ? [0, x, c]
		: h < 5 ? [x, 0, c]
		: [c, 0, x];
	const m = l - c / 2;
	return { r: (r + m) * 255, g: (g + m) * 255, b: (b + m) * 255 };
}

/**
 * Value syntax this check can read in full. Anything else - color-mix(),
 * oklch(), var(), light-dark() - is rejected rather than partially read:
 * color-mix(in srgb, #0b1522 80%, white) contains a hex, so a plain
 * "find the colours" scan would happily measure the wrong colour and pass.
 */
const READABLE_FUNCS = new Set([
	'rgb',
	'rgba',
	'hsl',
	'hsla',
	'linear-gradient',
	'radial-gradient',
	'conic-gradient',
	'repeating-linear-gradient',
	'repeating-radial-gradient',
	'repeating-conic-gradient'
]);

/** The first function in a value this check cannot read, if any. */
function unreadableFunc(value) {
	for (const [, name] of value.matchAll(/([a-z][a-z0-9-]*)\s*\(/gi)) {
		if (!READABLE_FUNCS.has(name.toLowerCase())) return name;
	}
	return null;
}

/** All colours appearing in a value, in order (gradient stops included). */
function colorsIn(value) {
	const out = [];
	const re = /(#[0-9a-fA-F]{3,8}\b|rgba?\([^)]*\)|hsla?\([^)]*\))/g;
	for (const [, c] of value.matchAll(re)) {
		const parsed = parseColor(c);
		if (parsed) out.push(parsed);
	}
	return out;
}

function channel(c8) {
	const c = c8 / 255;
	return c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
}

function luminance({ r, g, b }) {
	return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b);
}

/** Composite a (possibly translucent) foreground over an opaque base. */
function over(fg, base) {
	const a = fg.a;
	return {
		r: fg.r * a + base.r * (1 - a),
		g: fg.g * a + base.g * (1 - a),
		b: fg.b * a + base.b * (1 - a),
		a: 1
	};
}

function contrast(fg, bg) {
	const l1 = luminance(fg);
	const l2 = luminance(bg);
	const [hi, lo] = l1 >= l2 ? [l1, l2] : [l2, l1];
	return (hi + 0.05) / (lo + 0.05);
}

// --- theme parsing -------------------------------------------------------

function stripComments(css) {
	return css.replace(/\/\*[\s\S]*?\*\//g, '');
}

function parseTheme(file) {
	const css = stripComments(readFileSync(file, 'utf8'));
	const m = css.match(
		/^\s*:root\[data-theme='([a-z0-9-]+)'\]\s*\{([^]*)\}\s*$/
	);
	if (!m) {
		fail(
			`${relative(ROOT, file)}: must be exactly one rule of the form` +
				` :root[data-theme='<theme-id>'] { … } with no other selectors or at-rules`
		);
		return null;
	}
	const id = m[1];
	const body = m[2];
	if (/[{}]/.test(body)) {
		fail(`${relative(ROOT, file)}: nested braces are not allowed in a theme`);
		return null;
	}
	const tokens = {};
	for (const decl of body.split(';')) {
		const d = decl.trim();
		if (!d) continue;
		const dm = d.match(/^(--[a-z0-9-]+)\s*:\s*([\s\S]+)$/);
		if (!dm || !dm[1].startsWith('--')) {
			fail(
				`${relative(ROOT, file)}: "${d.slice(0, 60)}" is not a custom-property declaration -` +
					` a theme may declare tokens only`
			);
			continue;
		}
		tokens[dm[1]] = dm[2].trim();
	}
	return { id, tokens };
}

// --- checks --------------------------------------------------------------

const themeFiles = readdirSync(THEMES_DIR)
	.filter((f) => f.endsWith('.css'))
	.sort();

if (themeFiles.length < 2) {
	fail(`expected the shipped themes in ${relative(ROOT, THEMES_DIR)}, found ${themeFiles.length}`);
}

for (const name of themeFiles) {
	const file = join(THEMES_DIR, name);
	const label = relative(ROOT, file);
	const theme = parseTheme(file);
	if (!theme) continue;

	if (theme.id !== name.slice(0, -'.css'.length)) {
		fail(`${label}: selector id '${theme.id}' does not match its file name`);
	}

	// Completeness: every contract token declared, and nothing beyond it -
	// a token no component consumes is dead weight every theme must carry.
	const missing = CONTRACT.filter((t) => !(t in theme.tokens));
	if (missing.length > 0) {
		fail(`${label}: missing contract token(s): ${missing.join(', ')}`);
	}
	const extra = Object.keys(theme.tokens).filter((t) => !CONTRACT.includes(t));
	if (extra.length > 0) {
		fail(
			`${label}: token(s) outside the contract: ${extra.join(', ')} -` +
				` add them to CONTRACT.md (and to every theme) or drop them`
		);
	}

	// Contrast: resolve the surfaces the app actually renders, then measure.
	// Counted from here so a completeness failure is never reported as a
	// contrast failure.
	const failuresBeforeContrast = failures;
	const declared = theme.tokens;

	// Per theme, not global: another theme's failure must not label this one,
	// and each check is reported under its own name.
	const report = () =>
		console.log(
			`theme: ${label} purity OK` +
				`, completeness ${CONTRACT.length - missing.length}/${CONTRACT.length}` +
				(extra.length > 0 ? ` +${extra.length} outside the contract` : '') +
				`, contrast ${failures > failuresBeforeContrast ? 'FAILED' : 'OK'}`
		);

	/**
	 * Every colour in a token's value, in order - each gradient stop, or the
	 * single colour. A value this check cannot read is a FAILURE, never a
	 * silently skipped pair: a skipped pair reports "contrast OK", which is
	 * how a 1:1 gradient button and an hsl() panel both passed once.
	 * Memoised so an unreadable token is reported once, not per pair.
	 */
	const colorCache = new Map();
	function colorsOf(token) {
		if (colorCache.has(token)) return colorCache.get(token);
		const spec = declared[token];
		let colors = null;
		if (spec === undefined) {
			colors = null; // completeness already reported this one
		} else {
			const bad = unreadableFunc(spec);
			const found = bad ? [] : colorsIn(spec);
			if (bad) {
				fail(
					`${label}: ${token}: "${spec.slice(0, 48)}" uses ${bad}(), which this check cannot` +
						` read - use hex, rgb()/rgba(), hsl()/hsla() or a gradient of them` +
						` (a partially read value would be measured as the wrong colour)`
				);
			} else if (found.length === 0) {
				fail(
					`${label}: ${token}: "${spec.slice(0, 48)}" holds no colour this check can read -` +
						` use hex, rgb()/rgba() or hsl()/hsla() (an unreadable value cannot be measured)`
				);
			} else {
				colors = found;
			}
		}
		colorCache.set(token, colors);
		return colors;
	}

	// Page background: --bg-image layered over the opaque --bg.
	const bgColors = colorsOf('--bg');
	if (!bgColors) {
		report();
		continue;
	}
	const bgBase = bgColors[0];
	if (bgBase.a < 1) {
		fail(`${label}: --bg must be opaque - it is the surface everything else composites over`);
		report();
		continue;
	}
	const bgImage = declared['--bg-image']?.trim();
	const pageBg =
		bgImage && bgImage !== 'none'
			? (colorsOf('--bg-image') ?? [bgBase]).map((c) => (c.a < 1 ? over(c, bgBase) : c))
			: [bgBase];

	// A surface token resolved to opaque candidates: every gradient stop,
	// each composited over every page-background candidate when translucent.
	function surfaces(token) {
		const colors = colorsOf(token);
		if (!colors) return [];
		return colors.flatMap((c) => (c.a < 1 ? pageBg.map((p) => over(c, p)) : [c]));
	}

	const panel = surfaces('--panel');
	const panel2 = surfaces('--panel-2');
	// Callouts appear in the main panel and in dashboard sections alike, so
	// a tint is measured over both and judged by the worse of them.
	const panels = [...panel, ...panel2];

	// Foreground over a set of backgrounds: least favourable combination.
	function minRatio(fgToken, bgs) {
		const fgs = colorsOf(fgToken);
		if (!fgs) return null;
		let worst = Infinity;
		for (const fgc of fgs) {
			for (const bg of bgs) {
				const fg = fgc.a < 1 ? over(fgc, bg) : fgc;
				worst = Math.min(worst, contrast(fg, bg));
			}
		}
		return worst;
	}

	// Refusal/warning/info callouts: text on a status tint over the section
	// surface (the nested-translucency case).
	function tintOver(tintToken, under) {
		const tints = colorsOf(tintToken);
		if (!tints) return [];
		return tints.flatMap((t) => under.map((u) => (t.a < 1 ? over(t, u) : t)));
	}

	const pairs = [
		['--text', 'on --panel', panel, BODY_RATIO],
		['--text', 'on --panel-2', panel2, BODY_RATIO],
		['--text', 'on page background', pageBg, BODY_RATIO],
		['--muted', 'on --panel', panel, BODY_RATIO],
		['--muted', 'on --panel-2', panel2, BODY_RATIO],
		['--ok', 'as text on --panel', panel, BODY_RATIO],
		['--warn', 'as text on --panel', panel, BODY_RATIO],
		['--danger', 'as text on --panel', panel, BODY_RATIO],
		['--info', 'as text on --panel', panel, BODY_RATIO],
		['--unknown', 'on --panel', panel, BODY_RATIO],
		['--not-applicable', 'on --panel', panel, BODY_RATIO],
		['--assumed', 'on --panel', panel, BODY_RATIO],
		['--text-on-accent', 'on --accent', surfaces('--accent'), BODY_RATIO],
		['--text-on-accent', 'on --accent-gradient (worst stop)', surfaces('--accent-gradient'), BODY_RATIO],
		['--text', 'on warn tint callout', tintOver('--warn-tint', panels), BODY_RATIO],
		['--danger', 'on danger tint callout', tintOver('--danger-tint', panels), BODY_RATIO],
		['--text', 'on info tint callout', tintOver('--info-tint', panels), BODY_RATIO],
		['--border', 'on --panel', panel, EDGE_RATIO],
		['--border', 'on --panel-2', panel2, EDGE_RATIO],
		['--border', 'on page background', pageBg, EDGE_RATIO],
		// --accent-strong is the focus ring (CONTRACT.md): a non-text edge,
		// so it takes the 3:1 UI-component threshold against what it sits on.
		['--accent-strong', 'as a focus ring on --panel', panel, EDGE_RATIO],
		['--accent-strong', 'as a focus ring on --panel-2', panel2, EDGE_RATIO],
		['--accent-strong', 'as a focus ring on page background', pageBg, EDGE_RATIO]
	];

	for (const [fgToken, where, bgs, min] of pairs) {
		if (!bgs || bgs.length === 0) continue; // its token already failed above
		const ratio = minRatio(fgToken, bgs);
		if (ratio === null) continue; // completeness already reported
		if (ratio < min) {
			fail(
				`${label}: ${fgToken} ${where} measures ${ratio.toFixed(2)}:1 (needs ${min}:1),` +
					` composited at the worst gradient stop`
			);
		}
	}

	report();
}

// The documented contract cannot drift from the enforced one.
const contractDoc = readFileSync(join(THEMES_DIR, 'CONTRACT.md'), 'utf8');
const undocumented = CONTRACT.filter((t) => !contractDoc.includes(`\`${t}\``));
if (undocumented.length > 0) {
	fail(`themes/CONTRACT.md does not document: ${undocumented.join(', ')}`);
}

if (failures > 0) {
	console.error('\ntheme check FAILED');
	process.exit(1);
}
console.log('theme check OK');
