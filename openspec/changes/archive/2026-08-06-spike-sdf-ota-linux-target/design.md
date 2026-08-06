## Context

`sdf_ota`'s `CMakeLists.txt` unconditionally `REQUIRES mbedtls app_update sdf_common` and `PRIV_REQUIRES sdf_config`, with three source files: `sdf_ota.c`, `sdf_ota_version.c`, `sdf_ota_signature.c`. Static analysis (no ESP-IDF toolchain was available when this design was first drafted) turned up enough evidence to substantially narrow — though not fully close — the proposal's open question. That analysis has since been confirmed against a real `idf.py build` (ESP-IDF v6.0.2, found installed at `~/.espressif/v6.0.2/esp-idf` in this environment) — see "Confirmed Outcome" below:

- **`esp_partition` already builds and is tested for `IDF_TARGET=linux` today**, via `sdf_storage` (`PRIV_REQUIRES nvs_flash esp_partition`, calls `esp_partition_find_first`/`esp_partition_erase_range`) and `sdf_platform` (`PRIV_REQUIRES sdf_config esp_partition`, calls `esp_partition_find_first`), both already wired into `test_runner` and passing. So the low-level partition API is not the risk here.
- **`sdf_ota.c` has a hard, unconditional link-time dependency on `sdf_app_emit_audit()`** (`extern void sdf_app_emit_audit(...)`, called at lines 44 and 376), a symbol defined inside `sdf_app` — the same component `add-linux-target-sdf-app-support`'s investigation confirmed is hardware/stack-bound and will not be part of `test_runner`'s link graph. This means `sdf_ota.c` cannot link on `linux` regardless of whether `app_update` itself builds cleanly — it needs either `sdf_app` linked in (impossible per that change's own scoping) or a linux stub for this one symbol. This is a firmer blocker than the `app_update`/`esp_partition` question the proposal's Why section led with.
- **`sdf_ota_version.c` has zero hardware or `sdf_app` dependencies** — pure semver string parsing (`parse_semver`, `sdf_ota_version_compare`), only `<string.h>`/`<stdlib.h>`/`<ctype.h>`/`esp_log.h`. Trivially linux-safe.
- **`sdf_ota_signature.c` is entirely gated by `CONFIG_SDF_OTA_SIGNATURE_VERIFY`** (default `n`, per `sdf_config/Kconfig`). The `#else` branch (what actually compiles by default) is a 6-line no-op stub with no `esp_partition`/`sdf_app`/`mbedtls` usage at all. The `#if` branch (only compiled when the option is explicitly turned on) uses `esp_partition` (proven linux-safe above) and `mbedtls/ed25519.h` (already linux-tested elsewhere in this repo, per the Nuki crypto suite) — but also `#include "sdf_app.h"`, and **`sdf_ota`'s `CMakeLists.txt` never lists `sdf_app` in `REQUIRES`/`PRIV_REQUIRES`**, so that header include's resolution is questionable even for the existing hardware build whenever `SDF_OTA_SIGNATURE_VERIFY=y`. This looks like a latent, pre-existing gap unrelated to linux-portability — out of scope to fix here, but worth flagging (see Open Questions) since it affects whether the `#if` branch is even currently exercised on hardware.

Net effect: the two files with real linux-testing value (`sdf_ota_version.c`, and `sdf_ota_signature.c` under its default-off Kconfig configuration) don't actually need `app_update` at all, and don't touch the symbol that blocks `sdf_ota.c`. The proposal's framing ("genuinely unknown whether `app_update` builds for `linux`") turns out to be closer to "irrelevant to this spike's realistic scope" — `sdf_ota.c`, the one file that needs `app_update`, is excluded from the linux-safe subset by the `sdf_app_emit_audit` blocker alone, independent of whatever `app_update` itself does.

## Confirmed Outcome

The `idf.py build` attempt (`IDF_TARGET=linux`, `firmware/test_runner`) **succeeded**, confirming the static analysis above — with one correction static analysis missed: the linux branch's initial `REQUIRES mbedtls sdf_common` failed with `fatal error: 'esp_partition.h' file not found`, because `sdf_ota.h` (shared by all three source files, including the linux-safe ones) unconditionally `#include`s `esp_partition.h` for the `sdf_ota_verify_signature()` prototype — a header-level dependency the static analysis of `sdf_ota.c`'s *body* didn't surface. Adding `esp_partition` to the linux branch's `REQUIRES` (already established above as linux-safe, via `sdf_storage`/`sdf_platform`) fixed it; the build is clean from there. The hardware (`esp32c6`) build was also re-run after the `CMakeLists.txt` split and produced the same `sdf_ota.c`/`sdf_ota_version.c`/`sdf_ota_signature.c` object set as before — the split is a no-op for that target.

The `sdf_ota_version.c` and `sdf_ota_signature.c` Unity suites (10 test cases total) were then wired into `test_runner` and run via `./build/sdf_test_runner.elf`: all 10 pass, alongside the rest of the suite (166 total tests, 0 failures, 11 pre-existing `IGNORE`s unrelated to this change).

## Goals / Non-Goals

**Goals:**
- Get `sdf_ota_version.c` and `sdf_ota_signature.c` (default Kconfig configuration) building and unit-tested under `IDF_TARGET=linux`, without requiring `app_update` or any `sdf_app` symbol.
- Leave `sdf_ota.c` and the `CONFIG_SDF_OTA_SIGNATURE_VERIFY=y` branch of `sdf_ota_signature.c` explicitly out of the linux build, with the reason (the `sdf_app_emit_audit` link dependency, and the `sdf_app.h`-include-without-`REQUIRES` issue) documented rather than silently worked around.
- Confirm the above with an actual `idf.py set-target linux && idf.py build` run in an environment with the ESP-IDF toolchain — this design narrows the risk via static analysis but does not substitute for actually building.

**Non-Goals:**
- Making `sdf_ota.c`'s partition-write/rollback logic linux-testable. That would require a `linux` stub for `sdf_app_emit_audit` (or a broader refactor moving audit-emission out of `sdf_ota.c`, mirroring `add-linux-target-sdf-app-support`'s pattern) plus resolving whatever `app_update` itself needs on `linux` — real scope, deliberately deferred to a follow-up per the proposal.
- Fixing the `sdf_app.h`-include-without-`REQUIRES sdf_app` issue in `sdf_ota_signature.c`'s `#if CONFIG_SDF_OTA_SIGNATURE_VERIFY` branch. Flagged as an Open Question, not fixed here — fixing it is unrelated to linux-testability and risks scope creep into a spike change.
- Testing the `CONFIG_SDF_OTA_SIGNATURE_VERIFY=y` code path at all in this change, given the unresolved include issue above. If a future change fixes that issue, testing the real Ed25519 verification path (rather than the no-op stub) becomes a natural follow-up.

