#ifndef URETICULUM_HAL_H
#define URETICULUM_HAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Must be monotonic and must not wrap within any single uptime. */
uint64_t ur_hal_millis(void);

/* Returns 0 on targets without a real-time clock. */
uint64_t ur_hal_unix_micros(void);

void ur_hal_delay_ms(uint32_t ms);

typedef struct ur_mutex ur_mutex_t;

ur_mutex_t* ur_hal_mutex_create(void);
void        ur_hal_mutex_destroy(ur_mutex_t* m);
void        ur_hal_mutex_lock(ur_mutex_t* m);
void        ur_hal_mutex_unlock(ur_mutex_t* m);

typedef void (*ur_task_fn)(void* arg);
typedef struct ur_task ur_task_t;

/* `stack_words` is in platform-native stack units (FreeRTOS: 32-bit words). */
ur_task_t* ur_hal_task_spawn(const char* name,
                             ur_task_fn  fn,
                             void*       arg,
                             size_t      stack_words,
                             int         priority);

void ur_hal_watchdog_feed(void);

/* Cryptographic-quality entropy. POSIX reads /dev/urandom. FreeRTOS firmware
 * is expected to back this with the MCU's TRNG (nRF: nrf_rng, STM32: RNG
 * peripheral, etc). Returns 0 on success, non-zero on failure. */
int ur_hal_random_bytes(uint8_t* buf, size_t len);

/* May be called from multiple tasks — implementation must serialize. */
void ur_hal_log_write(const char* line, size_t len);

#ifdef __cplusplus
}
#endif

#endif
