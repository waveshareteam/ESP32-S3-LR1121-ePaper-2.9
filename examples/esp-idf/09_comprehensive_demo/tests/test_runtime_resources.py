"""Host regressions for the actual memory/ISR functions, using bounded heaps.

Run with Python 3 and a host C compiler; neither SDK nor board is required.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
SUPPORT = PROJECT / 'components/esp32_s3_lr1121_epaper_2_9_support/src'


def section(path, start, end):
    source = path.read_text()
    return source[source.index(start):source.index(end, source.index(start))]


def run_c(source):
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / 'test.c'
        exe = Path(tmp) / 'test'
        src.write_text(source)
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


HEAP_MOCK = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_SPIRAM 2
#define MALLOC_CAP_8BIT 4
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
typedef int esp_err_t;
static size_t available[2], largest[2], limit[2], live;
static unsigned calls, fail_call;
static bool corrupt;
static int pool(uint32_t caps) { return (caps & MALLOC_CAP_SPIRAM) ? 1 : 0; }
static size_t heap_caps_get_free_size(uint32_t caps) { return available[pool(caps)]; }
static size_t heap_caps_get_largest_free_block(uint32_t caps) { return largest[pool(caps)]; }
static size_t heap_caps_get_total_size(uint32_t caps) { (void)caps; return 8*1024*1024; }
typedef struct { size_t size; int kind; } header_t;
static void *heap_caps_malloc(size_t bytes, uint32_t caps) {
    int kind = pool(caps);
    calls++;
    if (calls == fail_call || bytes > available[kind] ||
        bytes > largest[kind] || bytes > limit[kind]) return NULL;
    header_t *header = malloc(sizeof(*header) + bytes);
    assert(header);
    header->size = bytes; header->kind = kind;
    available[kind] -= bytes; live++;
    return header + 1;
}
static void heap_caps_free(void *ptr) {
    if (!ptr) return;
    header_t *header = (header_t *)ptr - 1;
    available[header->kind] += header->size;
    assert(live); live--; free(header);
}
static void reset_heap(size_t internal, size_t contiguous) {
    assert(live == 0);
    available[0] = internal; largest[0] = contiguous;
    available[1] = largest[1] = 8*1024*1024;
    limit[0] = limit[1] = SIZE_MAX;
    calls = fail_call = 0; corrupt = false;
}
static uint32_t esp_crc32_le(uint32_t seed, const uint8_t *data, size_t size) {
    if (corrupt) ((uint8_t *)data)[0] ^= 1;
    for (size_t i = 0; i < size; i++) seed = seed * 33 + data[i];
    return seed;
}
static int64_t esp_timer_get_time(void) { static int64_t now; return now += 1000; }
'''


class RuntimeResourcesTest(unittest.TestCase):
    def test_self_test_fragmented_heap_and_allocation_failures(self):
        code = section(PROJECT / 'main/test_system.c', '#define INTERNAL_TEST_BYTES',
                       'static bool flash_read_test')
        run_c(HEAP_MOCK + code + r'''
int main(void) {
    (void)heap_caps_get_total_size;
    reset_heap(166*1024, 31744);
    memory_test_result_t result = memory_test(5, INTERNAL_TEST_BYTES);
    assert(result.tested && result.passed && result.bytes == 31744);
    assert(result.crc && result.throughput_kib_s && !live);
    reset_heap(166*1024, 64*1024);
    limit[0] = 4096; /* Fragmentation changed after the budget snapshot. */
    result = memory_test(5, INTERNAL_TEST_BYTES);
    assert(result.tested && result.passed && result.bytes == 4096 && !live);
    reset_heap(166*1024, 64*1024);
    limit[0] = 0;
    result = memory_test(5, INTERNAL_TEST_BYTES);
    assert(!result.tested && !result.passed && result.bytes == 0 && !live);
    reset_heap(48*1024, 32768);
    result = memory_test(5, INTERNAL_TEST_BYTES);
    assert(!result.tested && !result.passed && calls == 0);
    reset_heap(166*1024, 65536);
    result = memory_test(6, PSRAM_TEST_BYTES);
    assert(result.tested && result.passed && result.bytes == PSRAM_TEST_BYTES && !live);
}
''')

    def test_gpio_service_installed_once_and_failed_install_retried(self):
        code = section(SUPPORT / 'board_support.c', 'esp_err_t board_support_prepare(void)',
                       'esp_err_t board_support_deinit(void)')
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_BOARD_ERR_MANAGER_ALREADY_INIT 4
#define ESP_LOGE(...) ((void)0)
#define ESP_RETURN_ON_ERROR(expr, ...) do { int err = (expr); if (err) return err; } while (0)
#define ESP_RETURN_ON_FALSE(expr, err, ...) do { if (!(expr)) return err; } while (0)
#define pdTRUE 1
#define portMAX_DELAY 0
#define BOARD_DEVICE_RTC 0
static bool s_gpio_isr_service_ready, s_ops_registered;
static int s_spi_mutex, rtc_device_init, rtc_device_deinit;
static int installs, managers, install_result, manager_result, locked;
static int spi_mutex_init(void) { return ESP_OK; }
static int xSemaphoreTake(int mutex, int wait) {
    (void)mutex; (void)wait; assert(!locked); locked = 1; return pdTRUE;
}
static void xSemaphoreGive(int mutex) { (void)mutex; assert(locked); locked = 0; }
static int gpio_install_isr_service(int flags) {
    assert(flags == 0 && locked); installs++; return install_result;
}
static int esp_board_device_set_ops(int board, int init, int deinit) {
    (void)board; (void)init; (void)deinit; assert(s_gpio_isr_service_ready); return ESP_OK;
}
static int esp_board_manager_init(void) {
    assert(s_gpio_isr_service_ready); managers++; return manager_result;
}
''' + code + r'''
int main(void) {
    install_result = ESP_ERR_NO_MEM;
    assert(board_support_prepare() == ESP_ERR_NO_MEM);
    assert(installs == 1 && managers == 0 && !locked && !s_gpio_isr_service_ready);
    install_result = ESP_OK; manager_result = ESP_ERR_NO_MEM;
    assert(board_support_prepare() == ESP_ERR_NO_MEM);
    assert(installs == 2 && s_gpio_isr_service_ready && !locked);
    manager_result = ESP_OK;
    for (int i = 0; i < 100; i++) assert(board_support_prepare() == ESP_OK);
    assert(installs == 2 && !locked);
    manager_result = ESP_BOARD_ERR_MANAGER_ALREADY_INIT;
    assert(board_support_prepare() == ESP_OK && installs == 2);
    /* The service may have been installed by code outside board support. */
    s_gpio_isr_service_ready = false; install_result = ESP_ERR_INVALID_STATE;
    assert(board_support_prepare() == ESP_OK);
    assert(board_support_prepare() == ESP_OK && installs == 3);
}
''')


if __name__ == '__main__':
    unittest.main()
