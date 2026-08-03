## 1. Version Control Configuration

- [x] 1.1 Add `*.key` and `ota_private.key` to the root `.gitignore` file.

## 2. CMake Build System Modification

- [x] 2.1 Modify `firmware/components/sdf_ota/CMakeLists.txt` to check if `ota_private.key` exists in the expected location.
- [x] 2.2 If the key does not exist, use a CMake `execute_process` command to run the Python key generation script (`tools/sdf_sign_ota.py` with appropriate arguments) so a throwaway key is generated before the build proceeds.
- [x] 2.3 Ensure the build uses the auto-generated key correctly for signature extraction and signing.

## 3. Testing and Verification

- [x] 3.1 Run `idf.py build` locally without a key to verify the key is generated automatically.
- [x] 3.2 Verify that the generated key is ignored by Git (`git status`).
- [x] 3.3 Ensure the build completes successfully and the firmware embeds the newly generated public key.
