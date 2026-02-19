#include "sdf_nuki_ble_transport.h"

#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "os/os_mbuf.h"

#include "sdf_protocol_ble.h"

#define SDF_NUKI_SCAN_ITVL 0x0010
#define SDF_NUKI_SCAN_WINDOW 0x0010

static const char *TAG = "sdf_nuki_ble";

static const ble_uuid_t *SDF_NUKI_PAIRING_SVC_UUID =
    BLE_UUID128_DECLARE(0xa9, 0x2e, 0xe1, 0x00, 0x55, 0x01, 0x11, 0xe4,
                        0x91, 0x6c, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66);

static const ble_uuid_t *SDF_NUKI_KEYTURNER_SVC_UUID =
    BLE_UUID128_DECLARE(0xa9, 0x2e, 0xe2, 0x00, 0x55, 0x01, 0x11, 0xe4,
                        0x91, 0x6c, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66);

static const ble_uuid_t *SDF_NUKI_GDIO_UUID =
    BLE_UUID128_DECLARE(0xa9, 0x2e, 0xe1, 0x01, 0x55, 0x01, 0x11, 0xe4,
                        0x91, 0x6c, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66);

static const ble_uuid_t *SDF_NUKI_USDIO_UUID =
    BLE_UUID128_DECLARE(0xa9, 0x2e, 0xe2, 0x02, 0x55, 0x01, 0x11, 0xe4,
                        0x91, 0x6c, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66);

static sdf_nuki_ble_transport_t *s_transport;
static ble_addr_t s_pending_addr;
static bool s_pending_connect;

typedef enum {
    SDF_NUKI_DISC_NONE = 0,
    SDF_NUKI_DISC_PAIRING_SVC,
    SDF_NUKI_DISC_PAIRING_CHR,
    SDF_NUKI_DISC_PAIRING_DSC,
    SDF_NUKI_DISC_KEYTURNER_SVC,
    SDF_NUKI_DISC_KEYTURNER_CHR,
    SDF_NUKI_DISC_KEYTURNER_DSC,
    SDF_NUKI_DISC_SUBSCRIBE_GDIO,
    SDF_NUKI_DISC_SUBSCRIBE_USDIO
} sdf_nuki_disc_state_t;

static sdf_nuki_disc_state_t s_disc_state;

static void sdf_nuki_ble_start_scan(void);
static int sdf_nuki_ble_gap_event(struct ble_gap_event *event, void *arg);
static int sdf_nuki_ble_on_mtu(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    uint16_t mtu,
    void *arg);
static int sdf_nuki_ble_disc_svc_cb(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    const struct ble_gatt_svc *service,
    void *arg);
static int sdf_nuki_ble_disc_chr_cb(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    const struct ble_gatt_chr *chr,
    void *arg);
static int sdf_nuki_ble_disc_dsc_cb(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    uint16_t chr_val_handle,
    const struct ble_gatt_dsc *dsc,
    void *arg);
static int sdf_nuki_ble_on_subscribe_gdio(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr,
    void *arg);
static int sdf_nuki_ble_on_subscribe_usdio(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr,
    void *arg);

static void sdf_nuki_ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset; reason=%d", reason);
}

static void sdf_nuki_ble_on_sync(void)
{
    if (s_transport == NULL) {
        return;
    }

    ble_hs_id_infer_auto(0, &s_transport->own_addr_type);
    s_transport->synced = true;

    if (s_transport->start_requested) {
        sdf_nuki_ble_start_scan();
    }
}

static void sdf_nuki_ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static bool sdf_nuki_ble_addr_match(const ble_addr_t *a, const ble_addr_t *b)
{
    return a->type == b->type && memcmp(a->val, b->val, sizeof(a->val)) == 0;
}

static bool sdf_nuki_ble_adv_has_nuki_service(const struct ble_hs_adv_fields *fields)
{
    for (int i = 0; i < fields->num_uuids128; ++i) {
        const ble_uuid_t *uuid = &fields->uuids128[i].u;
        if (ble_uuid_cmp(uuid, SDF_NUKI_PAIRING_SVC_UUID) == 0 ||
            ble_uuid_cmp(uuid, SDF_NUKI_KEYTURNER_SVC_UUID) == 0) {
            return true;
        }
    }
    return false;
}

