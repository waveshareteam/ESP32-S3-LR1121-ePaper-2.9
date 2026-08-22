#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Board-level ownership for the LR1121/e-paper shared rail and SPI3 bus.
 * Pin values are validated against Board Manager data before either driver is
 * allowed to use the hardware.
 */

#define BOARD_PERIPH_SHARED_SPI "spi_shared"

#define BOARD_DEVICE_EPAPER "epaper_ssd16xx"
#define BOARD_DEVICE_RADIO  "radio_lr1121"

typedef enum {
    BOARD_SPI_OWNER_NONE = 0,
    BOARD_SPI_OWNER_EPAPER,
    BOARD_SPI_OWNER_RADIO,
} board_spi_owner_t;

/* Validated e-paper wiring returned to the panel adapter. */
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

/* Validated LR1121 wiring returned to the RadioLib HAL adapter. */
typedef struct {
    gpio_num_t dio1;
    gpio_num_t nss;
    gpio_num_t reset;
    gpio_num_t busy;
    gpio_num_t sck;
    gpio_num_t miso;
    gpio_num_t mosi;
    spi_host_device_t spi_host;
    int spi_speed_hz;
} board_radio_hw_config_t;

/* Initializes Board Manager, the SPI lock, and the shared power rail once. */
esp_err_t board_support_prepare(void);

/*
 * Controls the panel power request. The implementation keeps the shared rail
 * asserted while LR1121 Class C reception may still be active.
 */
esp_err_t board_epaper_power_set(bool on);

/* Copies validated, immutable board wiring into caller-owned structures. */
esp_err_t board_epaper_get_hw_config(board_epaper_hw_config_t *config);
esp_err_t board_radio_get_hw_config(board_radio_hw_config_t *config);

/*
 * Serializes complete SPI transactions across the radio and display drivers.
 * Pass a negative timeout to wait indefinitely; unlock with the same owner.
 */
bool board_spi_lock(board_spi_owner_t owner, int32_t timeout_ms);
void board_spi_unlock(board_spi_owner_t owner);

/* Diagnostic snapshot only; it does not transfer or release ownership. */
board_spi_owner_t board_spi_get_owner(void);

#ifdef __cplusplus
}
#endif
