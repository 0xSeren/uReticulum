#include "ureticulum/hal.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

/* ESP-IDF HAL backend. ESP-IDF wraps FreeRTOS so the primitive set is
 * essentially identical to the bare-FreeRTOS HAL — just different include
 * paths plus the IDF-specific helpers (esp_random, esp_log, esp_timer). */

uint64_t ur_hal_millis(void) {
    return esp_timer_get_time() / 1000ULL;
}

uint64_t ur_hal_unix_micros(void) {
    /* Wall clock requires SNTP / RTC sync. Until firmware sets it up,
     * report 0 so RNS::Utilities::OS::time() falls back to monotonic. */
    return 0;
}

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

struct ur_recursive_mutex { SemaphoreHandle_t h; };

ur_recursive_mutex_t* ur_hal_recursive_mutex_create(void) {
    SemaphoreHandle_t h = xSemaphoreCreateRecursiveMutex();
    if (!h) return NULL;
    ur_recursive_mutex_t* m = (ur_recursive_mutex_t*)pvPortMalloc(sizeof(*m));
    if (!m) { vSemaphoreDelete(h); return NULL; }
    m->h = h;
    return m;
}

void ur_hal_recursive_mutex_destroy(ur_recursive_mutex_t* m) {
    if (!m) return;
    vSemaphoreDelete(m->h);
    vPortFree(m);
}

void ur_hal_recursive_mutex_lock(ur_recursive_mutex_t* m)   { xSemaphoreTakeRecursive(m->h, portMAX_DELAY); }
void ur_hal_recursive_mutex_unlock(ur_recursive_mutex_t* m) { xSemaphoreGiveRecursive(m->h); }

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

void ur_hal_watchdog_feed(void) {
    /* The IDF task watchdog is configured per-task; firmware that enables
     * it should call esp_task_wdt_reset() in the same task as the
     * Reticulum loop. We leave this as a no-op so the default build
     * doesn't fight a watchdog the user didn't configure. */
}

int ur_hal_random_bytes(uint8_t* buf, size_t len) {
    if (!buf || len == 0) return 0;
    esp_fill_random(buf, len);
    return 0;
}

void ur_hal_log_write(const char* line, size_t len) {
    /* Strip the trailing newline if present — esp_log adds its own. */
    if (len > 0 && line[len - 1] == '\n') len--;
    /* Cast away const for printf("%.*s") which doesn't accept const len. */
    ESP_LOGI("ureticulum", "%.*s", (int)len, line);
}
