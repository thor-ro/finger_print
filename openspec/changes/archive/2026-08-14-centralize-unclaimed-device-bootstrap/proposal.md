## Why

On an unclaimed device (zero enrolled users) there is no admin who could authorize an admin action, so the system has a bootstrap bypass: the action executes immediately, without admin-fingerprint authorization. That bypass is the single most security-sensitive branch in the admin-action flow — it is the one place where an action runs with no authorization at all.

It currently lives in `sdf_button_dispatch_action()` (`sdf_services_button.c:143-164`), an *input-layer* function whose job is otherwise just translating a button gesture into an action. Three problems follow:

1. **Wrong layer.** "May this action run without authorization?" is an authorization decision. It is implemented in the button driver glue, not in the code that owns `pending_admin_action` and the admin-fingerprint gate. Nothing structurally prevents a second request path from being added without the reviewer noticing the bypass exists at all.
2. **Invisible coupling.** The other path that sets a pending action, `sdf_services_request_admin_action()` (`sdf_services.c:1051`), has no bootstrap branch. On an unclaimed device it sets a pending action that no fingerprint can ever authorize, so it silently times out. Whether that is the *intended* behavior for a remote request is a deliberate security property — but today it holds by accident of where the code was written, not by an expressed decision.
3. **Fragile under relocation.** Any change that moves button dispatch to a different execution site must carry this branch with it or silently lose the ability to claim a fresh device. `dispatch-admin-actions-off-esp-timer` is exactly such a change.

There is also a **documentation defect** to correct while doing this. The existing "Simplified Pre-Enrollment Bootstrap Branch" requirement reads as though `ENROLL` were the only action eligible for unauthenticated immediate execution. The implementation is broader: `ENROLL` takes a dedicated route into local enrollment, and *every other* button-reachable action falls to an `else` branch that invokes the admin-action execution callback immediately, also without authorization (`sdf_services_button.c:153-162`). On an unclaimed device that means a double-click really does open the BLE pairing window and an 8-second hold really does run a factory reset, with no authorization — which is defensible (there is nothing yet to protect, and opening the pairing window is how the device gets claimed), but it is not what the requirement says. The requirement does not say *who* may invoke the bypass either, and it names `sdf_button_task` as the owner.

## What Changes

- Extract the zero-enrolled-users bootstrap decision out of `sdf_button_dispatch_action()` into a shared authorization helper owned by the services authorization layer — the same layer that owns `pending_admin_action`, the admin-fingerprint gate, and `sdf_services_execute_admin_action()`.
- Make the request's **origin** an explicit parameter of that decision rather than an implicit consequence of which function contains the code. The bypass SHALL apply only to a locally-originated physical request; a remotely-originated request SHALL NOT be granted it, on an unclaimed device or otherwise.
- `sdf_button_dispatch_action()` becomes a caller of the helper, passing local-physical origin. Its observable behavior is unchanged.
- `sdf_services_request_admin_action()` becomes a caller of the same helper, passing remote origin. Its observable behavior is unchanged — it still never bypasses — but that is now an expressed decision rather than an omission.
- Correct "Simplified Pre-Enrollment Bootstrap Branch" to describe what the bypass actually does — `ENROLL` routes into local enrollment, every other button-reachable action routes into the admin-action execution callback, and `ENROLL_ADMIN` cannot reach it at all.
- **No behavior change on any existing path.** This change makes today's behavior explicit, correctly specified, and single-sited; it does not widen or narrow the bypass.

## Capabilities

### Modified Capabilities
- `sdf-services-tasks`: reword "Simplified Pre-Enrollment Bootstrap Branch" to describe the bootstrap bypass as a property of the admin-action authorization path rather than of `sdf_button_task`, and to state its actual breadth; and add requirements restricting the bypass to locally-originated physical requests and requiring it to be decided in one place.

## Impact

- Code: `firmware/components/sdf_services/src/sdf_services_button.c` (bootstrap branch removed, replaced by a helper call), `sdf_services.c` (helper added; `sdf_services_request_admin_action()` routed through it), `sdf_services_internal.h` (declaration).
- No public API signature change. `sdf_button_dispatch_action()` keeps its signature and its non-static linkage for the host tests.
- Security posture: unchanged in effect, but the unauthenticated-execution bypass becomes a single reviewable function with an explicit origin gate, rather than an implicit property of one input handler.
- **Sequencing**: lands **second** of four. Approved order: `unify-pending-admin-action-led-mapping` → `centralize-unclaimed-device-bootstrap` → `dispatch-admin-actions-off-esp-timer` → `quiesce-poll-loops-light-sleep`. It is a prerequisite for `dispatch-admin-actions-off-esp-timer`, which relocates button-originated dispatch to `sdf_admin_task` and needs the bootstrap branch to already have a home that survives that move. It is placed *before* that change deliberately: its lock-discipline risk (design Decision 3) is best exercised while dispatch is still synchronous, so a deadlock has one candidate cause rather than two.
- **Spec-delta overlap**: `quiesce-poll-loops-light-sleep` also MODIFIES "Simplified Pre-Enrollment Bootstrap Branch", solely to strip `sdf_button_task` naming. Since this change lands first and reworks that requirement more thoroughly, that change's delta for it is expected to be dropped at its archive time rather than reconciled.
- Out of scope: which actions are eligible for the bypass (still `ENROLL` only, unchanged), the admin-fingerprint gate itself, the pending-action timeout, and the enrollment flow the bypass starts.
