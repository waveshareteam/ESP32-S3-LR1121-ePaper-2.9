#include "app_tests.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_chip_info.h"
#include "esp_crc.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "psa/crypto.h"

static const char *TAG = "system_test";

#define INTERNAL_TEST_BYTES (64U * 1024U)
#define PSRAM_TEST_BYTES     (1024U * 1024U)
#define FLASH_CHUNK_BYTES    (16U * 1024U)
#define MEMORY_TEST_MIN      (4U * 1024U)
#define MEMORY_TEST_RESERVE  (48U * 1024U)
#define MEMORY_TEST_MARGIN   1024U

typedef struct {
    bool tested;
    bool passed;
    uint32_t bytes;
    uint32_t crc;
    uint32_t throughput_kib_s;
} memory_test_result_t;

static uint8_t memory_pattern(size_t index)
{
    uint32_t value = (uint32_t)index * 0x45D9F3BU;
    value ^= value >> 13;
    value ^= 0xA55A3CC3U;
    return (uint8_t)value;
}

static memory_test_result_t memory_test(uint32_t caps, size_t requested)
{
    memory_test_result_t result = {0};
    const size_t free_bytes = heap_caps_get_free_size(caps);
    const size_t largest = heap_caps_get_largest_free_block(caps);
    const size_t reserve = MEMORY_TEST_RESERVE + MEMORY_TEST_MARGIN;
    size_t bytes = free_bytes > reserve ? free_bytes - reserve : 0;
    if (bytes > requested) bytes = requested;
    if (bytes > largest) bytes = largest;
    bytes &= ~(size_t)255U;
    uint8_t *buffer = NULL;
    while (bytes >= MEMORY_TEST_MIN) {
        buffer = heap_caps_malloc(bytes, caps);
        if (buffer) break;
        if (bytes == MEMORY_TEST_MIN) break;
        bytes = (bytes / 2U) & ~(size_t)255U;
        if (bytes < MEMORY_TEST_MIN) bytes = MEMORY_TEST_MIN;
    }
    if (!buffer) {
        ESP_LOGE(TAG, "memory test NOT_TESTED: caps=0x%x requested=%u free=%u largest=%u",
                 (unsigned)caps, (unsigned)requested,
                 (unsigned)free_bytes, (unsigned)largest);
        return result;
    }
    result.tested = true;
    result.bytes = (uint32_t)bytes;
    ESP_LOGI(TAG, "memory test: caps=0x%x requested=%u actual=%u",
             (unsigned)caps, (unsigned)requested, (unsigned)bytes);

    const int64_t start = esp_timer_get_time();
    for (size_t i = 0; i < bytes; ++i) {
        buffer[i] = memory_pattern(i);
    }
    bool valid = true;
    for (size_t i = 0; i < bytes; ++i) {
        if (buffer[i] != memory_pattern(i)) {
            valid = false;
            break;
        }
    }
    result.crc = esp_crc32_le(0, buffer, bytes);
    const int64_t elapsed_us = esp_timer_get_time() - start;
    if (elapsed_us > 0) {
        result.throughput_kib_s = (uint32_t)(
            ((uint64_t)bytes * 2U * 1000000U) /
            ((uint64_t)elapsed_us * 1024U));
    }
    result.passed = valid;
    heap_caps_free(buffer);
    return result;
}

static bool flash_read_test(uint32_t flash_size, uint32_t *crc,
                            uint32_t *throughput_kib_s)
{
    uint8_t *buffer = heap_caps_malloc(
        FLASH_CHUNK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buffer) {
        return false;
    }

    uint32_t aggregate = 0;
    const int64_t start = esp_timer_get_time();
    for (uint32_t offset = 0; offset < flash_size;
         offset += FLASH_CHUNK_BYTES) {
        const uint32_t remaining = flash_size - offset;
        const size_t chunk = remaining < FLASH_CHUNK_BYTES
                                 ? remaining
                                 : FLASH_CHUNK_BYTES;
        if (esp_flash_read(NULL, buffer, offset, chunk) != ESP_OK) {
            heap_caps_free(buffer);
            return false;
        }
        aggregate = esp_crc32_le(aggregate, buffer, chunk);
        if ((offset & ((256U * 1024U) - 1U)) == 0U) {
            vTaskDelay(1);
        }
    }
    const int64_t elapsed_us = esp_timer_get_time() - start;
    heap_caps_free(buffer);
    *crc = aggregate;
    if (elapsed_us > 0) {
        *throughput_kib_s = (uint32_t)(
            ((uint64_t)flash_size * 1000000U) /
            ((uint64_t)elapsed_us * 1024U));
    }
    return true;
}

