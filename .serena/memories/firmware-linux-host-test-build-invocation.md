ESP-IDF is not on PATH by default in this environment. To build/run the Linux host
test_runner (firmware/test_runner):

```
source /Users/thorstenropertz/.espressif/v6.0.2/esp-idf/export.sh
cd firmware/test_runner
idf.py -B build_linux -D IDF_TARGET=linux build
./build_linux/sdf_test_runner.elf
```

`export.sh` must be sourced (adds idf.py, sets IDF_PATH, python venv) before any
`idf.py` invocation; there is no system-wide idf.py. Build reuses build_linux/ if
already configured. Full suite as of nuki-pairing-setup-flow change: 206 tests,
0 failures, 11 ignored (pre-existing HIL-only skips unrelated to this change).