## 1. Remove emit_async

- [x] 1.1 Remove `sdf_event_router_emit_async()` declaration from `firmware/components/sdf_event_router/include/sdf_event_router.h`
- [x] 1.2 Remove `sdf_event_router_emit_async()` definition from `firmware/components/sdf_event_router/src/sdf_event_router.c`
- [x] 1.3 Grep repo-wide for `emit_async` to confirm no remaining references (production, test, or docs)

## 2. Close subscribe() bounds-check gap

- [x] 2.1 Add `type >= SDF_EVENT_ROUTER_TYPE_COUNT` to the argument guard in `sdf_event_router_subscribe()`, returning `ESP_ERR_INVALID_ARG`
- [x] 2.2 Add `test_sdf_event_router_subscribe_rejects_invalid_type()` to `firmware/components/sdf_event_router/test/test_sdf_event_router.c`, asserting `ESP_ERR_INVALID_ARG` for `type == SDF_EVENT_ROUTER_TYPE_COUNT`
- [x] 2.3 Wire the new test into `firmware/test_runner/main/test_runner_main.c` (`extern` declaration + `RUN_TEST()`)

## 3. Verify

- [x] 3.1 Build `test_runner` for `IDF_TARGET=linux` and run `sdf_test_runner.elf`; confirm all suites pass including the new subscribe test
- [x] 3.2 Build the main firmware target to confirm removing `emit_async()` doesn't break any target-specific code path not caught by the host test runner