static int sdf_nuki_ble_connect(const ble_addr_t *addr)
{
    s_transport->state = SDF_NUKI_BLE_STATE_CONNECTING;

    return ble_gap_connect(
        s_transport->own_addr_type,
        addr,
        BLE_HS_FOREVER,
        NULL,
        sdf_nuki_ble_gap_event,
        NULL);
}

static int sdf_nuki_ble_start_pairing_service_discovery(void);
static int sdf_nuki_ble_start_keyturner_service_discovery(void);

static int sdf_nuki_ble_on_mtu(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    uint16_t mtu,
    void *arg)
{
    (void)conn_handle;
    (void)arg;

    if (error->status != 0) {
        ESP_LOGW(TAG, "MTU exchange failed; status=%d", error->status);
    } else {
        ESP_LOGI(TAG, "MTU exchange complete: %u", mtu);
    }

    return sdf_nuki_ble_start_pairing_service_discovery();
}

static int sdf_nuki_ble_disc_svc_cb(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    const struct ble_gatt_svc *service,
    void *arg)
{
    if (error->status == 0 && service != NULL) {
        if (s_disc_state == SDF_NUKI_DISC_PAIRING_SVC) {
            s_transport->pairing_svc_start = service->start_handle;
            s_transport->pairing_svc_end = service->end_handle;
        } else if (s_disc_state == SDF_NUKI_DISC_KEYTURNER_SVC) {
            s_transport->keyturner_svc_start = service->start_handle;
            s_transport->keyturner_svc_end = service->end_handle;
        }
        return 0;
    }

    if (error->status != BLE_HS_EDONE) {
        ESP_LOGE(TAG, "Service discovery failed; status=%d", error->status);
        return error->status;
    }

    if (s_disc_state == SDF_NUKI_DISC_PAIRING_SVC) {
        if (s_transport->pairing_svc_start == 0) {
            ESP_LOGE(TAG, "Pairing service not found");
            return BLE_HS_EAPP;
        }

        s_disc_state = SDF_NUKI_DISC_PAIRING_CHR;
        return ble_gattc_disc_all_chrs(
            s_transport->conn_handle,
            s_transport->pairing_svc_start,
            s_transport->pairing_svc_end,
            sdf_nuki_ble_disc_chr_cb,
            arg);
    }

    if (s_disc_state == SDF_NUKI_DISC_KEYTURNER_SVC) {
        if (s_transport->keyturner_svc_start == 0) {
            ESP_LOGE(TAG, "Keyturner service not found");
            return BLE_HS_EAPP;
        }

        s_disc_state = SDF_NUKI_DISC_KEYTURNER_CHR;
        return ble_gattc_disc_all_chrs(
            s_transport->conn_handle,
            s_transport->keyturner_svc_start,
            s_transport->keyturner_svc_end,
            sdf_nuki_ble_disc_chr_cb,
            arg);
    }

    return 0;
}

