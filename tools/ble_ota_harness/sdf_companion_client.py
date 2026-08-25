"""GATT client for the SDF BLE companion protocol (add-ble-ota-emulator-harness).

Mirrors the firmware's wire format exactly; every constant below is cited to
its defining source line:

- AUTH opcodes (sdf_ble_companion.c:40-45):
    0x00 LOGOUT, 0x01 RESULT_OK, 0x02 REGISTER, 0x03 LOGIN_INIT,
    0x04 LOGIN_VERIFY, 0x05... (RESULT_PENDING = 0x02 on the wire as a
    notification value).
  Note REGISTER's opcode byte on the wire is 0x02 and a *successful*
  registration/login is signalled by a 1-byte AUTH notification of 0x01
  (sdf_ble_companion_set_authenticated -> ble_gatts_notify_custom,
  sdf_ble_companion.c:1455-1467).

- LOGIN_INIT write: [0x03][username_len][username]
  (sdf_ble_companion.c:331-334).
- Challenge read: [salt(16)][iteration_count(4, LE)][nonce(16)]
  (sdf_ble_companion.c:290-299).
- LOGIN_VERIFY write: [0x04][response(32)] where response =
  HMAC-SHA256(key=stretched_credential, msg=nonce)
  (sdf_ble_companion.c:371-373; sdf_services_web_auth.c verify_response).
- stretched_credential = PBKDF2-HMAC-SHA256(password_hash, salt, iterations,
  dklen=32) (sdf_services_web_auth_stretch_credential); password_hash =
  SHA-256(password) sent raw (SDF_STORAGE_WEB_USER_HASH_LEN=32);
  SDF_SERVICES_WEB_AUTH_PBKDF2_ITERATIONS=10000 (sdf_services.h:123-124).

- OTA writes are [opcode][payload] against sdf_ble_ota_protocol.h:
    BEGIN 0x01 + u32le size, CHUNK 0x02 + data, END 0x03.
  Max chunk payload for an MTU is mtu - 4 (SDF_BLE_OTA_MTU_OVERHEAD).
  Status notifications are JSON: {"status":"ready","offset":N},
  {"status":"chunk_ack","offset":N}, {"status":"success"},
  {"status":"failed","error": "..."} (sdf_ble_companion_ota.c).

If any of these firmware constants change, this module fails loudly (login or
transfer rejected), never silently - see design.md D8.
"""

from __future__ import annotations

import asyncio
import hashlib
import hmac as hmac_mod
import json
import struct
from collections.abc import Callable

from bumble.device import Device, Peer

from bumble_espemu import (
    AUTH_UUID,
    CONFIG_UUID,
    ENROLL_UUID,
    OTA_UUID,
    STATUS_UUID,
    SVC_UUID,
)

# --- auth protocol constants (see module docstring for sources) -------------
AUTH_OPCODE_LOGOUT = 0x00
AUTH_RESULT_OK = 0x01
AUTH_OPCODE_REGISTER = 0x02
AUTH_OPCODE_LOGIN_INIT = 0x03
AUTH_OPCODE_LOGIN_VERIFY = 0x04

WEB_USER_HASH_LEN = 32
WEB_USER_SALT_LEN = 16
WEB_STRETCHED_LEN = 32
PBKDF2_ITERATIONS = 10_000
NONCE_LEN = 16
RESPONSE_LEN = 32

# --- ota protocol constants --------------------------------------------------
OTA_OPCODE_BEGIN = 0x01
OTA_OPCODE_CHUNK = 0x02
OTA_OPCODE_END = 0x03
OTA_MTU_OVERHEAD = 4


class CompanionError(Exception):
    pass


