#include "ureticulum/hal.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* Assumes FreeRTOS 10+ with configUSE_MUTEXES and
 * configSUPPORT_DYNAMIC_ALLOCATION enabled. Firmware overrides the weak
 * ur_hal_unix_micros_impl / ur_hal_log_write_impl / ur_hal_watchdog_feed
 * symbols to wire up RTC, UART/USB/RTT log sink, and the board watchdog. */

uint64_t ur_hal_millis(void) {
    return (uint64_t)xTaskGetTickCount() * (1000U / configTICK_RATE_HZ);
}

__attribute__((weak)) uint64_t ur_hal_unix_micros_impl(void) { return 0; }
uint64_t ur_hal_unix_micros(void) { return ur_hal_unix_micros_impl(); }

void ur_hal_delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

struct ur_mutex { SemaphoreHandle_t h; };

ur_mutex_t* ur_hal_mutex_create(void) {
    SemaphoreHandle_t h = xSemaphoreCreateMutex();
    if (!h) return NULL;
    ur_mutex_t* m = (ur_mutex_t*)pvPortMalloc(sizeof(*m));
    if (!m) { vSemaphoreDelete(h); return NULL; }
    m->h = h;
    return m;
}

void ur_hal_mutex_destroy(ur_mutex_t* m) {
    if (!m) return;
    vSemaphoreDelete(m->h);
    vPortFree(m);
}

void ur_hal_mutex_lock(ur_mutex_t* m)   { xSemaphoreTake(m->h, portMAX_DELAY); }
void ur_hal_mutex_unlock(ur_mutex_t* m) { xSemaphoreGive(m->h); }

struct ur_task { TaskHandle_t h; };

ur_task_t* ur_hal_task_spawn(const char* name,
                             ur_task_fn  fn,
                             void*       arg,
                             size_t      stack_words,
                             int         priority) {
    ur_task_t* t = (ur_task_t*)pvPortMalloc(sizeof(*t));
    if (!t) return NULL;
    BaseType_t ok = xTaskCreate((TaskFunction_t)fn,
                                name ? name : "ureticulum",
                                (uint16_t)stack_words,
                                arg,
                                (UBaseType_t)priority,
                                &t->h);
    if (ok != pdPASS) { vPortFree(t); return NULL; }
    return t;
}

__attribute__((weak)) void ur_hal_watchdog_feed(void) {}

__attribute__((weak)) int ur_hal_random_bytes(uint8_t* buf, size_t len) {
    /* Firmware MUST override this with a real TRNG. The default returns -1
     * so any caller that forgets to wire it up fails fast instead of using
     * predictable bytes. */
    (void)buf; (void)len;
    return -1;
}

__attribute__((weak)) void ur_hal_log_write_impl(const char* line, size_t len) {
    fwrite(line, 1, len, stdout);
}

void ur_hal_log_write(const char* line, size_t len) {
    ur_hal_log_write_impl(line, len);
}
