"""Layer 2 harness (add-ble-ota-emulator-harness): drive the companion BLE
OTA protocol against the ble_ota_gate fixture running under esp-emu.

One boot runs the whole sequence:

  1. discover / connect / LE SC Just Works pair (7.1, 7.2)
  2. an OTA-characteristic write *before* login must be rejected with
     insufficient-authentication (7.4)
  3. REGISTER + challenge-response LOGIN (7.3)
  4. tampered image over BEGIN/CHUNK/END -> rejected with the signature error
     reason (7.5, 7.6, 8.1 case 1)
  5. foreign-key image -> rejected likewise (8.1 case 2); that both rejected
     sessions are followed by a successful BEGIN/commit proves session
     recovery (8.2)
  6. valid image -> commit; sdf_ota_verify_and_commit() reboots the device on
     success and never returns (sdf_ota.c:463-473), so the terminal success is
     observed as a connection loss without any failure status

Emits a machine-checkable terminal line:
  BLE_OTA_HARNESS_RESULT status=PASS cases_run=N/3 prelogin_reject=OK \
      tampered=REJECT foreign_key=REJECT valid=COMMIT
A fixture or harness run that never reaches all three cases cannot print
status=PASS.
"""

from __future__ import annotations

import argparse
import asyncio
import sys
from pathlib import Path

from bumble import hci
from bumble.core import ProtocolError

from bumble_espemu import EmulatorBleBridge, create_central, find_companion
from sdf_companion_client import (
    OTA_OPCODE_BEGIN,
    CompanionClient,
    CompanionError,
)

RESULT_PREFIX = "BLE_OTA_HARNESS_RESULT"

# BLE_ATT_ERR_INSUFFICIENT_AUTHEN - what sdf_ble_companion's access callbacks
# return for writes from a connection that is encrypted but not logged in.
ATT_ERR_INSUFFICIENT_AUTHENTICATION = 0x05

# esp_err_to_name(SDF_ERR_OTA_SIGNATURE_INVALID) - the code is component-local
# (sdf_ota.h:43) and not registered with esp_err_to_name, so it renders as hex.
SIGNATURE_INVALID_REASON = "0xA001"


def emit(status: str, detail: str = "") -> None:
    print(f"{RESULT_PREFIX} status={status} {detail}".rstrip(), flush=True)


