// Shared board services for the e-paper, LR1121 radio, RTC, and fuel gauge.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Names used to retrieve peripherals and devices from the board manager.
#define BOARD_PERIPH_I2C        "i2c_master"
#define BOARD_PERIPH_SHARED_SPI "spi_shared"
#define BOARD_PERIPH_BOOT       "gpio_boot"

#define BOARD_DEVICE_STATUS_LED "status_led"
#define BOARD_DEVICE_EPAPER     "epaper_ssd16xx"
#define BOARD_DEVICE_RTC        "rtc_pcf85063a"
#define BOARD_DEVICE_AXP2602    "bat_axp2602"
#define BOARD_DEVICE_RADIO      "radio_lr1121"

// Identifies the current owner of the SPI bus shared by the display and radio.
typedef enum {
    BOARD_SPI_OWNER_NONE = 0,
    BOARD_SPI_OWNER_EPAPER,
    BOARD_SPI_OWNER_RADIO,
} board_spi_owner_t;

// Calendar fields returned by the on-board RTC.
typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool voltage_low;
} board_rtc_time_t;

// Snapshot of the RTC interrupt line and interrupt control register.
typedef struct {
    uint32_t count;
    int gpio_level;
    uint8_t control2;
} board_rtc_interrupt_status_t;

// High-level battery direction reported by the AXP2602 fuel gauge.
typedef enum {
    BOARD_AXP2602_STATE_IDLE = 0,
    BOARD_AXP2602_STATE_CHARGING,
    BOARD_AXP2602_STATE_DISCHARGING,
} board_axp2602_state_t;

// Normalized battery measurements exposed to the application.
typedef struct {
    uint16_t voltage_mv;
    float current_ma;
    int8_t ic_temperature_c;
    uint8_t state_of_charge_percent;
    uint16_t time_to_empty_min;
    uint16_t time_to_full_min;
    uint8_t sense_resistor_mohm;
    board_axp2602_state_t state;
} board_axp2602_info_t;

// E-paper pins and SPI settings obtained from the board definition.
typedef struct {
    gpio_num_t busy;
    gpio_num_t reset;
    gpio_num_t dc;
    gpio_num_t cs;
    gpio_num_t sck;
    gpio_num_t mosi;
    spi_host_device_t spi_host;
    int spi_speed_hz;
} board_epaper_hw_config_t;

// LR1121 pins and SPI settings obtained from the board definition.
typedef struct {
    gpio_num_t dio1;
    gpio_num_t nss;
    gpio_num_t reset;
    gpio_num_t busy;
    gpio_num_t dio7;
    gpio_num_t dio8;
    gpio_num_t sck;
    gpio_num_t miso;
    gpio_num_t mosi;
    spi_host_device_t spi_host;
    int spi_speed_hz;
} board_radio_hw_config_t;

// Initializes the board manager and shared synchronization primitives.
esp_err_t board_support_prepare(void);

// Provides controlled access to the shared I2C bus.
esp_err_t board_i2c_acquire(i2c_master_bus_handle_t *bus);
esp_err_t board_i2c_get_config(const i2c_master_bus_config_t **config);
esp_err_t board_i2c_probe(uint8_t address, uint32_t timeout_ms);

// Provides access to board GPIOs and hardware configurations.
gpio_num_t board_boot_gpio(void);
esp_err_t board_status_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue);
esp_err_t board_status_led_set_percent(uint8_t percent);
esp_err_t board_status_led_set(bool on);
esp_err_t board_epaper_power_set(bool on);
esp_err_t board_epaper_get_hw_config(board_epaper_hw_config_t *config);
esp_err_t board_radio_get_hw_config(board_radio_hw_config_t *config);

// Serializes access to the display/radio SPI bus.
bool board_spi_lock(board_spi_owner_t owner, int32_t timeout_ms);
void board_spi_unlock(board_spi_owner_t owner);
board_spi_owner_t board_spi_get_owner(void);

// Controls the RTC and reads its interrupt state.
esp_err_t board_rtc_init(void);
esp_err_t board_rtc_get_time(board_rtc_time_t *time);
esp_err_t board_rtc_set_time(const board_rtc_time_t *time);
esp_err_t board_rtc_start_1hz_timer_interrupt(void);
esp_err_t board_rtc_clear_interrupt_flags(void);
esp_err_t board_rtc_get_interrupt_status(board_rtc_interrupt_status_t *status);
uint32_t board_rtc_get_interrupt_count(void);

// Controls the AXP2602 fuel gauge through the board-level I2C service.
esp_err_t board_axp2602_init(void);
esp_err_t board_axp2602_get_info(board_axp2602_info_t *info);
esp_err_t board_axp2602_sleep(void);

// Places external-device GPIOs in their board-defined deep-sleep state.
esp_err_t board_prepare_gpio_deep_sleep(void);

#ifdef __cplusplus
}
#endif