static bool aes_known_answer_test(void)
{
    static const uint8_t key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
    };
    static const uint8_t plain[16] = {
        0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d,
        0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34,
    };
    static const uint8_t expected[16] = {
        0x39, 0x25, 0x84, 0x1d, 0x02, 0xdc, 0x09, 0xfb,
        0xdc, 0x11, 0x85, 0x97, 0x19, 0x6a, 0x0b, 0x32,
    };
    uint8_t output[16] = {0};
    size_t output_length = 0;
    psa_key_id_t key_id = PSA_KEY_ID_NULL;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECB_NO_PADDING);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128);

    psa_status_t status = psa_crypto_init();
    if (status == PSA_SUCCESS) {
        status = psa_import_key(&attributes, key, sizeof(key), &key_id);
    }
    if (status == PSA_SUCCESS) {
        status = psa_cipher_encrypt(
            key_id, PSA_ALG_ECB_NO_PADDING, plain, sizeof(plain),
            output, sizeof(output), &output_length);
    }
    psa_reset_key_attributes(&attributes);
    if (key_id != PSA_KEY_ID_NULL) {
        const psa_status_t destroy_status = psa_destroy_key(key_id);
        if (status == PSA_SUCCESS) {
            status = destroy_status;
        }
    }
    return status == PSA_SUCCESS && output_length == sizeof(output) &&
           memcmp(output, expected, sizeof(output)) == 0;
}

static bool sha256_known_answer_test(void)
{
    static const uint8_t expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    uint8_t digest[32] = {0};
    const uint8_t message[] = {'a', 'b', 'c'};
    size_t digest_length = 0;
    return psa_crypto_init() == PSA_SUCCESS &&
           psa_hash_compute(PSA_ALG_SHA_256, message, sizeof(message),
                            digest, sizeof(digest),
                            &digest_length) == PSA_SUCCESS &&
           digest_length == sizeof(digest) &&
           memcmp(digest, expected, sizeof(digest)) == 0;
}

static bool rng_interface_test(void)
{
    uint8_t random_data[64] = {0};
    esp_fill_random(random_data, sizeof(random_data));
    bool differs = false;
    for (size_t i = 1; i < sizeof(random_data); ++i) {
        if (random_data[i] != random_data[0]) {
            differs = true;
            break;
        }
    }
    return differs;
}

static atomic_bool s_timer_fired;

static void timer_callback(void *arg)
{
    (void)arg;
    atomic_store_explicit(&s_timer_fired, true, memory_order_release);
}

static bool timer_test(void)
{
    esp_timer_handle_t timer = NULL;
    const esp_timer_create_args_t args = {
        .callback = timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "self_test_timer",
        .skip_unhandled_events = false,
    };
    if (esp_timer_create(&args, &timer) != ESP_OK) {
        return false;
    }
    atomic_store_explicit(&s_timer_fired, false, memory_order_release);
    esp_err_t ret = esp_timer_start_once(timer, 10000);
    vTaskDelay(pdMS_TO_TICKS(30));
    const bool fired =
        atomic_load_explicit(&s_timer_fired, memory_order_acquire);
    const esp_err_t delete_ret = esp_timer_delete(timer);
    return ret == ESP_OK && delete_ret == ESP_OK && fired;
}