static int sdf_nuki_ble_disc_chr_cb(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    const struct ble_gatt_chr *chr,
    void *arg)
{
    if (error->status == 0 && chr != NULL) {
        if (s_disc_state == SDF_NUKI_DISC_PAIRING_CHR) {
            if (ble_uuid_cmp(&chr->uuid.u, SDF_NUKI_GDIO_UUID) == 0) {
                s_transport->gdio_handle = chr->val_handle;
            }
        } else if (s_disc_state == SDF_NUKI_DISC_KEYTURNER_CHR) {
            if (ble_uuid_cmp(&chr->uuid.u, SDF_NUKI_USDIO_UUID) == 0) {
                s_transport->usdio_handle = chr->val_handle;
            }
        }
        return 0;
    }

    if (error->status != BLE_HS_EDONE) {
        ESP_LOGE(TAG, "Characteristic discovery failed; status=%d", error->status);
        return error->status;
    }

    if (s_disc_state == SDF_NUKI_DISC_PAIRING_CHR) {
        if (s_transport->gdio_handle == 0) {
            ESP_LOGE(TAG, "GDIO characteristic not found");
            return BLE_HS_EAPP;
        }

        s_disc_state = SDF_NUKI_DISC_PAIRING_DSC;
        return ble_gattc_disc_all_dscs(
            s_transport->conn_handle,
            (uint16_t)(s_transport->gdio_handle + 1),
            s_transport->pairing_svc_end,
            sdf_nuki_ble_disc_dsc_cb,
            arg);
    }

    if (s_disc_state == SDF_NUKI_DISC_KEYTURNER_CHR) {
        if (s_transport->usdio_handle == 0) {
            ESP_LOGE(TAG, "USDIO characteristic not found");
            return BLE_HS_EAPP;
        }

        s_disc_state = SDF_NUKI_DISC_KEYTURNER_DSC;
        return ble_gattc_disc_all_dscs(
            s_transport->conn_handle,
            (uint16_t)(s_transport->usdio_handle + 1),
            s_transport->keyturner_svc_end,
            sdf_nuki_ble_disc_dsc_cb,
            arg);
    }

    return 0;
}

static int sdf_nuki_ble_disc_dsc_cb(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    uint16_t chr_val_handle,
    const struct ble_gatt_dsc *dsc,
    void *arg)
{
    (void)chr_val_handle;

    if (error->status == 0 && dsc != NULL) {
        if (ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
            if (s_disc_state == SDF_NUKI_DISC_PAIRING_DSC) {
                s_transport->gdio_cccd = dsc->handle;
            } else if (s_disc_state == SDF_NUKI_DISC_KEYTURNER_DSC) {
                s_transport->usdio_cccd = dsc->handle;
            }
        }
        return 0;
    }

    if (error->status != BLE_HS_EDONE) {
        ESP_LOGE(TAG, "Descriptor discovery failed; status=%d", error->status);
        return error->status;
    }

    if (s_disc_state == SDF_NUKI_DISC_PAIRING_DSC) {
        if (s_transport->gdio_cccd == 0) {
            ESP_LOGE(TAG, "GDIO CCCD not found");
            return BLE_HS_EAPP;
        }

        return sdf_nuki_ble_start_keyturner_service_discovery();
    }

    if (s_disc_state == SDF_NUKI_DISC_KEYTURNER_DSC) {
        if (s_transport->usdio_cccd == 0) {
            ESP_LOGE(TAG, "USDIO CCCD not found");
            return BLE_HS_EAPP;
        }

        uint8_t value[2] = {0x02, 0x00};
        return ble_gattc_write_flat(
            s_transport->conn_handle,
            s_transport->gdio_cccd,
            value,
            sizeof(value),
            sdf_nuki_ble_on_subscribe_gdio,
            arg);
    }

    return 0;
}

static int sdf_nuki_ble_on_subscribe_gdio(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr,
    void *arg)
{
    if (error->status != 0) {
        ESP_LOGE(TAG, "GDIO subscribe failed; status=%d", error->status);
        return error->status;
    }

    uint8_t value[2] = {0x02, 0x00};
    s_disc_state = SDF_NUKI_DISC_SUBSCRIBE_USDIO;
    return ble_gattc_write_flat(
        conn_handle,
        s_transport->usdio_cccd,
        value,
        sizeof(value),
        sdf_nuki_ble_on_subscribe_usdio,
        arg);
}

static int sdf_nuki_ble_on_subscribe_usdio(
    uint16_t conn_handle,
    const struct ble_gatt_error *error,
    struct ble_gatt_attr *attr,
    void *arg)
{
    if (error->status != 0) {
        ESP_LOGE(TAG, "USDIO subscribe failed; status=%d", error->status);
        return error->status;
    }

    s_transport->state = SDF_NUKI_BLE_STATE_READY;
    if (s_transport->ready_cb != NULL) {
        s_transport->ready_cb(s_transport->ready_ctx);
    }
    return 0;
}

