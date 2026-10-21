#include "app_tests.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_crc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "psa/crypto.h"

static const char *TAG = "stress";

#define STRESS_CORE0_DONE          BIT0
#define STRESS_CORE1_DONE          BIT1
#define STRESS_INTERNAL_PER_CORE   (32U * 1024U)
#define STRESS_PSRAM_PER_CORE      (512U * 1024U)
/* Leave room for both worker stacks, the radio task and runtime allocations. */
#define STRESS_INTERNAL_RESERVE    (48U * 1024U)
#define STRESS_INTERNAL_MIN        (4U * 1024U)
#define STRESS_PSRAM_RESERVE       (512U * 1024U)
#define STRESS_HASH_BLOCK_SIZE     1024U

typedef struct {
    uint32_t core;
    uint8_t *internal;
    size_t internal_size;
    uint8_t *psram;
    size_t psram_size;
} stress_worker_t;

static EventGroupHandle_t s_done;
static TaskHandle_t s_core0_task;
static TaskHandle_t s_core1_task;
static stress_worker_t s_workers[2];
static atomic_bool s_stop;
static atomic_bool s_running;
static atomic_uint_least32_t s_digest;
static atomic_uint_least32_t s_errors;
static atomic_uint_least32_t s_iterations[2];
static atomic_uint_least32_t s_stack_low[2];
static int64_t s_started_us;

static uint8_t test_pattern(size_t index, uint32_t seed)
{
    uint32_t value = (uint32_t)index * 0x45D9F3Bu;
    value ^= seed + (uint32_t)(index >> 7);
    value ^= value >> 16;
    return (uint8_t)value;
}

static bool exercise_memory(uint8_t *buffer, size_t size, uint32_t seed,
                            uint32_t *crc)
{
    if (!buffer || size == 0) {
        return true;
    }
    for (size_t i = 0; i < size; i++) {
        buffer[i] = test_pattern(i, seed);
    }
    uint32_t value = esp_crc32_le(seed, buffer, size);
    for (size_t i = 0; i < size; i++) {
        if (buffer[i] != test_pattern(i, seed)) {
            return false;
        }
    }
    *crc ^= value;
    return true;
}

static bool exercise_cpu(uint32_t core, uint32_t round, uint32_t *digest)
{
    uint8_t block[STRESS_HASH_BLOCK_SIZE];
    uint8_t first[32];
    uint8_t second[32];
    size_t first_len = 0;
    size_t second_len = 0;
    uint32_t integer = 0x9E3779B9u ^ core ^ round;

    for (size_t i = 0; i < sizeof(block); i++) {
        integer = integer * 1664525u + 1013904223u;
        block[i] = (uint8_t)(integer >> 24);
    }
    if (psa_hash_compute(PSA_ALG_SHA_256, block, sizeof(block),
                         first, sizeof(first), &first_len) != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256, block, sizeof(block),
                         second, sizeof(second), &second_len) != PSA_SUCCESS ||
        first_len != sizeof(first) || second_len != sizeof(second) ||
        memcmp(first, second, sizeof(first)) != 0) {
        return false;
    }

    volatile float value = 1.0f + (float)core;
    for (uint32_t i = 1; i <= 4096; i++) {
        value = value * 1.000013f + (float)(i & 0x0FU) * 0.000031f;
        if (value > 1000.0f) {
            value *= 0.001f;
        }
    }
    if (!isfinite(value) || value <= 0.0f || value > 1000.0f) {
        return false;
    }

    *digest ^= esp_crc32_le(integer, first, sizeof(first));
    *digest ^= (uint32_t)(value * 100000.0f);
    return true;
}

static void update_stack_low_watermark(uint32_t core)
{
    const uint32_t current = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    uint32_t previous =
        atomic_load_explicit(&s_stack_low[core], memory_order_relaxed);
    while (current < previous &&
           !atomic_compare_exchange_weak_explicit(
               &s_stack_low[core], &previous, current,
               memory_order_relaxed, memory_order_relaxed)) {
    }
}

static void stress_task(void *arg)
{
    stress_worker_t *worker = (stress_worker_t *)arg;
    uint32_t round = 0;

    while (!atomic_load_explicit(&s_stop, memory_order_acquire)) {
        uint32_t digest = 0;
        bool valid = exercise_cpu(worker->core, round, &digest);
        valid = exercise_memory(worker->internal, worker->internal_size,
                                0x13579BDFu ^ round ^ worker->core,
                                &digest) && valid;
        valid = exercise_memory(worker->psram, worker->psram_size,
                                0x2468ACE0u ^ round ^ worker->core,
                                &digest) && valid;
        if (!valid) {
            atomic_fetch_add_explicit(&s_errors, 1, memory_order_relaxed);
            ESP_LOGE(TAG, "core%" PRIu32 " workload verification failed",
                     worker->core);
        }

        atomic_fetch_xor_explicit(&s_digest, digest, memory_order_relaxed);
        atomic_fetch_add_explicit(&s_iterations[worker->core], 1,
                                  memory_order_relaxed);
        round++;
        update_stack_low_watermark(worker->core);
        vTaskDelay(1);
    }

    update_stack_low_watermark(worker->core);
    xEventGroupSetBits(s_done, worker->core == 0
                                   ? STRESS_CORE0_DONE
                                   : STRESS_CORE1_DONE);
    vTaskDelete(NULL);
}

