/**
 * @file sdf_mock_linux_zigbee.h
 * @brief Test-only view of the Linux host mock's recorded attribute state.
 *
 * The target implementation applies attribute updates asynchronously on a task
 * it owns, so nothing is readable synchronously after an update call returns.
 * The host mock has no such task; these accessors expose the values an apply
 * would push, which is the observable half of the contract documented in
 * sdf_protocol_zigbee.h. Tests assert convergence to the latest recorded value
 * rather than that any individual write happened.
 */
#ifndef SDF_MOCK_LINUX_ZIGBEE_H
#define SDF_MOCK_LINUX_ZIGBEE_H

#include "sdkconfig.h"

#ifdef CONFIG_IDF_TARGET_LINUX

#include <stdint.h>

uint8_t sdf_protocol_zigbee_mock_get_lock_state(void);
uint8_t sdf_protocol_zigbee_mock_get_battery_percent_remaining(void);
uint16_t sdf_protocol_zigbee_mock_get_alarm_mask(void);

/** @return the recorded user list, or NULL if none has been accepted. */
const char *sdf_protocol_zigbee_mock_get_user_list(void);

void sdf_protocol_zigbee_mock_reset(void);

#endif /* CONFIG_IDF_TARGET_LINUX */

#endif /* SDF_MOCK_LINUX_ZIGBEE_H */
