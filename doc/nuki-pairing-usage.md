# Nuki Pairing Usage (GDIO)

This note shows a minimal pairing flow using the GDIO characteristic and how to store the resulting credentials in NVS.

**Usage sketch**
```c
#include "sdf_protocol_ble.h"
#include "sdf_nuki_pairing.h"
#include "sdf_storage.h"

static sdf_nuki_client_t client;
static sdf_nuki_pairing_t pairing;

static int ble_send(void *ctx, const uint8_t *data, size_t len)
{
    // Send data to GDIO (unencrypted) or USDIO (encrypted) based on context.
    return 0;
}

static void on_nuki_message(void *ctx, const sdf_nuki_message_t *msg)
{
    // Handle encrypted responses if needed.
}

void start_pairing(uint32_t app_id)
{
    sdf_nuki_credentials_t creds = {0};

    sdf_nuki_client_init(&client, &creds,
                         ble_send, NULL,   /* encrypted send (USDIO) */
                         ble_send, NULL,   /* unencrypted send (GDIO) */
                         on_nuki_message, NULL);
    sdf_nuki_pairing_init(&pairing, &client, 0 /* App */, app_id, "SDF");

    sdf_nuki_pairing_start(&pairing);
}

void on_gdio_indication(const uint8_t *data, size_t len)
{
    // Feed unencrypted pairing messages from GDIO
    sdf_nuki_pairing_handle_unencrypted(&pairing, data, len);
}

void on_usdio_indication(const uint8_t *data, size_t len)
{
    // Feed encrypted pairing messages (Authorization-ID) from USDIO
    sdf_nuki_pairing_handle_encrypted(&pairing, data, len);

    if (pairing.state == SDF_NUKI_PAIRING_COMPLETE) {
        sdf_nuki_credentials_t creds;
        if (sdf_nuki_pairing_get_credentials(&pairing, &creds) == SDF_NUKI_RESULT_OK) {
            sdf_storage_nuki_save(creds.authorization_id, creds.shared_key);
        }
    }
}
```

**Notes**
- Pairing starts by writing a `Request Data (0x0001)` with `Public Key (0x0003)` to GDIO.
- The Authorization Data command is encrypted with the shared key and uses the pairing authorization id `0x7FFFFFFF`.
- After pairing, store `authorization_id` and `shared_key` in NVS for future encrypted commands.
