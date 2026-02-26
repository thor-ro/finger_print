/* Mock structs and types for linux tests */
#ifndef BLE_ADDR_RANDOM
#define BLE_ADDR_RANDOM 1
typedef struct {
    uint8_t type;
    uint8_t val[6];
} ble_addr_t;
#endif

typedef struct sdf_nuki_ble_transport_t sdf_nuki_ble_transport_t;
struct sdf_nuki_ble_transport_t { int dummy; };

typedef struct sdf_nuki_pairing_t sdf_nuki_pairing_t;
struct sdf_nuki_pairing_t { int state; };

/* Mock nuki functions missing on Linux */
#define SDF_NUKI_PAIRING_COMPLETE 3
#define SDF_NUKI_BLE_CHANNEL_USDIO 1
#define SDF_NUKI_BLE_CHANNEL_GDIO 2

typedef enum {
    SDF_NUKI_BLE_CHANNEL_UNKNOWN = 0,
    SDF_NUKI_BLE_CHANNEL_USDIO_ENUM = SDF_NUKI_BLE_CHANNEL_USDIO,
    SDF_NUKI_BLE_CHANNEL_GDIO_ENUM = SDF_NUKI_BLE_CHANNEL_GDIO
} sdf_nuki_ble_channel_t;

static inline bool sdf_nuki_ble_is_ready(sdf_nuki_ble_transport_t *t) { return true; }
static inline int sdf_nuki_ble_send(sdf_nuki_ble_transport_t *t, int channel, const uint8_t *data, size_t len) { return 0; }
static inline int sdf_nuki_pairing_init(sdf_nuki_pairing_t *p, sdf_nuki_client_t *c, uint8_t t, uint32_t aid, const char *n) { return 0; }
static inline int sdf_nuki_pairing_start(sdf_nuki_pairing_t *p) { return 0; }
static inline int sdf_nuki_pairing_handle_unencrypted(sdf_nuki_pairing_t *p, const uint8_t *d, size_t l) { return 0; }
static inline int sdf_nuki_pairing_handle_encrypted(sdf_nuki_pairing_t *p, const uint8_t *d, size_t l) { return 0; }
static inline int sdf_nuki_pairing_get_credentials(sdf_nuki_pairing_t *p, sdf_nuki_credentials_t *c) { return 0; }
static inline void sdf_nuki_ble_init(sdf_nuki_ble_transport_t *t, void *rx, void *rx_ctx, void *rdy, void *rdy_ctx) {}
static inline void sdf_nuki_ble_set_target_addr(sdf_nuki_ble_transport_t *t, const ble_addr_t *a) {}
static inline void sdf_nuki_ble_start(sdf_nuki_ble_transport_t *t) {}