## Decisions

**Split `sdf_ota`'s `CMakeLists.txt` by `IDF_TARGET`, rather than trying to make the whole component linux-buildable.**
```cmake
if(IDF_TARGET STREQUAL "linux")
    idf_component_register(SRCS "src/sdf_ota_version.c"
                                 "src/sdf_ota_signature.c"
                           INCLUDE_DIRS "include"
                           REQUIRES mbedtls sdf_common esp_partition)
else()
    idf_component_register(SRCS "src/sdf_ota.c"
                                 "src/sdf_ota_version.c"
                                 "src/sdf_ota_signature.c"
                                 ${SDF_VERSION_C}
                           INCLUDE_DIRS "include"
                           REQUIRES mbedtls app_update sdf_common
                           PRIV_REQUIRES sdf_config
                           EMBED_FILES ${OTA_PUBLIC_KEY_BIN})
endif()
```
This matches the convention every other hardware-coupled component in this repo already uses (`sdf_platform`, `sdf_drivers`, `sdf_power`, `sdf_protocol_ble`, `sdf_services`, `sdf_protocol_zigbee`). `app_update` and the key-signing custom targets (`sign_ota`, `ota_extract_pubkey`, the `openssl`/`sdf_sign_ota.py` key-generation logic) stay hardware-only — none of that is meaningful on a host build with no real flash image to sign.

