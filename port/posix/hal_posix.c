#define _POSIX_C_SOURCE 200809L

#include "ureticulum/hal.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static uint64_t s_boot_ms(void) {
    static uint64_t boot = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
    if (boot == 0) boot = now;
    return now - boot;
}

uint64_t ur_hal_millis(void) {
    return s_boot_ms();
}

uint64_t ur_hal_unix_micros(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

void ur_hal_delay_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { /* retry */ }
}

struct ur_mutex { pthread_mutex_t m; };

ur_mutex_t* ur_hal_mutex_create(void) {
    ur_mutex_t* m = (ur_mutex_t*)malloc(sizeof(*m));
    if (!m) return NULL;
    if (pthread_mutex_init(&m->m, NULL) != 0) { free(m); return NULL; }
    return m;
}

void ur_hal_mutex_destroy(ur_mutex_t* m) {
    if (!m) return;
    pthread_mutex_destroy(&m->m);
    free(m);
}

void ur_hal_mutex_lock(ur_mutex_t* m)   { pthread_mutex_lock(&m->m); }
void ur_hal_mutex_unlock(ur_mutex_t* m) { pthread_mutex_unlock(&m->m); }

struct ur_task { pthread_t tid; ur_task_fn fn; void* arg; };

static void* s_trampoline(void* p) {
    struct ur_task* t = (struct ur_task*)p;
    t->fn(t->arg);
    return NULL;
}

ur_task_t* ur_hal_task_spawn(const char* name,
                             ur_task_fn  fn,
                             void*       arg,
                             size_t      stack_words,
                             int         priority) {
    (void)name; (void)stack_words; (void)priority;
    struct ur_task* t = (struct ur_task*)malloc(sizeof(*t));
    if (!t) return NULL;
    t->fn = fn; t->arg = arg;
    if (pthread_create(&t->tid, NULL, s_trampoline, t) != 0) { free(t); return NULL; }
    pthread_detach(t->tid);
    return t;
}

void ur_hal_watchdog_feed(void) { /* no watchdog on POSIX */ }

int ur_hal_random_bytes(uint8_t* buf, size_t len) {
    if (buf == NULL || len == 0) return 0;
    FILE* f = fopen("/dev/urandom", "rb");
    if (f == NULL) return -1;
    size_t n = fread(buf, 1, len, f);
    fclose(f);
    return (n == len) ? 0 : -1;
}

static pthread_mutex_t s_log_mu = PTHREAD_MUTEX_INITIALIZER;

void ur_hal_log_write(const char* line, size_t len) {
    pthread_mutex_lock(&s_log_mu);
    fwrite(line, 1, len, stdout);
    fflush(stdout);
    pthread_mutex_unlock(&s_log_mu);
}