esp_err_t test_system_run(app_test_result_t *result)
{
    esp_chip_info_t chip = {0};
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    const bool flash_size_ok =
        esp_flash_get_size(NULL, &flash_size) == ESP_OK && flash_size > 0;
    const uint32_t cpu_mhz = esp_rom_get_cpu_ticks_per_us();
    const uint32_t internal_total =
        heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t internal_free =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t internal_min =
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint8_t mac[6] = {0};
    const bool mac_ok = esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK;
    const uint32_t wake_causes = esp_sleep_get_wakeup_causes();

    ESP_LOGI(TAG,
             "chip=%s rev=%d cores=%d features=0x%" PRIx32
             " cpu=%" PRIu32 "MHz IDF=%s",
             CONFIG_IDF_TARGET, chip.revision, chip.cores, chip.features,
             cpu_mhz, esp_get_idf_version());
    ESP_LOGI(TAG,
             "flash=%" PRIu32 "KB internal total/free/min=%" PRIu32 "/%"
             PRIu32 "/%" PRIu32 " PSRAM total/free=%" PRIu32 "/%" PRIu32,
             flash_size / 1024U, internal_total, internal_free, internal_min,
             psram_total, psram_free);
    ESP_LOGI(TAG,
             "MAC=%02X:%02X:%02X:%02X:%02X:%02X reset=%d wake_bitmap=0x%08"
             PRIX32,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             esp_reset_reason(), wake_causes);

    const memory_test_result_t internal = memory_test(
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, INTERNAL_TEST_BYTES);
    memory_test_result_t psram = {0};
    if (psram_total > 0) {
        const size_t psram_bytes =
            psram_free > PSRAM_TEST_BYTES ? PSRAM_TEST_BYTES : psram_free / 2U;
        psram = memory_test(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                            psram_bytes);
    }

    uint32_t flash_crc = 0;
    uint32_t flash_kib_s = 0;
    const bool flash_ok =
        flash_size_ok &&
        flash_read_test(flash_size, &flash_crc, &flash_kib_s);
    const bool rng_ok = rng_interface_test();
    const bool aes_ok = aes_known_answer_test();
    const bool sha_ok = sha256_known_answer_test();
    const bool timer_ok = timer_test();
    const bool cpu_ok =
        chip.cores == 2 && cpu_mhz == CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
#if CONFIG_SPIRAM
    const bool psram_ok = psram.tested && psram.passed;
#else
    const bool psram_ok = true;
#endif

    ESP_LOGI(TAG,
             "RAM=%s bytes=%" PRIu32 " crc=%08" PRIX32 " throughput=%" PRIu32
             "KiB/s",
             !internal.tested ? "NOT_TESTED" : (internal.passed ? "PASS" : "FAIL"),
             internal.bytes,
             internal.crc, internal.throughput_kib_s);
    ESP_LOGI(TAG,
             "PSRAM=%s bytes=%" PRIu32 " crc=%08" PRIX32 " throughput=%" PRIu32
             "KiB/s",
             !psram.tested ? "NOT_TESTED" : (psram.passed ? "PASS" : "FAIL"),
             psram.bytes, psram.crc, psram.throughput_kib_s);
    ESP_LOGI(TAG,
             "FLASH_READ=%s bytes=%" PRIu32 " crc=%08" PRIX32
             " throughput=%" PRIu32 "KiB/s",
             flash_ok ? "PASS" : "FAIL", flash_size, flash_crc, flash_kib_s);
    ESP_LOGI(TAG, "RNG=%s PSA_AES_KAT=%s PSA_SHA256_KAT=%s TIMER=%s MAC=%s",
             rng_ok ? "PASS" : "FAIL", aes_ok ? "PASS" : "FAIL",
             sha_ok ? "PASS" : "FAIL", timer_ok ? "PASS" : "FAIL",
             mac_ok ? "PASS" : "FAIL");

    const bool pass = cpu_ok && flash_ok && internal.passed && psram_ok &&
                      rng_ok && aes_ok && sha_ok && timer_ok && mac_ok;
    char line1[48];
    char line2[48];
    snprintf(line1, sizeof(line1), "%dcore/%" PRIu32 "MHz %s",
             chip.cores, cpu_mhz, pass ? "PASS" : "FAIL");
    snprintf(line2, sizeof(line2), "RAM%" PRIu32 "K PS%" PRIu32 "K F%" PRIu32 "K",
             internal_free / 1024U, psram_total / 1024U, flash_size / 1024U);
    app_result_set(result, "S3 SELF",
                   pass ? APP_TEST_PASS : APP_TEST_FAIL, line1, line2);
    return pass ? ESP_OK : ESP_FAIL;
}
