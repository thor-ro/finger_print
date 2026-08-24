"""D6 gate (add-ble-ota-emulator-harness task 6.4): connect, LE SC Just Works
pair, and receive one GATT notification from the ble_ota_gate fixture running
under esp-emu.

This is the Layer 2 go/no-go: pairing or notifications failing here means the
rest of the harness (login, transfer) cannot be carried by Bumble's controller
emulation and design.md D6's fallback applies.

Emits a machine-checkable terminal line:
  BLE_SPIKE_PAIR_RESULT status=PASS|FAIL stage=<stage> detail=<detail>
"""

from __future__ import annotations

import argparse
import asyncio
import sys

from bumble import hci

from bumble_espemu import EmulatorBleBridge, create_central, find_companion
from sdf_companion_client import CompanionClient

RESULT_PREFIX = "BLE_SPIKE_PAIR_RESULT"


def emit(status: str, stage: str, detail: str = "") -> None:
    print(f"{RESULT_PREFIX} status={status} stage={stage} detail={detail}", flush=True)


async def run(args: argparse.Namespace) -> int:
    bridge = EmulatorBleBridge(args.device_port, args.central_port)
    await bridge.start(debug=args.debug)
    try:
        central = await create_central(bridge)

        address = await find_companion(central, timeout_s=args.scan_timeout)
        print(f"discovered {COMPANION_NAME} at {address}", flush=True)

        connection = await asyncio.wait_for(
            central.connect(
                address,
                own_address_type=hci.OwnAddressType.PUBLIC,
                timeout=args.connect_timeout,
            ),
            timeout=args.connect_timeout + 5,
        )
        print("connected", flush=True)

        # LE Secure Connections Just Works - no passkey, no OOB.
        await asyncio.wait_for(connection.pair(), timeout=args.pair_timeout)
        if not connection.is_encrypted:
            emit("FAIL", "pair", "link not encrypted after pairing")
            return 1
        print(f"paired (encryption={connection.is_encrypted})", flush=True)

        client = CompanionClient(central, connection)
        await client.discover()
        await client.subscribe()
        print("subscribed to AUTH notifications", flush=True)

        # REGISTER -> fixture auto-approves the WEB_REG_AUTH admin action
        # (design.md D10) -> 1-byte AUTH notification 0x01. This is the
        # notification the D6 gate requires.
        await client.register("harness", args.password, timeout_s=args.notify_timeout)
        print("received registration result notification", flush=True)

        emit("PASS", "notify", f"address={address}")
        return 0
    except Exception as exc:  # noqa: BLE001 - any failure must be loud
        emit("FAIL", getattr(exc, "stage", "unknown"), repr(exc))
        return 1
    finally:
        await bridge.close()


COMPANION_NAME = "SDF"


async def amain() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device-port", type=int, default=14431)
    parser.add_argument("--central-port", type=int, default=14432)
    parser.add_argument("--scan-timeout", type=float, default=30.0)
    parser.add_argument("--connect-timeout", type=float, default=15.0)
    parser.add_argument("--pair-timeout", type=float, default=30.0)
    parser.add_argument("--notify-timeout", type=float, default=20.0)
    parser.add_argument("--password", default="correct horse battery staple")
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()
    return await run(args)


if __name__ == "__main__":
    sys.exit(asyncio.run(amain()))