class CompanionClient:
    """Drives register / login / OTA transfer over the companion GATT service."""

    def __init__(self, central: Device, connection):
        self.central = central
        self.connection = connection
        self.peer = Peer(connection)
        self.auth_queue: asyncio.Queue[bytes] = asyncio.Queue()
        self.ota_queue: asyncio.Queue[bytes] = asyncio.Queue()
        # User-management replies/list-parts/progress (JSON) from the
        # Enrollment characteristic (companion-user-mgmt).
        self.enroll_queue: asyncio.Queue[dict] = asyncio.Queue()
        # Device-health reports / change markers from the Status
        # characteristic (companion-device-health).
        self.status_queue: asyncio.Queue[bytes] = asyncio.Queue()
        self.auth = None
        self.ota = None
        self.config = None
        self.enroll = None
        self.status = None
        self.att_mtu = 23

    async def discover(self) -> None:
        services = await self.peer.discover_services()
        if not any(str(s.uuid).lower() == SVC_UUID.lower() for s in services):
            raise CompanionError("companion service not found")
        chars = await self.peer.discover_characteristics()
        uuid_strs = {str(c.uuid).lower(): c for c in chars}
        if AUTH_UUID not in uuid_strs or OTA_UUID not in uuid_strs:
            raise CompanionError(
                f"expected AUTH and OTA characteristics, found {sorted(uuid_strs)}"
            )
        self.auth = uuid_strs[AUTH_UUID]
        self.ota = uuid_strs[OTA_UUID]
        # Optional here (OTA scenarios never touch it); the identity scenario
        # asserts on it via require_config_discovered().
        self.config = uuid_strs.get(CONFIG_UUID)
        self.enroll = uuid_strs.get(ENROLL_UUID)
        self.status = uuid_strs.get(STATUS_UUID)

    def require_config_discovered(self) -> None:
        if self.config is None:
            raise CompanionError("Config characteristic not discovered")

    async def read_config(self) -> bytes:
        """Read the Config characteristic. Raises bumble ProtocolError with
        error_code 0x05 (INSUFFICIENT_AUTHENTICATION) when the session's
        live-derived authority does not cover it."""
        self.require_config_discovered()
        return await self.config.read_value()

    def require_enroll_discovered(self) -> None:
        if self.enroll is None:
            raise CompanionError("Enrollment characteristic not discovered")

    # --- device health (companion-device-health) ------------------------------

    def require_status_discovered(self) -> None:
        if self.status is None:
            raise CompanionError("Status characteristic not discovered")

    async def read_status(self) -> bytes:
        """Read the Status characteristic. Pre-login this raises bumble
        ProtocolError 0x05 (INSUFFICIENT_AUTHENTICATION); post-login it
        returns the health-report JSON."""
        self.require_status_discovered()
        return await self.status.read_value()

    async def subscribe_status(self) -> None:
        """Subscribe to Status notifications (health report updates)."""
        self.require_status_discovered()
        await self.status.subscribe(self._on_status_notify)

    def _on_status_notify(self, value: bytes) -> None:
        self.status_queue.put_nowait(bytes(value))

    async def next_status_notification(self, timeout_s: float = 5.0) -> bytes:
        """Resolves with the raw payload of the next Status notification -
        a JSON health report, or an empty change marker."""
        return await asyncio.wait_for(self.status_queue.get(), timeout=timeout_s)

    async def drain_status_notifications(self) -> list[bytes]:
        items: list[bytes] = []
        while not self.status_queue.empty():
            items.append(self.status_queue.get_nowait())
        return items

    async def read_enroll(self) -> bytes:
        """Read the Enrollment characteristic - a minimal-packet authority
        probe (its value is empty unless an enrollment is in progress), so it
        exercises the same live-authority gate as Config without a large
        multi-PDU response. Same 0x05 refusal semantics as Config."""
        self.require_enroll_discovered()
        return await self.enroll.read_value()

    async def subscribe(self) -> None:
        await self.auth.subscribe(self._on_auth_notify)
        await self.ota.subscribe(self._on_ota_notify)
        if self.enroll is not None:
            await self.enroll.subscribe(self._on_enroll_notify)

    async def negotiate_mtu(self, mtu: int = 247) -> int:
        """Exchange ATT MTU; returns the negotiated value."""
        self.att_mtu = await self.peer.request_mtu(mtu)
        return self.att_mtu

    def _on_auth_notify(self, value: bytes) -> None:
        self.auth_queue.put_nowait(value)

    def _on_ota_notify(self, value: bytes) -> None:
        self.ota_queue.put_nowait(value)

    def _on_enroll_notify(self, value: bytes) -> None:
        try:
            self.enroll_queue.put_nowait(json.loads(bytes(value).decode()))
        except (ValueError, UnicodeDecodeError) as exc:
            raise CompanionError(f"bad enroll notification: {exc}") from exc

    # --- user management (companion-user-mgmt wire format) --------------------

    async def um_write(self, request: dict) -> None:
        """Writes one user-management request without waiting for its reply."""
        await self.enroll.write_value(
            json.dumps(request).encode(), with_response=True
        )

    async def um_wait_reply(self, req_id: int, timeout_s: float = 15.0) -> dict:
        """Resolves with the terminal message for `req_id` - a {"req":N,
        "result":...} reply, or the final list part ("end":true) for a list.
        Messages belonging to other requests (e.g. stale progress) are
        skipped."""
        deadline = asyncio.get_running_loop().time() + timeout_s
        while True:
            remaining = deadline - asyncio.get_running_loop().time()
            if remaining <= 0:
                raise CompanionError(f"timeout waiting for UM reply req={req_id}")
            try:
                msg = await asyncio.wait_for(self.enroll_queue.get(),
                                             timeout=remaining)
            except asyncio.TimeoutError as exc:
                raise CompanionError(
                    f"timeout waiting for UM reply req={req_id}"
                ) from exc
            if isinstance(msg, dict) and msg.get("req") == req_id:
                return msg

    # --- auth -----------------------------------------------------------------

    @staticmethod
    def stretch_credential(
        password_hash: bytes, salt: bytes, iteration_count: int
    ) -> bytes:
        """Mirror of sdf_services_web_auth_stretch_credential()."""
        return hashlib.pbkdf2_hmac(
            "sha256", password_hash, salt, iteration_count, dklen=WEB_STRETCHED_LEN
        )

    @staticmethod
    def login_response(stretched: bytes, nonce: bytes) -> bytes:
        """Mirror of sdf_services_web_auth_verify_response()'s expectation."""
        return hmac_mod.new(stretched, nonce, hashlib.sha256).digest()

    async def _await_auth_notification(self, timeout_s: float) -> bytes:
        return await asyncio.wait_for(self.auth_queue.get(), timeout=timeout_s)

    async def register(
        self, username: str, password: str, timeout_s: float = 20.0
    ) -> None:
        """REGISTER write; resolves when the admin-approved result notification
        arrives (the fixture auto-approves, design.md D10)."""
        password_hash = hashlib.sha256(password.encode()).digest()
        payload = (
            bytes([AUTH_OPCODE_REGISTER, len(username)])
            + username.encode()
            + password_hash
        )
        # AUTH queue: a stale notification left over from an earlier exchange
        # could satisfy this REGISTER's result wait below. OTA queue: drains
        # whatever is left by task 7.4's pre-login rejection test.
        while not self.auth_queue.empty():
            self.auth_queue.get_nowait()
        await self.ota_clear_queues()
        await self.auth.write_value(payload, with_response=True)
        value = await self._await_auth_notification(timeout_s)
        if value != bytes([AUTH_RESULT_OK]):
            raise CompanionError(f"registration failed, notification={value.hex()}")

    async def login(self, username: str, password: str, timeout_s: float = 15.0) -> None:
        """Challenge-response login per task 1.2's captured parameters."""
        await self.auth.write_value(
            bytes([AUTH_OPCODE_LOGIN_INIT, len(username)]) + username.encode(),
            with_response=True,
        )
        challenge = await self.auth.read_value()
        if len(challenge) != WEB_USER_SALT_LEN + 4 + NONCE_LEN:
            raise CompanionError(f"bad challenge length {len(challenge)}")
        salt = challenge[:WEB_USER_SALT_LEN]
        (iteration_count,) = struct.unpack_from("<I", challenge, WEB_USER_SALT_LEN)
        nonce = challenge[WEB_USER_SALT_LEN + 4 :]
        if iteration_count != PBKDF2_ITERATIONS:
            raise CompanionError(f"unexpected iteration count {iteration_count}")

        stretched = self.stretch_credential(
            hashlib.sha256(password.encode()).digest(), salt, iteration_count
        )
        response = self.login_response(stretched, nonce)
        await self.auth.write_value(
            bytes([AUTH_OPCODE_LOGIN_VERIFY]) + response, with_response=True
        )
        value = await self._await_auth_notification(timeout_s)
        if value != bytes([AUTH_RESULT_OK]):
            raise CompanionError(f"login rejected, notification={value.hex()}")

    # --- ota ------------------------------------------------------------------

    async def ota_clear_queues(self) -> None:
        while not self.ota_queue.empty():
            self.ota_queue.get_nowait()

    async def ota_transfer(
        self,
        image: bytes,
        chunk_size: int | None = None,
        expect_success: bool = True,
        notify_timeout_s: float = 20.0,
        inter_chunk_delay_s: float = 0.0,
        progress_callback: Callable[[str, int], None] | None = None,
    ) -> dict:
        """BEGIN/CHUNK/END over the OTA characteristic.

        Returns the terminal status dict. Asserts (via CompanionError) that no
        chunk is empty or exceeds max_chunk_len(mtu), and surfaces the error
        reason from the failed status rather than just success/failure.

        progress_callback (optional) is invoked synchronously as each stage
        completes, letting a caller track positive transfer evidence without
        polling the queues: ("begin_ready", 0) after BEGIN is acknowledged
        ready; ("chunk_ack", offset) after each chunk acknowledgement is
        validated against the running offset; ("end_written", offset) after
        the END write was accepted.
        """
        mtu = self.att_mtu
        max_chunk = max(0, mtu - OTA_MTU_OVERHEAD)
        if chunk_size is None:
            chunk_size = max_chunk
        if chunk_size <= 0 or chunk_size > max_chunk:
            raise CompanionError(f"chunk size {chunk_size} invalid for MTU {mtu}")
        if image is None or len(image) == 0:
            raise CompanionError("empty image")

        await self.ota_clear_queues()
        await self.ota.write_value(
            bytes([OTA_OPCODE_BEGIN]) + struct.pack("<I", len(image)),
            with_response=True,
        )
        ready = await self._next_status(notify_timeout_s)
        if ready.get("status") != "ready":
            raise CompanionError(f"expected ready at BEGIN, got {ready}")
        if progress_callback is not None:
            progress_callback("begin_ready", 0)

        offset = 0
        while offset < len(image):
            chunk = image[offset : offset + chunk_size]
            if not chunk:
                raise CompanionError("internal: empty chunk")
            if len(chunk) > max_chunk:
                raise CompanionError("internal: oversized chunk")
            # Write-without-response: the chunk_ack notification is the
            # application-level acknowledgement (sdf_ble_companion_ota.c
            # notifies on every accepted chunk), and the harness keeps at most
            # one chunk outstanding, so ordering and loss detection are
            # preserved without depending on ATT request/response pacing.
            await self.ota.write_value(
                bytes([OTA_OPCODE_CHUNK]) + chunk, with_response=False
            )
            await asyncio.sleep(0)
            ack = await self._next_status(notify_timeout_s)
            if inter_chunk_delay_s > 0:
                await asyncio.sleep(inter_chunk_delay_s)
            if ack.get("status") != "chunk_ack":
                raise CompanionError(f"expected chunk_ack, got {ack}")
            new_offset = ack.get("offset")
            if not isinstance(new_offset, int) or new_offset != offset + len(chunk):
                raise CompanionError(f"ack offset {new_offset} != {offset + len(chunk)}")
            offset = new_offset
            if progress_callback is not None:
                progress_callback("chunk_ack", offset)

        await self.ota.write_value(bytes([OTA_OPCODE_END]), with_response=True)
        if progress_callback is not None:
            progress_callback("end_written", offset)
        final = await self._next_status(notify_timeout_s)
        status = final.get("status")
        if expect_success and status != "success":
            raise CompanionError(f"transfer expected success, got {final}")
        if not expect_success and status != "failed":
            raise CompanionError(f"transfer expected failure, got {final}")
        if status == "failed" and not final.get("error"):
            raise CompanionError(f"failure carried no reason: {final}")
        return final

    async def _next_status(self, timeout_s: float) -> dict:
        raw = await asyncio.wait_for(self.ota_queue.get(), timeout=timeout_s)
        try:
            parsed = json.loads(raw.decode())
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise CompanionError(f"unparseable OTA notification {raw!r}") from exc
        if not isinstance(parsed, dict) or "status" not in parsed:
            raise CompanionError(f"OTA notification without status: {parsed!r}")
        return parsed
