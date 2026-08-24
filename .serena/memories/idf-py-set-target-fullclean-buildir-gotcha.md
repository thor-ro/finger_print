# idf.py set-target / fullclean build-directory gotcha

When scripting out-of-tree ESP-IDF builds (`idf.py -C <src> -B <build_dir> ... set-target esp32c6`),
`set-target` runs a `fullclean` dependency first. `fullclean` refuses to touch `<build_dir>` if it
contains *any* file it doesn't recognize as CMake-generated output ("doesn't seem to be a CMake
build directory. Refusing to automatically delete files in this directory."), exiting non-zero.

**Cause, confirmed empirically**: piping that first `set-target` invocation into
`tee "<build_dir>/set-target.log"` is enough to trigger this, even against a *freshly created,
otherwise-empty* `<build_dir>`. `tee` opens/creates its output file essentially at pipeline start,
before idf.py's own cmake process has produced any recognized marker (e.g. `CMakeCache.txt`) — so
the log file itself is the "foreign" file that trips the refusal. A bare `idf.py ... set-target`
with no `tee` in the pipe succeeds fine against the same pre-existing empty directory; the failure
is specific to teeing *into* the build directory on the *first* idf.py invocation against it.

**Fix**: never tee `set-target`'s own output into `<build_dir>`; write that one log to a sibling
path instead (e.g. `<build_dir>.set-target.log`, next to the directory rather than inside it).
Every subsequent step (`build`, `merge-bin`, boot logs, ...) is safe to tee straight into
`<build_dir>`, since by then `set-target`'s cmake configure has already populated it with
`CMakeCache.txt` and the fullclean safety check no longer re-triggers.

Whether `<build_dir>` is pre-created via `mkdir -p` beforehand or left for idf.py to create is *not*
the deciding factor by itself — confirmed both a pre-`mkdir -p`'d empty directory and a nonexistent
path work fine for a plain (non-teed) `set-target` call. The deciding factor is only whether a
non-CMake file (like a teed log) lands inside the directory before/during that first invocation.

See `mem:esp-emu-panics-are-real` for a related esp-emu fidelity note; this one is purely an
`idf.py`/CMake build-directory quirk, unrelated to the emulator itself.

Fixed in `scripts/run_ota_signature_gate.sh`'s `build_fixture()` (add-ble-ota-emulator-harness
change) by routing the `set-target` log to `"${build_dir}.set-target.log"`.