class Harness:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.results: dict[str, str] = {}

    async def expect_disconnection(self, connection, timeout_s: float) -> bool:
        """Resolve True when the peripheral disconnects (commit reboot)."""
        loop = asyncio.get_running_loop()
        disconnected = loop.create_future()
        connection.on(
            "disconnection",
            lambda _: None if disconnected.done() else disconnected.set_result(True),
        )
        try:
            await asyncio.wait_for(disconnected, timeout=timeout_s)
            return True
        except asyncio.TimeoutError:
            return False

    async def run(self) -> int:
        bridge = EmulatorBleBridge(self.args.device_port, self.args.central_port)
        await bridge.start(debug=self.args.debug)
        try:
            central = await create_central(bridge)

            address = await find_companion(central, timeout_s=self.args.scan_timeout)
            connection = await asyncio.wait_for(
                central.connect(
                    address,
                    own_address_type=hci.OwnAddressType.PUBLIC,
                    timeout=self.args.connect_timeout,
                ),
                timeout=self.args.connect_timeout + 5,
            )
            print(f"connected to {address}", flush=True)

            # 7.2: LE Secure Connections Just Works, no passkey/OOB.
            await asyncio.wait_for(connection.pair(), timeout=self.args.pair_timeout)
            if not connection.is_encrypted:
                raise CompanionError("link not encrypted after pairing")
            print("paired (LE SC Just Works, encrypted)", flush=True)

            client = CompanionClient(central, connection)
            await client.discover()
            await client.subscribe()
            mtu = await client.negotiate_mtu(247)
            print(f"subscribed; negotiated ATT MTU {mtu}", flush=True)

            # 7.4: OTA write before login must be rejected with
            # insufficient-authentication, opening no session.
            try:
                await client.ota.write_value(
                    bytes([OTA_OPCODE_BEGIN]) + b"\x00\x00\x00\x00",
                    with_response=True,
                )
                self.results["prelogin_reject"] = "NOT_REJECTED"
                emit("FAIL", "cases_run=0/3 prelogin_reject=NOT_REJECTED")
                return 1
            except ProtocolError as exc:
                if exc.error_code != ATT_ERR_INSUFFICIENT_AUTHENTICATION:
                    self.results["prelogin_reject"] = f"WRONG_ERR_{exc.error_code:#04x}"
                    emit(
                        "FAIL",
                        f"cases_run=0/3 prelogin_reject=WRONG_ERR_{exc.error_code:#04x}",
                    )
                    return 1
                self.results["prelogin_reject"] = "OK"
            print("pre-login OTA write rejected with insufficient-authentication", flush=True)

            # 7.3: register + challenge-response login.
            await client.register("harness", self.args.password)
            await client.login("harness", self.args.password)
            print("registered and logged in", flush=True)

            images = {
                name: Path(path).read_bytes()
                for name, path in (
                    ("tampered", self.args.tampered_image),
                    ("foreign_key", self.args.foreign_image),
                    ("valid", self.args.valid_image),
                )
            }

            # 8.1 cases 1+2: rejects. Their successful BEGINs after a previous
            # rejection are also the session-recovery evidence (8.2).
            for case in ("tampered", "foreign_key"):
                final = await client.ota_transfer(
                    images[case],
                    expect_success=False,
                    notify_timeout_s=self.args.notify_timeout,
                    inter_chunk_delay_s=self.args.inter_chunk_delay,
                    chunk_size=self.args.chunk_size or None,
                )
                if final.get("error") != SIGNATURE_INVALID_REASON:
                    emit(
                        "FAIL",
                        f"cases_run={len(self.results)}/3 {case}=REJECTED_WITH_WRONG_REASON "
                        f"reason={final.get('error')!r}",
                    )
                    return 1
                self.results[case] = f"REJECT reason={final['error']}"
                print(f"{case}: rejected as expected ({final['error']})", flush=True)

            # 8.1 case 3: the valid image commits; the device reboots inside
            # sdf_ota_verify_and_commit() before any success notification can
            # be sent, so success is observed as disconnection without any
            # prior failed status.
            await client.ota_clear_queues()

            async def transfer_valid():
                await client.ota_transfer(
                    images["valid"],
                    expect_success=False,
                    notify_timeout_s=self.args.notify_timeout,
                    inter_chunk_delay_s=self.args.inter_chunk_delay,
                )

            transfer_task = asyncio.create_task(transfer_valid())
            saw_disconnect = await self.expect_disconnection(
                connection, self.args.reboot_timeout
            )
            if not saw_disconnect:
                transfer_task.cancel()
                emit("FAIL", "cases_run=2/3 valid=NO_COMMIT (no reboot within timeout)")
                return 1
            while not client.ota_queue.empty():
                notification = client.ota_queue.get_nowait()
                if b'"failed"' in notification:
                    emit(
                        "FAIL",
                        f"cases_run=2/3 valid=FAILED unexpectedly: {notification!r}",
                    )
                    return 1
            self.results["valid"] = "COMMIT"
            print("valid image committed (device rebooted into staging partition)", flush=True)

            detail = (
                f"cases_run=3/3 prelogin_reject={self.results['prelogin_reject']} "
                f"tampered={self.results['tampered'].split()[0]} "
                f"foreign_key={self.results['foreign_key'].split()[0]} "
                f"valid={self.results['valid']}"
            )
            emit("PASS", detail)
            return 0
        except Exception as exc:  # noqa: BLE001 - any failure must be loud
            emit("FAIL", repr(exc))
            return 1
        finally:
            await bridge.close()


async def amain() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device-port", type=int, default=14431)
    parser.add_argument("--central-port", type=int, default=14432)
    parser.add_argument("--tampered-image", type=str, required=True)
    parser.add_argument("--foreign-image", type=str, required=True)
    parser.add_argument("--valid-image", type=str, required=True)
    parser.add_argument("--scan-timeout", type=float, default=120.0)
    parser.add_argument("--connect-timeout", type=float, default=15.0)
    parser.add_argument("--pair-timeout", type=float, default=30.0)
    parser.add_argument("--notify-timeout", type=float, default=20.0)
    parser.add_argument("--reboot-timeout", type=float, default=60.0)
    parser.add_argument("--inter-chunk-delay", type=float, default=0.05)
    parser.add_argument("--chunk-size", type=int, default=0)
    parser.add_argument("--password", default="correct horse battery staple")
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()
    return await Harness(args).run()


if __name__ == "__main__":
    sys.exit(asyncio.run(amain()))