**Correction from the actual build (see "Confirmed Outcome" above):** the linux branch's `REQUIRES` also needs `esp_partition`, which the snippet above already reflects. Static analysis of `sdf_ota.c`'s body missed this because the dependency comes from `sdf_ota.h` — shared by all three source files — unconditionally `#include`-ing `esp_partition.h` for the `sdf_ota_verify_signature()` prototype, not from anything `sdf_ota_version.c`/`sdf_ota_signature.c` themselves do. The key-generation `execute_process()` block and the `sign_ota`/`ota_extract_pubkey` custom targets are now guarded inside the `else()` branch too (see Risks below), rather than left unconditional.

Alternatives considered:
- **Try to make `app_update` resolve for linux too, so `sdf_ota.c` can be included** — rejected for this change. Even if `app_update` itself builds, `sdf_ota.c` still can't *link* without a stub for `sdf_app_emit_audit`, which is a separate, larger piece of work (and arguably belongs paired with `add-linux-target-sdf-app-support`'s pattern of extracting testable logic rather than mocking hardware-adjacent symbols). Better handled as its own follow-up with its own evidence, not folded into this spike.
- **Mock `esp_partition`/`app_update` outright with a `sdf_ota_mock_linux.c`, following `sdf_drivers`'/`sdf_protocol_zigbee`'s pattern, to cover `sdf_ota.c` too** — rejected for the same reason: `esp_partition` doesn't need mocking (it already works), and the actual blocker (`sdf_app_emit_audit`) isn't a partition-API problem a mock would address.

**`sdf_ota_signature.c` is included in the linux build as-is (both `#if`/`#else` branches present in source), relying on the default `CONFIG_SDF_OTA_SIGNATURE_VERIFY=n` to select the no-op branch — not split into a separate linux-only file.**
Keeps one source file instead of forking logic; the linux `test_runner`'s `sdkconfig.defaults` (or lack of an override) determines which branch compiles, same as it does for the hardware build. If `test_runner` doesn't already carry a `sdkconfig.defaults.linux`, confirm the default value truly is `n` for the linux build during implementation (task 1 below).

## Risks / Trade-offs

