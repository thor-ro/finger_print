/*
 * Linux-target subset of sdf_ota (companion-device-health host tests).
 *
 * sdf_ota.c needs app_update and the flash partition APIs, which do not
 * exist for IDF_TARGET=linux, so only the target-independent sources are
 * registered there. The device-state health report reads the OTA state and
 * firmware version through sdf_ota_get_state()/sdf_ota_get_version(), so a
 * minimal, honest stand-in lives here: state IDLE, version from PROJECT_VER
 * (the same git-describe string the chip build embeds).
 */
#include "sdf_ota.h"

sdf_ota_state_t sdf_ota_get_state(void) { return SDF_OTA_STATE_IDLE; }

/* Same generated string the chip build embeds (firmware/cmake/version.cmake
 * configures version.c from PROJECT_VER before project()). */
extern const char sdf_ota_version_string[];

const char *sdf_ota_get_version(void) { return sdf_ota_version_string; }
