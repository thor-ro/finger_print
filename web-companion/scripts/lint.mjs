#!/usr/bin/env node
/**
 * Lint gate for the web companion.
 *
 * 1. Bans raw-HTML rendering (`{@html ...}`) anywhere under src/. Svelte's
 *    text interpolation escapes by default; this app has no reason to opt
 *    out, and doing so is how the legacy user-list XSS happened.
 * 2. Enforces the protocol-layer purity constraint: nothing under
 *    src/lib/protocol/ may import or reference the DOM, `navigator`,
 *    `window`, or SvelteKit internals.
 * 3. Confines Web Bluetooth to the single production transport:
 *    `navigator.bluetooth` may only appear in src/lib/transport/ble.ts.
 * 4. Bans colour literals anywhere under src/ - hex, rgb()/hsl() and named
 *    CSS colours alike. All colours come from theme tokens declared in
 *    themes/*.css (see themes/CONTRACT.md); a literal here is how a theme
 *    stops being able to restyle the app.
 *
 * Exits non-zero on any violation.
 */
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { join, relative, sep } from 'node:path';
import process from 'node:process';

const ROOT = new URL('..', import.meta.url).pathname;
const SRC = join(ROOT, 'src');

function walk(dir) {
	const entries = [];
	for (const name of readdirSync(dir)) {
		if (name === 'node_modules' || name === '.svelte-kit') continue;
		const full = join(dir, name);
		if (statSync(full).isDirectory()) entries.push(...walk(full));
		else if (/\.(svelte|ts|js|css)$/.test(name)) entries.push(full);
	}
	return entries;
}

const files = walk(SRC);
let failed = false;

function fail(file, line, message) {
	failed = true;
	console.error(`lint error  ${relative(ROOT, file)}:${line}  ${message}`);
}

// 1. {@html} is banned everywhere under src/.
for (const file of files) {
	const lines = readFileSync(file, 'utf8').split('\n');
	lines.forEach((line, i) => {
		if (/{@html\b/.test(line)) fail(file, i + 1, '{@html} is banned under src/ - render values as escaped text instead');
	});
}

// 2. lib/protocol/ stays pure: no DOM, no navigator, no $app/*.
const PROTOCOL = join(SRC, 'lib', 'protocol');
const FORBIDDEN_PROTOCOL = [
	{ re: /\bdocument\./, why: 'DOM access' },
	{ re: /\bnavigator\./, why: 'navigator access' },
	{ re: /\bwindow\./, why: 'window access' },
	{ re: /from\s+['"]\$app\//, why: '$app/* import' },
	{ re: /from\s+['"][^'"]*\.svelte(\.ts)?['"]/, why: 'component/store import' },
	{ re: /from\s+['"][^'"]*\/transport\//, why: 'transport import' }
];
for (const file of files.filter((f) => f.startsWith(PROTOCOL))) {
	const lines = readFileSync(file, 'utf8').split('\n');
	lines.forEach((line, i) => {
		for (const { re, why } of FORBIDDEN_PROTOCOL) {
			if (re.test(line)) fail(file, i + 1, `protocol layer must stay DOM-free: ${why}`);
		}
	});
}

// 3. navigator.bluetooth only in the one production transport.
const ONLY_BLE = join(SRC, 'lib', 'transport', 'ble.ts');
for (const file of files.filter((f) => !f.endsWith('.test.ts'))) {
	if (file === ONLY_BLE) continue;
	const lines = readFileSync(file, 'utf8').split('\n');
	lines.forEach((line, i) => {
		if (/navigator\.bluetooth|requestDevice/.test(line)) {
			fail(file, i + 1, 'Web Bluetooth may only be used in src/lib/transport/ble.ts');
		}
	});
}

// 4. No colour literals under src/ - themes own every colour value.
//    Named CSS colours count: `color: red` is as unthemeable as `#f00`.
//    They are matched only as the value of a colour-bearing property, so
//    ordinary prose and identifiers ("required", "silver lining") are safe.
const CSS_NAMED_COLOURS =
	'aliceblue antiquewhite aqua aquamarine azure beige bisque black blanchedalmond blue blueviolet brown burlywood cadetblue chartreuse chocolate coral cornflowerblue cornsilk crimson cyan darkblue darkcyan darkgoldenrod darkgray darkgreen darkgrey darkkhaki darkmagenta darkolivegreen darkorange darkorchid darkred darksalmon darkseagreen darkslateblue darkslategray darkslategrey darkturquoise darkviolet deeppink deepskyblue dimgray dimgrey dodgerblue firebrick floralwhite forestgreen fuchsia gainsboro ghostwhite gold goldenrod gray green greenyellow grey honeydew hotpink indianred indigo ivory khaki lavender lavenderblush lawngreen lemonchiffon lightblue lightcoral lightcyan lightgoldenrodyellow lightgray lightgreen lightgrey lightpink lightsalmon lightseagreen lightskyblue lightslategray lightslategrey lightsteelblue lightyellow lime limegreen linen magenta maroon mediumaquamarine mediumblue mediumorchid mediumpurple mediumseagreen mediumslateblue mediumspringgreen mediumturquoise mediumvioletred midnightblue mintcream mistyrose moccasin navajowhite navy oldlace olive olivedrab orange orangered orchid palegoldenrod palegreen paleturquoise palevioletred papayawhip peachpuff peru pink plum powderblue purple rebeccapurple red rosybrown royalblue saddlebrown salmon sandybrown seagreen seashell sienna silver skyblue slateblue slategray slategrey snow springgreen steelblue tan teal thistle tomato turquoise violet wheat white whitesmoke yellow yellowgreen';
const COLOUR_PROPERTY =
	'color|background|background-color|border|border-color|border-(?:top|right|bottom|left)-color|outline|outline-color|fill|stroke|box-shadow|text-shadow|text-decoration-color|caret-color|accent-color|column-rule-color';
const NAMED_COLOUR_VALUE = new RegExp(
	`(?:^|[;{'"\\s])(?:${COLOUR_PROPERTY})\\s*:\\s*[^;{}]*?\\b(${CSS_NAMED_COLOURS.split(' ').join('|')})\\b`,
	'i'
);
const COLOUR_LITERAL = [
	{ re: /#[0-9a-fA-F]{3,8}\b/, why: 'hex colour literal' },
	{ re: /\b(?:rgba?|hsla?)\s*\(/i, why: 'rgb()/hsl() colour literal' },
	{ re: NAMED_COLOUR_VALUE, why: 'named CSS colour' }
];
for (const file of files) {
	const lines = readFileSync(file, 'utf8').split('\n');
	lines.forEach((line, i) => {
		for (const { re, why } of COLOUR_LITERAL) {
			if (re.test(line)) {
				fail(file, i + 1, `colour literal banned under src/ (${why}) - use a theme token (themes/CONTRACT.md)`);
			}
		}
	});
}

if (failed) {
	console.error(`\nlint FAILED`);
	process.exit(1);
}
console.log(`lint OK  (${files.length} files scanned)`);