- [Static analysis says `app_update` "shouldn't matter" for this change's scope, but the actual `idf.py build` attempt could still surface an unrelated linux-target issue with `mbedtls/ed25519.h` or the `EMBED_FILES`/key-generation `execute_process()` calls at the top of `CMakeLists.txt` running unconditionally even in the linux branch] → **Confirmed by the actual build**: the real issue was neither `mbedtls/ed25519.h` (that builds fine) nor the key-generation block directly, but `sdf_ota.h`'s unconditional `#include "esp_partition.h"` — fixed by adding `esp_partition` to the linux branch's `REQUIRES` (see Decisions above). Separately, the `openssl genpkey`/`sdf_sign_ota.py extract-pubkey` key-generation block and the `sign_ota`/`ota_extract_pubkey` custom targets are now moved inside the hardware (`else()`) branch, since the linux build's `sdf_ota_signature.c` `#else` stub never reads the embedded key and `EMBED_FILES` isn't used there — no need to run `openssl`/`sdf_sign_ota.py` (or require them installed) for a linux build at all.
- [The `CONFIG_SDF_OTA_SIGNATURE_VERIFY=y` branch's `sdf_app.h` include may indicate the option has never actually been built/tested on hardware either] → Out of scope to fix (Non-Goals above), but worth a one-line note in this change's PR/commit so it isn't lost; not something this spike should silently paper over by, e.g., adding `sdf_app` to `sdf_ota`'s `REQUIRES` as a "fix" — that would pull a hardware-bound component into `sdf_ota`'s graph, defeating the point.
- [If the `idf.py build` attempt fails for a reason unrelated to the ones predicted here] → Proposal's own "if it doesn't build" path already covers this: capture the exact failing symbol/component rather than reverting silently. In practice the build succeeded (with the `esp_partition` fix above), so this path wasn't needed.

## Migration Plan

1. ✅ Add `sdf_ota` to `firmware/test_runner/CMakeLists.txt`'s `EXTRA_COMPONENT_DIRS`/`COMPONENTS`.
2. ✅ Apply the `IDF_TARGET`-conditional `CMakeLists.txt` split described in Decisions above.
3. ✅ Run `cd firmware/test_runner && idf.py set-target linux && idf.py build`. **Result: clean build**, after one fix — added `esp_partition` to the linux branch's `REQUIRES` (see "Confirmed Outcome" above for why).
4. **Build succeeded**, so: ✅ wrote `firmware/components/sdf_ota/test/test_sdf_ota_version.c` (9 cases: equal, leading-`v`, major/minor/patch ordering, release-vs-pre-release, pre-release alphanumeric ordering, build-metadata-ignored, malformed input) and `test_sdf_ota_signature.c` (1 case: default-Kconfig no-op path returns `ESP_OK` unconditionally). Wired into `test_runner/main/CMakeLists.txt` and `test_runner_main.c` — `sdf_ota` also had to be added to `main`'s own `test_reqs` for `sdf_ota.h`'s include dir to resolve. Extended `firmware-host-test-runner`'s "Wired-In Component Coverage" to add `sdf_ota`. All 10 new cases pass; full suite is 166 tests / 0 failures / 11 pre-existing ignores.
5. *(Not needed — build succeeded.)*
6. ✅ Confirmed the hardware build (`esp32c6`) is unaffected by the `CMakeLists.txt` split — `idf.py build` from `firmware/` succeeded and produced the same `sdf_ota.c`/`sdf_ota_version.c`/`sdf_ota_signature.c` object set as before the split.
7. Rollback is a plain revert of the `CMakeLists.txt` split and `test_runner` wiring — no production behavior or data changes either way.

## Open Questions

- **Still unresolved:** Does `sdf_ota_signature.c`'s `#if CONFIG_SDF_OTA_SIGNATURE_VERIFY` branch actually compile today on hardware, given `sdf_ota`'s `CMakeLists.txt` never `REQUIRES`/`PRIV_REQUIRES`s `sdf_app` but the branch `#include "sdf_app.h"`? This spike's step 1.5 hardware build ran with `firmware/sdkconfig.defaults`' actual value, `CONFIG_SDF_OTA_SIGNATURE_VERIFY=n` (despite `AGENTS.md`'s "OTA signature verification: Ed25519 (mandatory)" claim under Security Defaults — that line is itself stale/inaccurate and worth a separate doc-fix) — so the `#if` branch still wasn't compiled by anything this spike ran, on either target. Whether it compiles with `SDF_OTA_SIGNATURE_VERIFY=y` remains genuinely unconfirmed; worth a follow-up issue, not fixed in this spike.
- **Resolved:** the `openssl`/key-generation `execute_process()` block, and the `sign_ota`/`ota_extract_pubkey` custom targets, are now inside the hardware-only `else()` branch (see Decisions/Risks above) — confirmed via the actual `idf.py build` that the linux branch doesn't need them.
- If the build succeeds, does covering `sdf_ota.c`'s partition-write/rollback logic (via a `linux` stub for `sdf_app_emit_audit`, or extracting audit-emission the way `add-linux-target-sdf-app-support` extracts auth decisions) become a worthwhile follow-up? Not resolved here — flagged for whoever picks up hardware-only mocking work next, per the proposal's own framing.
