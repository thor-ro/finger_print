# Hardware Parity Suite

Manual-on-hardware test suite for the web-companion rebuild
(`openspec/changes/web-companion-tooling` tasks 6.1–6.8). These flows cannot
be automated: Web Bluetooth has no automatable device picker, and what they
exercise — fingerprint scans, the setup window, an OTA that reboots the
device — is physical. Everything that *can* be automated already is (58
headless tests via `npm test`); this suite covers the rest.

## Running

You need:

- The device, factory-reset or claimed as each case requires, running
  firmware at or above the OTA floor noted in `../README.md`.
- The deployed companion (`https://thor-ro.github.io/finger_print/`) or a
  local build (`BASE_PATH=/finger_print npm run build && npm run preview`).

```bash
npm run hw            # interactive: walks each case, records verdicts
npm run hw -- --list  # print the cases without running
npm run hw -- --verify  # gate: fails unless every case is recorded PASS
npm run hw -- --reset   # discard verdicts and start over
```

The runner asks for tester name, device firmware version and app URL once,
then presents each case (preconditions, steps, expected outcomes) and
records `PASS` / `FAIL` / `SKIPPED` plus optional notes into
`results.json`. Completed cases are skipped on re-runs, so the suite can be
spread across sessions.

## Gate

`npm run hw -- --verify` exits non-zero while any case lacks a PASS verdict.
Once all pass it renders `parity-results.md` — the parity record required by
task 6.9 before (and, after the early deployment switch, also after) the
legacy assets were removed.

## Cases

| ID | Task | Title |
|---|---|---|
| HW-6.1 | 6.1 | Full first-time setup on a wiped device |
| HW-6.2 | 6.2 | Wizard resume: reconnect within the setup window |
| HW-6.3 | 6.3 | Setup lapse: window elapses mid-wizard |
| HW-6.4 | 6.4 | Login on a claimed device, rejected login reveals nothing |
| HW-6.5a | 6.5 | UM: enrol verb with a real admin scan |
| HW-6.5b | 6.5 | UM: delete verb with a real admin scan |
| HW-6.5c | 6.5 | UM: permission change with a real admin scan |
| HW-6.5d | 6.5 | UM: rename with a real admin scan |
| HW-6.5e | 6.5 | UM: denied scan vs timeout rendered differently |
| HW-6.6 | 6.6 | Self-affecting change warnings do not block the change |
| HW-6.7 | 6.7 | Health view updates from notifications |
| HW-6.8 | 6.8 | OTA transfer to completion, with mid-transfer disconnect resume |
