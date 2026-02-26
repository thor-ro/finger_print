/**
 * @file sdf_lock_guard.h
 * @brief Scoped semaphore lock guard using GCC cleanup attribute.
 *
 * Include this header in files that already include FreeRTOS semaphore
 * headers. It provides the SDF_LOCK_GUARD macro for automatic semaphore
 * release on scope exit.
 *
 * Usage:
 *   {
 *     SDF_LOCK_GUARD(guard, my_mutex, 100);
 *     if (guard.acquired != pdTRUE) return;
 *     // ... work under lock ...
 *   } // auto-released here
 */
#ifndef SDF_LOCK_GUARD_H
#define SDF_LOCK_GUARD_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
  SemaphoreHandle_t sem;
  BaseType_t acquired;
} sdf_lock_guard_t;

static inline void sdf_lock_guard_release(sdf_lock_guard_t *guard) {
  if (guard->acquired == pdTRUE && guard->sem != NULL) {
    xSemaphoreGive(guard->sem);
    guard->acquired = pdFALSE;
  }
}

/**
 * Declare a scoped lock guard variable. The semaphore is taken immediately
 * and released automatically when the variable goes out of scope via GCC
 * __attribute__((cleanup)).
 *
 * @param _name   Variable name for the guard.
 * @param _sem    SemaphoreHandle_t to take/give.
 * @param _timeout_ms  Timeout in milliseconds for the take.
 */
#define SDF_LOCK_GUARD(_name, _sem, _timeout_ms)                               \
  sdf_lock_guard_t _name __attribute__((cleanup(sdf_lock_guard_release))) = {  \
      .sem = (_sem),                                                           \
      .acquired = xSemaphoreTake((_sem), pdMS_TO_TICKS(_timeout_ms)),          \
  }

#endif /* SDF_LOCK_GUARD_H */