static int sdf_nuki_ble_start_pairing_service_discovery(void)
{
    s_disc_state = SDF_NUKI_DISC_PAIRING_SVC;
    return ble_gattc_disc_svc_by_uuid(
        s_transport->conn_handle,
        SDF_NUKI_PAIRING_SVC_UUID,
        sdf_nuki_ble_disc_svc_cb,
        s_transport);
}

static int sdf_nuki_ble_start_keyturner_service_discovery(void)
{
    s_disc_state = SDF_NUKI_DISC_KEYTURNER_SVC;
    return ble_gattc_disc_svc_by_uuid(
        s_transport->conn_handle,
        SDF_NUKI_KEYTURNER_SVC_UUID,
        sdf_nuki_ble_disc_svc_cb,
        s_transport);
}

static void sdf_nuki_ble_start_scan(void)
{
    struct ble_gap_disc_params params;
    memset(&params, 0, sizeof(params));
    params.itvl = SDF_NUKI_SCAN_ITVL;
    params.window = SDF_NUKI_SCAN_WINDOW;
    params.passive = 0;
    params.filter_duplicates = 1;

    s_transport->state = SDF_NUKI_BLE_STATE_SCANNING;

    int rc = ble_gap_disc(
        s_transport->own_addr_type,
        BLE_HS_FOREVER,
        &params,
        sdf_nuki_ble_gap_event,
        NULL);

    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start scan; rc=%d", rc);
    }
}

static void sdf_nuki_ble_handle_adv(const struct ble_gap_event *event)
{
    if (s_transport->state != SDF_NUKI_BLE_STATE_SCANNING) {
        return;
    }

    if (s_transport->has_target) {
        if (sdf_nuki_ble_addr_match(&event->disc.addr, &s_transport->target_addr)) {
            s_pending_addr = event->disc.addr;
            s_pending_connect = true;
            ble_gap_disc_cancel();
        }
        return;
    }

    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0) {
        return;
    }

    if (sdf_nuki_ble_adv_has_nuki_service(&fields)) {
        s_pending_addr = event->disc.addr;
        s_pending_connect = true;
        ble_gap_disc_cancel();
    }
}

static void sdf_nuki_ble_handle_notify(const struct ble_gap_event *event)
{
    if (s_transport->state != SDF_NUKI_BLE_STATE_READY) {
        return;
    }

    uint16_t handle = event->notify_rx.attr_handle;
    sdf_nuki_ble_channel_t channel;

    if (handle == s_transport->gdio_handle) {
        channel = SDF_NUKI_BLE_CHANNEL_GDIO;
    } else if (handle == s_transport->usdio_handle) {
        channel = SDF_NUKI_BLE_CHANNEL_USDIO;
    } else {
        return;
    }

    uint8_t buffer[SDF_NUKI_MAX_MESSAGE];
    size_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
    if (len > sizeof(buffer)) {
        ESP_LOGW(TAG, "Notification too large: %u", (unsigned)len);
        return;
    }

    uint16_t copied_len = 0;
    if (ble_hs_mbuf_to_flat(
            event->notify_rx.om,
            buffer,
            (uint16_t)len,
            &copied_len) != 0) {
        return;
    }
    len = copied_len;

    if (s_transport->rx_cb != NULL) {
        s_transport->rx_cb(s_transport->rx_ctx, channel, buffer, len);
    }
}

