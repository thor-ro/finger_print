#ifndef SDF_NUKI_BLE_TRANSPORT_H
#define SDF_NUKI_BLE_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "host/ble_hs.h"

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

int sdf_nuki_ble_start(sdf_nuki_ble_transport_t *transport);

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
