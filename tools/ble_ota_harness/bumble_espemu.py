"""esp-emu <-> Bumble BLE bridge for the SDF ble_ota_gate fixture.

Architecture (mirrors bumble/apps/controllers.py, see
add-ble-ota-emulator-harness design.md D6 "Outcome"): two
bumble.controller.Controller instances attached to one shared
bumble.link.LocalLink, each serving an HCI `tcp-server:` transport.
`esp-emu --ble-hci tcp:host:port` dials in as the TCP *client* on the
device-side port; the harness's central Device attaches to the central-side
port as a plain HCI host.

Four deviations from stock Bumble (Controller/LocalLink) are required and were
established by the D6 spike (design.md D6 "Outcome"):

  1. Stock Controller has no handler for HCI LE Set Privacy Mode, so it
     answers "Unknown HCI Command". NimBLE tolerates that during discovery,
     but we answer SUCCESS instead so the exchange is spec-conformant.

  2. Stock Controller defaults its public address to 00:00:00:00:00:00,
     which NimBLE's ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, ...) refuses with
     BLE_HS_ENOADDR. Both controllers are therefore given explicit non-zero
     public addresses.

  3. Stock LocalLink stamps LE ACL PDUs with the sender controller's
     *random* address, which the Device host overwrites at power-on, so
     every PDU arrives stamped with an address that matches no connection's
     peer and is dropped with "no connection for ..." - SMP stalls forever.
     PublicAddressLocalLink below routes by the sender's public address,
     which is what both connections here were established on.

  4. Stock Controller sends Encryption Change without asking the host for
     its LTK first; NimBLE sits in its SMP LTK_START state, sees an
     unexpected Encryption Change and aborts pairing with UNSPECIFIED_REASON.
     PatchedController.on_le_encrypted emits HCI_LE_Long_Term_Key_Request
     first (its reply/negative-reply handlers complete the handshake).

Additionally, Bumble's default pairing config requests MITM, which a
NoInputNoOutput central cannot satisfy - create_central() pins the pairing
config to sc=True, mitm=False, bonding=True to match the firmware's Just
Works flags (design.md D6, "Outcome").

CI wiring (task 8.3) must use this module rather than a bare
`bumble.apps.controllers` bridge - see design.md D6's Outcome note.
"""

from __future__ import annotations

import asyncio
import logging

import bumble.logging
from bumble import core
from bumble import hci
from bumble.controller import Controller
from bumble.device import Device, DeviceConfiguration
from bumble.link import LocalLink
from bumble.transport import open_transport


DEVICE_PUBLIC_ADDRESS = "F2:11:22:33:44:01"
CENTRAL_PUBLIC_ADDRESS = "F2:11:22:33:44:02"

COMPANION_DEVICE_NAME = "SDF"

SVC_UUID = "7d5a0000-5c2b-4f8a-9e3d-1a2b3c4d5e6f"
AUTH_UUID = "7d5a0001-5c2b-4f8a-9e3d-1a2b3c4d5e6f"
CONFIG_UUID = "7d5a0002-5c2b-4f8a-9e3d-1a2b3c4d5e6f"
ENROLL_UUID = "7d5a0003-5c2b-4f8a-9e3d-1a2b3c4d5e6f"
OTA_UUID = "7d5a0004-5c2b-4f8a-9e3d-1a2b3c4d5e6f"
# Device health report (companion-device-health): read + notify JSON.
STATUS_UUID = "7d5a0006-5c2b-4f8a-9e3d-1a2b3c4d5e6f"


class PublicAddressLocalLink(LocalLink):
    """LocalLink whose LE ACL routing uses public addresses.

    Stock LocalLink.send_acl_data stamps LE ACL PDUs with the sender
    controller's *random* address. The bumble Device host overwrites its
    controller's random address with a generated static random at power-on,
    so PDUs then arrive stamped with an address that matches no connection's
    peer (every connection here uses public addresses) and are dropped with
    "no connection for ..." - SMP stalls forever. Routing by the sender's
    public address, which is what both connections were established on,
    fixes the mismatch.
    """

    def send_acl_data(
        self,
        sender_controller: Controller,
        destination_address: hci.Address,
        transport: core.PhysicalTransport,
        data: bytes,
    ) -> None:
        if transport == core.PhysicalTransport.LE:
            destination_controller = self.find_le_controller(destination_address)
            source_address = sender_controller.public_address
        else:
            super().send_acl_data(
                sender_controller, destination_address, transport, data
            )
            return
        if destination_controller is not None:
            asyncio.get_running_loop().call_soon(
                lambda: destination_controller.on_link_acl_data(
                    source_address, transport, data
                )
            )