static int sdf_nuki_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        sdf_nuki_ble_handle_adv(event);
        return 0;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (s_pending_connect) {
            s_pending_connect = false;
            sdf_nuki_ble_connect(&s_pending_addr);
        }
        return 0;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            ESP_LOGW(TAG, "Connect failed; status=%d", event->connect.status);
            sdf_nuki_ble_start_scan();
            return 0;
        }
        s_transport->conn_handle = event->connect.conn_handle;
        s_transport->state = SDF_NUKI_BLE_STATE_DISCOVERING;
        s_transport->gdio_handle = 0;
        s_transport->usdio_handle = 0;
        s_transport->gdio_cccd = 0;
        s_transport->usdio_cccd = 0;
        s_transport->pairing_svc_start = 0;
        s_transport->pairing_svc_end = 0;
        s_transport->keyturner_svc_start = 0;
        s_transport->keyturner_svc_end = 0;
        {
            int rc = ble_gattc_exchange_mtu(
                s_transport->conn_handle,
                sdf_nuki_ble_on_mtu,
                s_transport);
            if (rc != 0) {
                ESP_LOGW(TAG, "MTU exchange request failed; rc=%d", rc);
                return sdf_nuki_ble_start_pairing_service_discovery();
            }
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        s_transport->conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_transport->state = SDF_NUKI_BLE_STATE_IDLE;
        s_transport->gdio_handle = 0;
        s_transport->usdio_handle = 0;
        s_transport->gdio_cccd = 0;
        s_transport->usdio_cccd = 0;
        sdf_nuki_ble_start_scan();
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX:
        sdf_nuki_ble_handle_notify(event);
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: %u", event->mtu.value);
        return 0;
    default:
        return 0;
    }
}

int sdf_nuki_ble_init(
    sdf_nuki_ble_transport_t *transport,
    sdf_nuki_ble_rx_cb rx_cb,
    void *rx_ctx,
    sdf_nuki_ble_ready_cb ready_cb,
    void *ready_ctx)
{
    if (transport == NULL) {
        return -1;
    }

    memset(transport, 0, sizeof(*transport));
    transport->rx_cb = rx_cb;
    transport->rx_ctx = rx_ctx;
    transport->ready_cb = ready_cb;
    transport->ready_ctx = ready_ctx;
    transport->conn_handle = BLE_HS_CONN_HANDLE_NONE;

    s_transport = transport;

    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return rc;
    }

    ble_hs_cfg.reset_cb = sdf_nuki_ble_on_reset;
    ble_hs_cfg.sync_cb = sdf_nuki_ble_on_sync;

    nimble_port_freertos_init(sdf_nuki_ble_host_task);

    return 0;
}

int sdf_nuki_ble_start(sdf_nuki_ble_transport_t *transport)
{
    if (transport == NULL) {
        return -1;
    }

    transport->start_requested = true;
    if (transport->synced) {
        sdf_nuki_ble_start_scan();
    }

    return 0;
}

int sdf_nuki_ble_set_target_addr(
    sdf_nuki_ble_transport_t *transport,
    const ble_addr_t *addr)
{
    if (transport == NULL || addr == NULL) {
        return -1;
    }

    transport->target_addr = *addr;
    transport->has_target = true;
    return 0;
}

int sdf_nuki_ble_send(
    sdf_nuki_ble_transport_t *transport,
    sdf_nuki_ble_channel_t channel,
    const uint8_t *data,
    size_t len)
{
    if (transport == NULL || data == NULL || len == 0) {
        return -1;
    }

    if (transport->state != SDF_NUKI_BLE_STATE_READY) {
        return -1;
    }

    uint16_t handle = 0;
    if (channel == SDF_NUKI_BLE_CHANNEL_GDIO) {
        handle = transport->gdio_handle;
    } else if (channel == SDF_NUKI_BLE_CHANNEL_USDIO) {
        handle = transport->usdio_handle;
    }

    if (handle == 0) {
        return -1;
    }

    uint16_t mtu = ble_att_mtu(transport->conn_handle);
    uint16_t max_payload = mtu > 3 ? (uint16_t)(mtu - 3) : 20;

    if (len <= max_payload) {
        return ble_gattc_write_flat(
            transport->conn_handle,
            handle,
            data,
            len,
            NULL,
            NULL);
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        return -1;
    }

    int rc = ble_gattc_write_long(
        transport->conn_handle,
        handle,
        0,
        om,
        NULL,
        NULL);
    if (rc != 0) {
        os_mbuf_free_chain(om);
    }
    return rc;
}

bool sdf_nuki_ble_is_ready(const sdf_nuki_ble_transport_t *transport)
{
    return transport != NULL && transport->state == SDF_NUKI_BLE_STATE_READY;
}
