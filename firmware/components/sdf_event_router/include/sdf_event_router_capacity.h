#ifndef SDF_EVENT_ROUTER_CAPACITY_H
#define SDF_EVENT_ROUTER_CAPACITY_H

#include <assert.h>
#include <stdint.h>

/* Declared per-component subscription counts.
 * Every subsystem that subscribes to the event router declares its
 * exact subscription count here. */
#define SDF_EVENT_ROUTER_SUBS_APP             9
#define SDF_EVENT_ROUTER_SUBS_SERVICES_MATCH  3
#define SDF_EVENT_ROUTER_SUBS_SERVICES_ADMIN  4
#define SDF_EVENT_ROUTER_SUBS_SERVICES_ENROLL 3
#define SDF_EVENT_ROUTER_SUBS_BLE_COMPANION   3

#define SDF_EVENT_ROUTER_SUBS_DECLARED_TOTAL \
    (SDF_EVENT_ROUTER_SUBS_APP + \
     SDF_EVENT_ROUTER_SUBS_SERVICES_MATCH + \
     SDF_EVENT_ROUTER_SUBS_SERVICES_ADMIN + \
     SDF_EVENT_ROUTER_SUBS_SERVICES_ENROLL + \
     SDF_EVENT_ROUTER_SUBS_BLE_COMPANION) /* 22 */

/* Headroom above declared total. Set to 0: sizing the pool exactly at the
 * declared total ensures that any undeclared subscription immediately causes
 * an overt startup failure (rejected registration -> start() fails) rather
 * than silently consuming headroom. */
#define SDF_EVENT_ROUTER_SUBS_HEADROOM 0

#define SDF_EVENT_ROUTER_SUBSCRIBER_CAPACITY \
    (SDF_EVENT_ROUTER_SUBS_DECLARED_TOTAL + SDF_EVENT_ROUTER_SUBS_HEADROOM)

/* Sentinel value indicating end of subscriber index chain or empty head */
#define SDF_EVENT_ROUTER_SLOT_NONE 0xFF

static_assert(SDF_EVENT_ROUTER_SUBS_DECLARED_TOTAL <= SDF_EVENT_ROUTER_SUBSCRIBER_CAPACITY,
              "Declared subscriptions exceed router capacity");
static_assert(SDF_EVENT_ROUTER_SUBSCRIBER_CAPACITY < SDF_EVENT_ROUTER_SLOT_NONE,
              "Subscriber capacity cannot fit within 8-bit index (must be < 0xFF)");

#endif /* SDF_EVENT_ROUTER_CAPACITY_H */
