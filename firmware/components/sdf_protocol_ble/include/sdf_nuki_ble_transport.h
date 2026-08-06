#ifndef SDF_NUKI_BLE_TRANSPORT_H
#define SDF_NUKI_BLE_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#ifndef CONFIG_IDF_TARGET_LINUX
#include "host/ble_hs.h"
#else
/* NimBLE isn't available for IDF_TARGET_LINUX. sdf_nuki_ble_transport.c
 * (the real implementation using this type) isn't built for linux either
 * (see CMakeLists.txt); this stand-in only needs to keep the struct
 * layout below and this header's other consumers (e.g. sdf_app.h,
 * pulled in for declarations only) parseable on the host build. */
typedef struct {
  uint8_t type;
  uint8_t val[6];
} ble_addr_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SDF_NUKI_BLE_CHANNEL_GDIO = 0,
  SDF_NUKI_BLE_CHANNEL_USDIO = 1
} sdf_nuki_ble_channel_t;

typedef enum {
  SDF_NUKI_BLE_STATE_IDLE = 0,
  SDF_NUKI_BLE_STATE_SCANNING,
  SDF_NUKI_BLE_STATE_CONNECTING,
  SDF_NUKI_BLE_STATE_DISCOVERING,
  SDF_NUKI_BLE_STATE_READY
} sdf_nuki_ble_state_t;

typedef void (*sdf_nuki_ble_rx_cb)(void *ctx, sdf_nuki_ble_channel_t channel,
                                   const uint8_t *data, size_t len);

typedef void (*sdf_nuki_ble_ready_cb)(void *ctx);

/**
 * Hooks for GATT services that share the Nuki transport's NimBLE host.
 * The init hook runs after nimble_port_init() but before the host task starts.
 * The sync hook runs after the NimBLE host has synchronized with the controller.
 */
typedef int (*sdf_nuki_ble_server_init_cb)(void *ctx);
typedef void (*sdf_nuki_ble_server_sync_cb)(void *ctx);

typedef struct {
  ble_addr_t target_addr;
  bool has_target;

  uint8_t own_addr_type;
  uint16_t conn_handle;

  uint16_t pairing_svc_start;
  uint16_t pairing_svc_end;
  uint16_t keyturner_svc_start;
  uint16_t keyturner_svc_end;

  uint16_t gdio_handle;
  uint16_t gdio_cccd;
  uint16_t usdio_handle;
  uint16_t usdio_cccd;

  sdf_nuki_ble_state_t state;
  bool synced;
  bool start_requested;
  bool enabled;

  sdf_nuki_ble_rx_cb rx_cb;
  void *rx_ctx;
  sdf_nuki_ble_ready_cb ready_cb;
  void *ready_ctx;

  /* Reconnection backoff state */
  uint8_t reconnect_attempt;
  TimerHandle_t reconnect_timer;
  TimerHandle_t connect_timeout_timer;
} sdf_nuki_ble_transport_t;

int sdf_nuki_ble_init(sdf_nuki_ble_transport_t *transport,
                      sdf_nuki_ble_rx_cb rx_cb, void *rx_ctx,
                      sdf_nuki_ble_ready_cb ready_cb, void *ready_ctx);

/** Register a GATT service before calling sdf_nuki_ble_init(). */
int sdf_nuki_ble_register_server_service(sdf_nuki_ble_server_init_cb init_cb,
                                         sdf_nuki_ble_server_sync_cb sync_cb,
                                         void *ctx);

int sdf_nuki_ble_start(sdf_nuki_ble_transport_t *transport);

int sdf_nuki_ble_stop(sdf_nuki_ble_transport_t *transport);

int sdf_nuki_ble_set_enabled(sdf_nuki_ble_transport_t *transport, bool enabled);
bool sdf_nuki_ble_is_enabled(const sdf_nuki_ble_transport_t *transport);

bool sdf_nuki_ble_addr_is_empty(const ble_addr_t *addr);

int sdf_nuki_ble_set_target_addr(sdf_nuki_ble_transport_t *transport,
                                 const ble_addr_t *addr);

int sdf_nuki_ble_send(sdf_nuki_ble_transport_t *transport,
                      sdf_nuki_ble_channel_t channel, const uint8_t *data,
                      size_t len);

bool sdf_nuki_ble_is_ready(const sdf_nuki_ble_transport_t *transport);

void sdf_nuki_ble_reset_backoff(sdf_nuki_ble_transport_t *transport);

#ifdef __cplusplus
}
#endif

#endif /* SDF_NUKI_BLE_TRANSPORT_H */
