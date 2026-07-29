## Context

The system currently uses a fingerprint UART baud rate of 19200 (`CONFIG_SDF_FP_BAUD_RATE`), which introduces latency during fingerprint transactions. Slower transactions mean the CPU remains active for longer periods before returning to light or deep sleep, thereby increasing average power consumption. Additionally, when awake, the match task polls the sensor every 400ms (`CONFIG_SDF_MATCH_POLL_INTERVAL_MS`), continuously consuming power even when no interaction is occurring.

## Goals / Non-Goals

**Goals:**
- Increase UART baud rate for the fingerprint sensor to 115200 to reduce transaction time and keep CPU active time to a minimum.
- Modify the polling dynamics of `sdf_match_task` to reduce unnecessary polling when the system is awake, relying on the GPIO WAKE interrupt.

**Non-Goals:**
- Completely rewriting the `sdf_services` event router architecture.
- Changing the sleep modes (deep sleep, light sleep) definitions beyond optimizing the time spent in the active state.

## Decisions

- **UART Baud Rate**: Increase `CONFIG_SDF_FP_BAUD_RATE` from 19200 to 115200 in `sdkconfig.defaults`. The fingerprint sensor supports this higher rate, which will directly reduce the active CPU duty cycle.
- **Match Task Polling**: `sdf_match_task` currently uses a continuous 400ms poll loop when not suspended. We will change this to be interrupt-driven by the sensor's WAKE pin.
    - *Alternative considered*: Increasing the polling interval (e.g., to 1000ms). *Rejected* because it introduces unacceptable user-facing latency. Using the interrupt ensures immediate response while allowing the task to block on an event/queue indefinitely when idle.

## Risks / Trade-offs

- [Baud Rate Stability] → The physical connection to the sensor must be reliable enough for 115200 baud without data corruption. Mitigation: testing with the actual hardware.
- [Interrupt Reliability] → The WAKE pin might bounce or trigger spuriously. Mitigation: ensure proper debouncing or edge-trigger handling in the GPIO ISR before waking the match task.
