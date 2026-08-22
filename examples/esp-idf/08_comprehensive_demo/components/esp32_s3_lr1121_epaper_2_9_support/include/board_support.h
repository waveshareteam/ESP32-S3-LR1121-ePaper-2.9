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

#define BOARD_PERIPH_I2C        "i2c_master"
#define BOARD_PERIPH_SHARED_SPI "spi_shared"
#define BOARD_PERIPH_BOOT       "gpio_boot"

#define BOARD_DEVICE_STATUS_LED "status_led"
#define BOARD_DEVICE_EPAPER     "epaper_ssd16xx"
#define BOARD_DEVICE_RTC        "rtc_pcf85063a"
#define BOARD_DEVICE_AXP2602    "bat_axp2602"
#define BOARD_DEVICE_RADIO      "radio_lr1121"

#define BOARD_BUTTON_SLEEP_GPIO       GPIO_NUM_0
#define BOARD_BUTTON_RADIO_GPIO       GPIO_NUM_1
#define BOARD_RADIO_DIO1_GPIO         GPIO_NUM_6
#define BOARD_RTC_IRQ_GPIO            GPIO_NUM_7
#define BOARD_EPAPER_BUSY_GPIO        GPIO_NUM_8
#define BOARD_EPAPER_RESET_GPIO       GPIO_NUM_9
#define BOARD_I2C_SDA_GPIO            GPIO_NUM_13
#define BOARD_I2C_SCL_GPIO            GPIO_NUM_14
#define BOARD_EPAPER_DC_GPIO          GPIO_NUM_17
#define BOARD_EPAPER_CS_GPIO          GPIO_NUM_18
#define BOARD_EPAPER_POWER_GPIO       GPIO_NUM_38
#define BOARD_RADIO_RESET_GPIO        GPIO_NUM_39
#define BOARD_SHARED_SPI_SCK_GPIO     GPIO_NUM_40
#define BOARD_RADIO_BUSY_GPIO         GPIO_NUM_41
#define BOARD_RADIO_NSS_GPIO          GPIO_NUM_42
#define BOARD_SHARED_SPI_MOSI_GPIO    GPIO_NUM_45
#define BOARD_SHARED_SPI_MISO_GPIO    GPIO_NUM_46
#define BOARD_STATUS_LED_GPIO         GPIO_NUM_21
#define BOARD_AXP2602_CHARGE_GPIO     GPIO_NUM_47
#define BOARD_AXP2602_IRQ_GPIO        GPIO_NUM_48
#define BOARD_SDMMC_CMD_GPIO          GPIO_NUM_10
#define BOARD_SDMMC_D0_GPIO           GPIO_NUM_11
#define BOARD_SDMMC_CLK_GPIO          GPIO_NUM_12

#define BOARD_AXP2602_IRQ_LOW_SOC           (1U << 0)
#define BOARD_AXP2602_IRQ_SOC_UPDATE        (1U << 1)
#define BOARD_AXP2602_IRQ_OVER_TEMPERATURE  (1U << 2)
#define BOARD_AXP2602_IRQ_WATCHDOG_OVERFLOW (1U << 3)
#define BOARD_AXP2602_IRQ_ALL               (BOARD_AXP2602_IRQ_LOW_SOC | \
                                             BOARD_AXP2602_IRQ_SOC_UPDATE | \
                                             BOARD_AXP2602_IRQ_OVER_TEMPERATURE | \
                                             BOARD_AXP2602_IRQ_WATCHDOG_OVERFLOW)

typedef enum {
    BOARD_SPI_OWNER_NONE = 0,
    BOARD_SPI_OWNER_EPAPER,
    BOARD_SPI_OWNER_RADIO,
} board_spi_owner_t;

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

typedef struct {
    uint32_t count;
    int gpio_level;
    uint8_t control2;
} board_rtc_interrupt_status_t;

typedef enum {
    BOARD_AXP2602_STATE_IDLE = 0,
    BOARD_AXP2602_STATE_CHARGING,
    BOARD_AXP2602_STATE_DISCHARGING,
} board_axp2602_state_t;

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

esp_err_t board_support_prepare(void);
esp_err_t board_support_deinit(void);
void board_release_gpio_deep_sleep_holds(void);

esp_err_t board_i2c_acquire(i2c_master_bus_handle_t *bus);
esp_err_t board_i2c_get_config(const i2c_master_bus_config_t **config);
esp_err_t board_i2c_probe(uint8_t address, uint32_t timeout_ms);

gpio_num_t board_boot_gpio(void);
esp_err_t board_status_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue);
esp_err_t board_status_led_set_percent(uint8_t percent);
esp_err_t board_status_led_set(bool on);
esp_err_t board_epaper_power_set(bool on);
esp_err_t board_epaper_get_hw_config(board_epaper_hw_config_t *config);
esp_err_t board_radio_get_hw_config(board_radio_hw_config_t *config);

bool board_spi_lock(board_spi_owner_t owner, int32_t timeout_ms);
void board_spi_unlock(board_spi_owner_t owner);
board_spi_owner_t board_spi_get_owner(void);

esp_err_t board_rtc_init(void);
esp_err_t board_rtc_get_time(board_rtc_time_t *time);
esp_err_t board_rtc_set_time(const board_rtc_time_t *time);
esp_err_t board_rtc_start_1hz_timer_interrupt(void);
esp_err_t board_rtc_stop_timer_interrupt(void);
esp_err_t board_rtc_clear_interrupt_flags(void);
esp_err_t board_rtc_get_interrupt_status(board_rtc_interrupt_status_t *status);
uint32_t board_rtc_get_interrupt_count(void);

esp_err_t board_axp2602_init(void);
esp_err_t board_axp2602_get_info(board_axp2602_info_t *info);
esp_err_t board_axp2602_configure_irq(uint8_t enabled_events, bool pulse_mode);
esp_err_t board_axp2602_get_irq_status(uint8_t *status);
esp_err_t board_axp2602_clear_irq_status(uint8_t status);
esp_err_t board_axp2602_sleep(void);

/* GPIO38 switches the shared 3V3 rail used by the e-paper/LR1121 domain. */
esp_err_t board_shared_power_hold_off(void);
esp_err_t board_prepare_gpio_deep_sleep(void);

#ifdef __cplusplus
}
#endif
