/**
 * @file board_support.h
 * @brief Exposes shared board power, SPI, and device configuration services.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Board Manager names shared by the e-paper and LR1121 adapters. */
#define BOARD_PERIPH_SHARED_SPI "spi_shared"
#define BOARD_DEVICE_EPAPER "epaper_ssd16xx"
#define BOARD_DEVICE_RADIO "radio_lr1121"

/** Identifies the current holder of the shared SPI mutex. */
typedef enum {
    /** The bus mutex is not held by a registered board client. */
    BOARD_SPI_OWNER_NONE = 0,
    /** The e-paper adapter owns the current SPI transaction. */
    BOARD_SPI_OWNER_EPAPER,
    /** The LR1121 adapter owns the current SPI transaction. */
    BOARD_SPI_OWNER_RADIO,
} board_spi_owner_t;

/**
 * @brief Resolved e-paper pins and SPI settings from Board Manager.
 *
 * The SPI bus itself is shared with the LR1121 and must be locked separately.
 */
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

/**
 * @brief Resolved LR1121 pins and SPI settings from Board Manager.
 *
 * DIO7 and DIO8 are retained as board metadata; RadioLib drives the RF switch.
 */
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

/**
 * @brief Prepare shared peripheral power, the SPI mutex, and Board Manager.
 *
 * The operation is idempotent and keeps the rail shared by the e-paper and
 * LR1121 enabled.
 *
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t board_support_prepare(void);

/**
 * @brief Request e-paper power without disabling the LR1121 shared rail.
 *
 * Passing false leaves the shared rail unchanged; prepare board support first
 * when Class C reception requires the rail to remain enabled.
 *
 * @param on True to enable e-paper power, or false to leave the rail unchanged.
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t board_epaper_power_set(bool on);

/**
 * @brief Resolve the e-paper hardware configuration.
 *
 * @param[out] config Receives the validated board configuration.
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t board_epaper_get_hw_config(board_epaper_hw_config_t *config);

/**
 * @brief Resolve the LR1121 hardware configuration.
 *
 * @param[out] config Receives the validated board configuration.
 * @return ESP_OK on success, otherwise an ESP-IDF error code.
 */
esp_err_t board_radio_get_hw_config(board_radio_hw_config_t *config);

/**
 * @brief Acquire exclusive access to the shared SPI bus.
 *
 * @param owner Nonzero owner recorded until the matching unlock call.
 * @param timeout_ms Maximum wait in milliseconds, or a negative value forever.
 * @return True when the mutex and bus reference were acquired.
 */
bool board_spi_lock(board_spi_owner_t owner, int32_t timeout_ms);

/**
 * @brief Release shared SPI access held by the same owner.
 *
 * An owner mismatch is logged and deliberately leaves the mutex locked.
 *
 * @param owner Owner supplied to the successful lock call.
 */
void board_spi_unlock(board_spi_owner_t owner);

/**
 * @brief Return the recorded SPI owner for diagnostics.
 *
 * The returned snapshot does not itself provide synchronization.
 *
 * @return Current recorded owner, or BOARD_SPI_OWNER_NONE.
 */
board_spi_owner_t board_spi_get_owner(void);

#ifdef __cplusplus
}
#endif
