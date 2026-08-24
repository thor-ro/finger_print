# CMake two-pass component-requirements-expansion produces a spurious "Reusing existing signing key" line

**Finding**: During `idf.py build`/`idf.py reconfigure` for `firmware/` (esp32c6 target), the
`firmware/components/sdf_ota/CMakeLists.txt` message "Reusing existing signing key at ..." (or
"Generated a new signing key...") is printed **twice**. One instance correctly names
`CMAKE_SOURCE_DIR/../ota_signing_key.pem`. The other resolves to a bare `.../firmware/build.`
(no filename) — this looks alarming (like the key path resolved wrong) but is harmless.

**Cause**: ESP-IDF's build runs component `CMakeLists.txt` files through an internal two-pass
component-requirements-expansion mechanism: an early pass collects `REQUIRES`/`PRIV_REQUIRES`
with an incomplete/empty Kconfig context (so `CONFIG_SECURE_BOOT_SIGNING_KEY` is unset/empty
during that pass), then a real registration pass runs later with full Kconfig populated. During
the early pass, `get_filename_component(OTA_SIGNING_KEY "" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")`
resolves the empty string to `CMAKE_SOURCE_DIR` itself, i.e. `firmware/build` — which is always
`EXISTS`-true by the time this runs (CMake auto-creates the build dir first), so the early pass
always harmlessly takes the "reuse" branch. No key is ever written to the wrong path; only the
real (second) pass, with populated Kconfig, actually creates/reuses the real signing key file.

**Verification**: Confirmed inert from a fully clean checkout (deleted `firmware/build` entirely,
reran `idf.py reconfigure`, same harmless duplicate "Reusing" outcome, correct key file present
afterward, no fatal error). Confirmed via
`grep -rn "Reusing existing signing key" ~/.espressif/v6.0.2/esp-idf/components/esptool_py/ .../bootloader/`
that ESP-IDF's own source never emits this string — both log lines originate from
`sdf_ota/CMakeLists.txt`'s own `message()` calls, just executed twice by ESP-IDF's build
machinery.

**Impact on future work**: Do not treat the `.../firmware/build.` (no filename) variant of this
log line as a bug or as evidence the signing key path is misconfigured — it is a cosmetic
artifact of ESP-IDF's two-pass CMake requirements expansion, not a real key-path resolution
problem. Only the line naming the actual `ota_signing_key.pem` path is meaningful.