static size_t allocation_size(uint32_t caps, size_t desired, size_t reserve,
                              size_t worker_count)
{
    const size_t free_bytes = heap_caps_get_free_size(caps);
    const size_t largest = heap_caps_get_largest_free_block(caps);
    if (worker_count == 0 || free_bytes <= reserve) {
        return 0;
    }
    size_t allowed = (free_bytes - reserve) / worker_count;
    if (allowed > desired) {
        allowed = desired;
    }
    /* Do not assume that two allocations of the largest block will fit. */
    const size_t contiguous_per_worker = largest / worker_count;
    return allowed < contiguous_per_worker ? allowed : contiguous_per_worker;
}

static void free_worker_buffers(void)
{
    for (size_t i = 0; i < 2; i++) {
        heap_caps_free(s_workers[i].internal);
        heap_caps_free(s_workers[i].psram);
        s_workers[i].internal = NULL;
        s_workers[i].psram = NULL;
        s_workers[i].internal_size = 0;
        s_workers[i].psram_size = 0;
    }
}

static esp_err_t allocate_worker_buffers(size_t worker_count)
{
    const size_t internal_size = allocation_size(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
        STRESS_INTERNAL_PER_CORE, STRESS_INTERNAL_RESERVE, worker_count);
    const size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const size_t psram_size = psram_total == 0
                                  ? 0
                                  : allocation_size(
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                        STRESS_PSRAM_PER_CORE,
                                        STRESS_PSRAM_RESERVE, worker_count);

    ESP_LOGI(TAG,
             "buffer budget: internal free=%u largest=%u reserve=%u per_core=%u; "
             "PSRAM free=%u largest=%u per_core=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)STRESS_INTERNAL_RESERVE, (unsigned)internal_size,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)psram_size);
    if (internal_size < STRESS_INTERNAL_MIN || psram_size == 0) {
        ESP_LOGE(TAG, "insufficient RAM/PSRAM budget; full memory stress cannot run");
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < worker_count; i++) {
        s_workers[i].core = (uint32_t)i;
        s_workers[i].internal_size = internal_size;
        s_workers[i].psram_size = psram_size;
        s_workers[i].internal = heap_caps_malloc(
            internal_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s_workers[i].internal) {
            ESP_LOGE(TAG, "core%u internal buffer allocation failed: %u bytes",
                     (unsigned)i, (unsigned)internal_size);
            free_worker_buffers();
            return ESP_ERR_NO_MEM;
        }
        if (psram_size != 0) {
            s_workers[i].psram = heap_caps_malloc(
                psram_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!s_workers[i].psram) {
                ESP_LOGE(TAG, "core%u PSRAM buffer allocation failed: %u bytes",
                         (unsigned)i, (unsigned)psram_size);
                free_worker_buffers();
                return ESP_ERR_NO_MEM;
            }
        }
    }
    return ESP_OK;
}

esp_err_t app_stress_start(void)
{
    if (atomic_load_explicit(&s_running, memory_order_acquire)) {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(psa_crypto_init() == PSA_SUCCESS, ESP_FAIL, TAG,
                        "initialize PSA Crypto for stress failed");
    if (s_core0_task || s_core1_task) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_done) {
        s_done = xEventGroupCreate();
        ESP_RETURN_ON_FALSE(s_done, ESP_ERR_NO_MEM, TAG,
                            "create stress event group failed");
    }
    xEventGroupClearBits(s_done, STRESS_CORE0_DONE | STRESS_CORE1_DONE);

#if CONFIG_FREERTOS_UNICORE
    const size_t worker_count = 1;
#else
    const size_t worker_count = 2;
#endif
    ESP_RETURN_ON_ERROR(allocate_worker_buffers(worker_count), TAG,
                        "allocate bounded stress buffers failed");

    atomic_store_explicit(&s_stop, false, memory_order_release);
    atomic_store_explicit(&s_digest, 0, memory_order_relaxed);
    atomic_store_explicit(&s_errors, 0, memory_order_relaxed);
    for (size_t i = 0; i < 2; i++) {
        atomic_store_explicit(&s_iterations[i], 0, memory_order_relaxed);
        atomic_store_explicit(&s_stack_low[i], UINT32_MAX,
                              memory_order_relaxed);
    }

    if (xTaskCreatePinnedToCore(stress_task, "stress_cpu0", 6144,
                                &s_workers[0], 5, &s_core0_task, 0) != pdPASS) {
        free_worker_buffers();
        return ESP_ERR_NO_MEM;
    }

#if CONFIG_FREERTOS_UNICORE
    ESP_LOGW(TAG, "single-core build; CPU1 stress is NOT_TESTED");
#else
    if (xTaskCreatePinnedToCore(stress_task, "stress_cpu1", 6144,
                                &s_workers[1], 5, &s_core1_task, 1) != pdPASS) {
        atomic_store_explicit(&s_stop, true, memory_order_release);
        EventBits_t bits = xEventGroupWaitBits(
            s_done, STRESS_CORE0_DONE, pdFALSE, pdTRUE,
            pdMS_TO_TICKS(2000));
        if ((bits & STRESS_CORE0_DONE) != 0) {
            s_core0_task = NULL;
            free_worker_buffers();
        }
        return ESP_ERR_NO_MEM;
    }
#endif

    atomic_store_explicit(&s_running, true, memory_order_release);
    s_started_us = esp_timer_get_time();
    ESP_LOGI(TAG,
             "integer/FPU/PSA-SHA/RAM stress started: internal=%u bytes/core PSRAM=%u bytes/core",
             (unsigned)s_workers[0].internal_size,
             (unsigned)s_workers[0].psram_size);
    return ESP_OK;
}

esp_err_t app_stress_stop(void)
{
    if (!s_core0_task && !s_core1_task) {
        atomic_store_explicit(&s_running, false, memory_order_release);
        return ESP_OK;
    }

    atomic_store_explicit(&s_stop, true, memory_order_release);
    EventBits_t wait_bits = 0;
    if (s_core0_task) {
        wait_bits |= STRESS_CORE0_DONE;
    }
#if !CONFIG_FREERTOS_UNICORE
    if (s_core1_task) {
        wait_bits |= STRESS_CORE1_DONE;
    }
#endif
    EventBits_t bits = xEventGroupWaitBits(
        s_done, wait_bits, pdFALSE, pdTRUE, pdMS_TO_TICKS(4000));
    if ((bits & wait_bits) != wait_bits) {
        ESP_LOGE(TAG, "stress task stop timed out; buffers retained");
        return ESP_ERR_TIMEOUT;
    }

    s_core0_task = NULL;
    s_core1_task = NULL;
    free_worker_buffers();
    atomic_store_explicit(&s_running, false, memory_order_release);
    return ESP_OK;
}

uint32_t app_stress_get_digest(void)
{
    return atomic_load_explicit(&s_digest, memory_order_relaxed);
}

void app_stress_get_status(app_stress_status_t *status)
{
    if (!status) {
        return;
    }
    const uint32_t iterations0 =
        atomic_load_explicit(&s_iterations[0], memory_order_relaxed);
    const uint32_t iterations1 =
        atomic_load_explicit(&s_iterations[1], memory_order_relaxed);
    const int64_t elapsed_us = s_started_us > 0
                                   ? esp_timer_get_time() - s_started_us
                                   : 0;
    uint64_t internal_rate = 0;
    uint64_t psram_rate = 0;
    if (elapsed_us > 0) {
        const uint64_t internal_processed =
            ((uint64_t)iterations0 * s_workers[0].internal_size +
             (uint64_t)iterations1 * s_workers[1].internal_size) * 3U;
        const uint64_t psram_processed =
            ((uint64_t)iterations0 * s_workers[0].psram_size +
             (uint64_t)iterations1 * s_workers[1].psram_size) * 3U;
        internal_rate = internal_processed * 1000000U /
                        ((uint64_t)elapsed_us * 1024U);
        psram_rate = psram_processed * 1000000U /
                     ((uint64_t)elapsed_us * 1024U);
    }
    *status = (app_stress_status_t) {
        .running = atomic_load_explicit(&s_running, memory_order_acquire),
        .digest = atomic_load_explicit(&s_digest, memory_order_relaxed),
        .error_count = atomic_load_explicit(&s_errors, memory_order_relaxed),
        .iterations = {iterations0, iterations1},
        .internal_bytes = (uint32_t)(s_workers[0].internal_size +
                                     s_workers[1].internal_size),
        .psram_bytes = (uint32_t)(s_workers[0].psram_size +
                                  s_workers[1].psram_size),
        .internal_kib_s = internal_rate > UINT32_MAX
                              ? UINT32_MAX
                              : (uint32_t)internal_rate,
        .psram_kib_s = psram_rate > UINT32_MAX
                           ? UINT32_MAX
                           : (uint32_t)psram_rate,
        .uptime_ms = elapsed_us > 0
                         ? (uint32_t)((uint64_t)elapsed_us / 1000U)
                         : 0,
        .stack_low_watermark = {
            atomic_load_explicit(&s_stack_low[0], memory_order_relaxed),
            atomic_load_explicit(&s_stack_low[1], memory_order_relaxed),
        },
    };
    for (size_t i = 0; i < 2; i++) {
        if (status->stack_low_watermark[i] == UINT32_MAX) {
            status->stack_low_watermark[i] = 0;
        }
    }
}
