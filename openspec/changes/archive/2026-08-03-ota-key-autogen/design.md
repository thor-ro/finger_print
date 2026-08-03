## Context

The ESP32-C6 firmware enforces Ed25519 signatures for OTA updates. The build system uses an asymmetric key pair: the public key is embedded in the firmware, and the private key (`ota_private.key`) is used to sign the binaries. Currently, the build expects `ota_private.key` to be present in the `firmware/` directory, but the key is not ignored in `.gitignore`, posing a risk of accidental commits of real or dummy keys to this public repository.

## Goals / Non-Goals

**Goals:**
- Prevent accidental commits of OTA private keys to the repository.
- Ensure developers can clone and build the project with zero manual key setup.
- Maintain the integrity of the signature verification flow for local development.

**Non-Goals:**
- Changing the OTA signature algorithm from Ed25519.
- Modifying how CI injects the production key (this design assumes CI handles its own secrets injection).

## Decisions

**Decision 1: Auto-generate the OTA private key if missing**
- **Rationale:** By modifying `CMakeLists.txt` to invoke the key generation script automatically when `ota_private.key` is missing, we completely remove onboarding friction. Developers won't experience build failures asking them to run a manual script.
- **Alternatives Considered:** 
  - *Dummy Key:* Committing a `dev_key.pem` to the repo. Rejected because it opens the risk of flashing production devices with firmware signed by a public key, leaving them vulnerable to rogue OTA updates.
  - *BYO Key (Strict):* Failing the build until the developer generates the key. Rejected because it adds unnecessary friction for new contributors.

**Decision 2: Ignore `*.key` and `ota_private.key` in `.gitignore`**
- **Rationale:** Ensures that the auto-generated keys are never accidentally pushed to the repository.

## Risks / Trade-offs

- **[Risk] CI build failure if key injection happens after CMake configuration** → **Mitigation:** Ensure CI workflow injects the `ota_private.key` secret *before* `idf.py build` is invoked so that CMake finds the production key and does not generate a new one.