class PatchedController(Controller):
    """Stock Bumble controller emulation plus the fixes from D6."""


    def on_le_encrypted(self, connection) -> None:
        # Stock Bumble "just sets up the encryption without asking the host".
        # A real controller asks the peripheral host for its LTK first
        # (HCI LE Long Term Key Request event). NimBLE requires this: it sits
        # in its SMP LTK_START state waiting for the request, so an immediate
        # Encryption Change arrives in an unexpected SMP state and it aborts
        # pairing with UNSPECIFIED_REASON. The simulated link carries ACL
        # traffic in plaintext either way, so the LTK value itself is not
        # checked - only the handshake order matters.
        self.send_hci_packet(
            hci.HCI_LE_Long_Term_Key_Request_Event(
                connection_handle=connection.handle,
                random_number=b"\x00" * 8,
                encryption_diversifier=0,
            )
        )

    def on_hci_le_long_term_key_request_reply_command(
        self, command: hci.HCI_LE_Long_Term_Key_Request_Reply_Command
    ) -> hci.HCI_StatusAndConnectionHandleReturnParameters:
        connection = self.find_connection_by_handle(command.connection_handle)
        if connection is None:
            return hci.HCI_StatusAndConnectionHandleReturnParameters(
                status=hci.HCI_ErrorCode.UNKNOWN_CONNECTION_IDENTIFIER_ERROR,
                connection_handle=command.connection_handle,
            )
        # Complete the (simulated) LL START_ENC handshake.
        self.send_hci_packet(
            hci.HCI_Encryption_Change_Event(
                status=hci.HCI_ErrorCode.SUCCESS,
                connection_handle=connection.handle,
                encryption_enabled=1,
            )
        )
        return hci.HCI_StatusAndConnectionHandleReturnParameters(
            status=hci.HCI_ErrorCode.SUCCESS,
            connection_handle=command.connection_handle,
        )

    def on_hci_le_long_term_key_request_negative_reply_command(
        self, command: hci.HCI_LE_Long_Term_Key_Request_Negative_Reply_Command
    ) -> hci.HCI_StatusAndConnectionHandleReturnParameters:
        return hci.HCI_StatusAndConnectionHandleReturnParameters(
            status=hci.HCI_ErrorCode.SUCCESS,
            connection_handle=command.connection_handle,
        )

    def on_hci_le_set_privacy_mode_command(
        self, command: hci.HCI_LE_Set_Privacy_Mode_Command
    ) -> hci.HCI_StatusReturnParameters:
        return hci.HCI_StatusReturnParameters(hci.HCI_ErrorCode.SUCCESS)


class EmulatorBleBridge:
    """Owns the simulated link, both controllers and their TCP transports."""

    def __init__(self, device_port: int, central_port: int):
        self.device_port = device_port
        self.central_port = central_port
        self.link = PublicAddressLocalLink()
        self.device_transport = None
        self.central_transport = None
        self.device_controller = None
        self.central_controller = None

    async def start(self, debug: bool = False) -> None:
        if debug:
            bumble.logging.setup_basic_logging(default_level="debug")
        self.device_transport = await open_transport(
            f"tcp-server:127.0.0.1:{self.device_port}"
        )
        self.central_transport = await open_transport(
            f"tcp-server:127.0.0.1:{self.central_port}"
        )
        self.device_controller = PatchedController(
            "emu-device",
            host_source=self.device_transport.source,
            host_sink=self.device_transport.sink,
            link=self.link,
            public_address=DEVICE_PUBLIC_ADDRESS,
        )
        self.central_controller = PatchedController(
            "emu-central",
            host_source=self.central_transport.source,
            host_sink=self.central_transport.sink,
            link=self.link,
            public_address=CENTRAL_PUBLIC_ADDRESS,
        )

    async def close(self) -> None:
        for transport in (self.central_transport, self.device_transport):
            if transport is not None:
                await transport.close()


async def create_central(bridge: EmulatorBleBridge) -> Device:
    """Build the GATT central Device attached to the bridge's central port."""
    source, sink = await open_transport(f"tcp-client:127.0.0.1:{bridge.central_port}")
    config = DeviceConfiguration()
    config.name = "sdf-harness-central"
    # Default io_capability (NO_INPUT_NO_OUTPUT), SC and bonding enabled,
    # MITM off - matches the firmware's sm_io_cap = BLE_SM_IO_CAP_NO_IO,
    # sm_mitm = 0, sm_sc = 1 Just Works configuration.
    device = Device.from_config_with_hci(config, source, sink)
    # The firmware pairs Just Works: sm_io_cap = BLE_SM_IO_CAP_NO_IO,
    # sm_mitm = 0, sm_sc = 1. Bumble's default pairing config requests MITM,
    # which a NoInputNoOutput pair cannot satisfy - NimBLE accepts the SC
    # crypto exchange and then aborts with SMP_PAIRING_FAILED right after
    # encryption. Match the firmware's flags exactly.
    from bumble import pairing

    device.smp_manager.pairing_config_factory = lambda connection: (
        pairing.PairingConfig(sc=True, mitm=False, bonding=True)
    )
    await device.power_on()
    return device


async def find_companion(
    central: Device, name: str = COMPANION_DEVICE_NAME, timeout_s: float = 30.0
) -> hci.Address:
    """Scan until the emulated device advertises under `name`; return its address."""
    address = await asyncio.wait_for(
        central.find_peer_by_name(name), timeout=timeout_s
    )
    return address
