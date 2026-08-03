## Why

Currently, the `ota_private.key` file is required to build OTA updates, but it is not ignored via `.gitignore`. This creates a security risk where developers might accidentally commit a private key or use a dummy key that compromises production devices. The Auto-Gen approach resolves this by ensuring a throwaway key is seamlessly generated for local development, removing contributor friction while allowing CI/CD systems to securely inject real production keys.

## What Changes

- Ignore `*.key` and `ota_private.key` in the `.gitignore` files.
- Modify `firmware/components/sdf_ota/CMakeLists.txt` to conditionally generate a unique local private key if one is not present.
- This ensures local developers can seamlessly build without manually creating keys or exposing security keys in the repository.

## Capabilities

### New Capabilities
- `ota-key-autogen`: Secure, automated generation of a throwaway OTA signing key for local development builds.

### Modified Capabilities

## Impact

- `firmware/components/sdf_ota/CMakeLists.txt`
- `.gitignore`
- Developer workflow: contributors will not need to manually configure keys to run `idf.py build`.
