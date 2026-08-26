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
 *                  text, 3:1 for borders. Translucent surfaces are
 *                  COMPOSITED over the background beneath them (chained:
 *                  tint -> panel -> page background), and gradients are
 *                  measured at their least favourable stop - never an
 *                  average.
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
	'--ok-tint',
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

let failed = false;
function fail(message) {
	failed = true;
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
		let a = 1;
		if (m[4] !== undefined) {
			a = m[4].endsWith('%') ? parseFloat(m[4]) / 100 : parseFloat(m[4]);
		}
		return { r: +m[1], g: +m[2], b: +m[3], a };
	}
	return null;
}

/** All colours appearing in a value, in order (gradient stops included). */
function colorsIn(value) {
	const out = [];
	const re = /(#[0-9a-fA-F]{3,8}\b|rgba?\([^)]*\))/g;
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

	// Completeness: every contract token declared.
	const missing = CONTRACT.filter((t) => !(t in theme.tokens));
	if (missing.length > 0) {
		fail(`${label}: missing contract token(s): ${missing.join(', ')}`);
	}

	// Contrast: resolve the surfaces the app actually renders, then measure.
	const declared = theme.tokens;
	const has = (t) => t in declared && parseColor(declared[t]);
	if (!has('--bg')) continue; // completeness already reported

	function resolveStops(spec, fallbackSolid) {
		// Returns opaque candidate colours for a token value: each gradient
		// stop, else the colour itself. Translucency is resolved by the
		// caller, which knows what lies beneath.
		const colors = colorsIn(spec);
		if (colors.length > 0) return colors;
		return [fallbackSolid];
	}

	// Page background: --bg-image layered over the opaque --bg.
	const bgBase = parseColor(declared['--bg']);
	const pageBg =
		declared['--bg-image'] && declared['--bg-image'] !== 'none'
			? resolveStops(declared['--bg-image'], bgBase).map((c) => (c.a < 1 ? over(c, bgBase) : c))
			: [bgBase];

	// A translucent surface composited over every page-background candidate.
	function surfaces(token) {
		if (!has(token)) return [];
		return resolveStops(declared[token], null).map((c) =>
			c.a < 1 ? pageBg.map((p) => over(c, p)) : [c]
		).flat();
	}

	const panel = surfaces('--panel');
	const panel2 = surfaces('--panel-2');

	// Foreground over a set of backgrounds: least favourable combination.
	function minRatio(fgToken, bgs) {
		if (!has(fgToken)) return null;
		let worst = Infinity;
		for (const fgc of resolveStops(declared[fgToken], null)) {
			for (const bg of bgs) {
				const fg = fgc.a < 1 ? over(fgc, bg) : fgc;
				worst = Math.min(worst, contrast(fg, bg));
			}
		}
		return worst;
	}

	// Refusal/warning callouts: status text on its tint over the section
	// surface (the nested-translucency case).
	function tintedText(textToken, tintToken, under) {
		if (!has(tintToken)) return [];
		return colorsIn(declared[tintToken]).flatMap((t) =>
			under.map((u) => over(t, u))
		);
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
		['--text-on-accent', 'on --accent-strong', surfaces('--accent-strong'), BODY_RATIO],
		['--text-on-accent', 'on --accent-gradient', surfaces('--accent-gradient'), BODY_RATIO],
		['--text', 'on warn tint callout', tintedText(null, '--warn-tint', panel2), BODY_RATIO],
		['--danger', 'on danger tint callout', tintedText(null, '--danger-tint', panel2), BODY_RATIO],
		['--border', 'on --panel', panel, EDGE_RATIO],
		['--border', 'on --panel-2', panel2, EDGE_RATIO],
		['--border', 'on page background', pageBg, EDGE_RATIO]
	];

	for (const [fgToken, where, bgs, min] of pairs) {
		if (!bgs || bgs.length === 0) continue;
		const ratio = minRatio(fgToken, bgs);
		if (ratio === null) continue; // completeness already reported
		if (ratio < min) {
			fail(
				`${label}: ${fgToken} ${where} measures ${ratio.toFixed(2)}:1 (needs ${min}:1),` +
					` composited at the worst gradient stop`
			);
		}
	}

	console.log(
		`theme: ${label} purity OK, completeness ${CONTRACT.length - missing.length}/${CONTRACT.length}` +
			`, contrast ${failed ? 'FAILED' : 'OK'}`
	);
}

// The documented contract cannot drift from the enforced one.
const contractDoc = readFileSync(join(THEMES_DIR, 'CONTRACT.md'), 'utf8');
const undocumented = CONTRACT.filter((t) => !contractDoc.includes(`\`${t}\``));
if (undocumented.length > 0) {
	fail(`themes/CONTRACT.md does not document: ${undocumented.join(', ')}`);
}

if (failed) {
	console.error('\ntheme check FAILED');
	process.exit(1);
}
console.log('theme check OK');
