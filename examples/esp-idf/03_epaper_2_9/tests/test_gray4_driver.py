"""Compile the real driver against fake SPI/GPIO to check transfers and failures."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'components/esp_epaper/src/controllers/ssd1680_290_gray4.c'
PRELUDE = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#define _EPAPER_H_
#define _EPAPER_COMMON_H_
typedef int esp_err_t;
enum { ESP_OK=0, ESP_ERR_INVALID_ARG=1, ESP_ERR_NOT_SUPPORTED=2,
       ESP_ERR_INVALID_STATE=3, ESP_ERR_TIMEOUT=4, TEST_SPI_FAILURE=5 };
enum { EPD_CTRL_SSD16XX=7 };
struct epd_device { int unused; };
typedef struct epd_device *epd_handle_t;
typedef struct { int ctrl; } epd_panel_desc_t;
typedef struct { void *spi; int pin_dc, pin_cs, pin_rst; } epd_spi_t;
typedef struct { size_t length; const void *tx_buffer; } spi_transaction_t;
int gpio_set_level(int pin, int value);
int spi_device_polling_transmit(void *spi, spi_transaction_t *transaction);
const epd_panel_desc_t *epd_get_panel(epd_handle_t panel);
uint16_t epd_get_width(epd_handle_t panel);
uint16_t epd_get_height(epd_handle_t panel);
epd_spi_t *epd_get_spi(epd_handle_t panel);
int epd_spi_is_busy(epd_spi_t *spi);
int64_t esp_timer_get_time(void);
void vTaskDelay(unsigned ticks);
#define pdMS_TO_TICKS(ms) (ms)
static inline void test_log(const char *tag, const char *fmt, ...) {(void)tag; (void)fmt;}
#define ESP_LOGE(...) test_log(__VA_ARGS__)
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define ESP_RETURN_ON_ERROR(expr,tag,...) do { int r=(expr); if(r) {test_log(tag,__VA_ARGS__); return r;} } while(0)
#define ESP_RETURN_ON_FALSE(expr,err,tag,...) do {if(!(expr)) {test_log(tag,__VA_ARGS__); return err;} } while(0)
'''
HARNESS = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "epaper_gray4_v2.h"
static struct epd_device panel;
static epd_spi_t spi = {.spi=&panel, .pin_dc=1, .pin_cs=2, .pin_rst=3};
static epd_panel_desc_t desc = {.ctrl=EPD_CTRL_SSD16XX};
static int mode, cs, calls, fail_at, stuck, activated;
static int64_t time_us;
static unsigned command_count, plane_count[2];
static uint8_t current, commands[32], input[9472];
int gpio_set_level(int pin, int value) {if(pin==1) mode=value; if(pin==2) cs=value; return 0;}
int spi_device_polling_transmit(void *handle, spi_transaction_t *tx) {
    assert(handle==&panel && cs==0);
    if(++calls==fail_at) return TEST_SPI_FAILURE;
    const uint8_t *p=tx->tx_buffer;
    size_t size=tx->length/8;
    if(!mode) {assert(size==1); current=p[0]; commands[command_count++]=current;
        if(current==0x20) activated=1;
    } else if(current==0x24 || current==0x26) {
        unsigned plane=current==0x26;
        for(size_t i=0;i<size;i++) assert(p[i]==(plane ? 0xcc : 0xaa));
        plane_count[plane]+=size;
    }
    return 0;
}
const epd_panel_desc_t *epd_get_panel(epd_handle_t p) {(void)p;return &desc;}
uint16_t epd_get_width(epd_handle_t p) {(void)p;return 128;}
uint16_t epd_get_height(epd_handle_t p) {(void)p;return 296;}
epd_spi_t *epd_get_spi(epd_handle_t p) {(void)p;return &spi;}
int epd_spi_is_busy(epd_spi_t *p) {(void)p;return stuck;}
int64_t esp_timer_get_time(void) {return time_us;}
void vTaskDelay(unsigned ticks) {time_us+=(int64_t)ticks*1000;}
static void reset(void) {
    mode=0; cs=1; calls=0; fail_at=0; stuck=0; activated=0;
    time_us=0; command_count=0; plane_count[0]=0; plane_count[1]=0;
}
int main(void) {
    memset(input,0x1b,sizeof(input)); /* repeats black/dark/light/white */
    reset();
    assert(epd_show_gray4_v2(&panel,input,sizeof(input))==ESP_OK);
    assert(plane_count[0]==4736 && plane_count[1]==4736 && activated);
    const uint8_t expected[]={0x12,0x74,0x7e,0x01,0x11,0x44,0x45,0x3c,0x21,
        0x4e,0x4f,0x32,0x3f,0x03,0x04,0x2c,0x4e,0x4f,0x24,0x4e,0x4f,0x26,0x22,0x20};
    assert(command_count==sizeof(expected) && !memcmp(commands,expected,sizeof(expected)));
    int successful_calls=calls;
    /* Every possible SPI failure must propagate, and CS must be released. */
    for(int i=1;i<=successful_calls;i++) {
        reset(); fail_at=i;
        assert(epd_show_gray4_v2(&panel,input,sizeof(input))==TEST_SPI_FAILURE);
        assert(calls==i && cs==1 && !activated);
    }
    reset(); stuck=1;
    assert(epd_show_gray4_v2(&panel,input,sizeof(input))==ESP_ERR_TIMEOUT);
    assert(calls==0 && time_us>=15000000 && time_us<16000000);
    reset();
    assert(epd_show_gray4_v2(&panel,input,sizeof(input)-1)==ESP_ERR_INVALID_ARG);
    assert(calls==0);
    desc.ctrl=99;
    assert(epd_show_gray4_v2(&panel,input,sizeof(input))==ESP_ERR_NOT_SUPPORTED);
    assert(calls==0);
    puts("PASS: SPI sequence, both planes, every SPI error, timeout and invalid arguments");
}
'''


class Gray4DriverTest(unittest.TestCase):
    def test_driver_with_fake_hardware(self):
        with tempfile.TemporaryDirectory() as folder:
            temp = Path(folder)
            (temp/'prelude.h').write_text(PRELUDE)
            (temp/'harness.c').write_text(HARNESS)
            for name in ('driver/gpio.h','esp_check.h','esp_timer.h',
                         'freertos/FreeRTOS.h','freertos/task.h'):
                path=temp/name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('/* Declarations supplied by the test prelude. */\n')
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                '-include',str(temp/'prelude.h'),'-I',str(temp),
                '-I',str(ROOT/'components/esp_epaper/include'),
                str(DRIVER),str(temp/'harness.c'),'-o',str(temp/'test')],check=True)
            subprocess.run([str(temp/'test')],check=True)

if __name__ == '__main__':
    unittest.main()
