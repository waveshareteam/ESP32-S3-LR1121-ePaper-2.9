/**
 * @file board_lorawan_radio.cpp
 * @brief Adapts RadioLib LR1121 access to ESP-IDF board resources.
 *
 * RadioLib callbacks cannot return ESP-IDF errors, so hardware failures are
 * logged and the adapter retains a state that initialization can reject.
 */

#include "board_lorawan_radio.h"

#include <inttypes.h>
#include <new>

#include <RadioLib.h>

#include "board_support.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LOW     0
#define HIGH    1
#define INPUT   0x01
#define OUTPUT  0x03
#define RISING  0x01
#define FALLING 0x02

static const char *TAG = "board_lorawan";

static constexpr float EXPECTED_TCXO_VOLTAGE = 3.0f;

/**
 * @brief ESP-IDF implementation of the RadioLib hardware abstraction.
 *
 * Each complete RadioLib transaction owns the board SPI mutex, preventing the
 * e-paper client from interleaving bytes on the shared bus.
 */
class EspIdfRadioHal : public RadioLibHal {
public:
    EspIdfRadioHal(spi_host_device_t host, gpio_num_t sck, gpio_num_t miso,
                   gpio_num_t mosi, uint32_t speed_hz, gpio_num_t nss)
        : RadioLibHal(INPUT, OUTPUT, LOW, HIGH, RISING, FALLING),
          spi_host(host), spi_sck(sck), spi_miso(miso), spi_mosi(mosi),
          spi_speed_hz(speed_hz), radio_nss(nss), spi(nullptr),
          transaction_locked(false), isr_service_installed(false) {}

    void init() override {
        spiBegin();
    }

    void term() override {
        spiEnd();
    }

    void yield() override {
        vTaskDelay(1);
    }

    void pinMode(uint32_t pin, uint32_t mode) override {
        if (pin == RADIOLIB_NC) {
            return;
        }

        gpio_config_t conf = {};
        conf.pin_bit_mask = BIT64(pin);
        conf.mode = (mode == OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
        conf.pull_up_en = GPIO_PULLUP_DISABLE;
        conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        conf.intr_type = GPIO_INTR_DISABLE;
        esp_err_t ret = gpio_config(&conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "configure GPIO%" PRIu32 " failed: %s",
                     pin, esp_err_to_name(ret));
        }
    }

    void digitalWrite(uint32_t pin, uint32_t value) override {
        if (pin == RADIOLIB_NC) {
            return;
        }
        esp_err_t ret = gpio_set_level((gpio_num_t)pin, value);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "set GPIO%" PRIu32 " failed: %s",
                     pin, esp_err_to_name(ret));
        }
    }

    uint32_t digitalRead(uint32_t pin) override {
        if (pin == RADIOLIB_NC) {
            return 0;
        }
        return gpio_get_level((gpio_num_t)pin);
    }

    void attachInterrupt(uint32_t pin, void (*cb)(void), uint32_t mode) override {
        if (pin == RADIOLIB_NC || cb == nullptr) {
            return;
        }

        esp_err_t ret = gpio_set_intr_type((gpio_num_t)pin,
                                           (gpio_int_type_t)mode);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "set GPIO interrupt type %" PRIu32 " failed: %s",
                     pin, esp_err_to_name(ret));
            return;
        }

        /*
         * The ISR service is process-wide. An existing service is valid, while
         * this adapter owns only the single callback slot RadioLib needs.
         */
        if (!isr_service_installed) {
            ret = gpio_install_isr_service(0);
            if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(TAG, "install GPIO ISR service failed: %s",
                         esp_err_to_name(ret));
                return;
            }
            isr_service_installed = true;
        }

        /* Reject a second radio interrupt instead of silently replacing it. */
        if (interrupt_slot.callback != nullptr) {
            if (interrupt_slot.pin != pin) {
                ESP_LOGE(TAG, "only one RadioLib GPIO interrupt is supported");
                return;
            }
            ret = gpio_isr_handler_remove((gpio_num_t)pin);
            if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(TAG, "replace GPIO ISR handler %" PRIu32 " failed: %s",
                         pin, esp_err_to_name(ret));
                return;
            }
        }

        interrupt_slot.pin = pin;
        interrupt_slot.callback = cb;
        ret = gpio_isr_handler_add((gpio_num_t)pin, gpioIsrTrampoline,
                                   &interrupt_slot);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "add GPIO ISR handler %" PRIu32 " failed: %s",
                     pin, esp_err_to_name(ret));
            interrupt_slot.pin = RADIOLIB_NC;
            interrupt_slot.callback = nullptr;
        }
    }

    void detachInterrupt(uint32_t pin) override {
        if (pin == RADIOLIB_NC) {
            return;
        }
        esp_err_t ret = gpio_isr_handler_remove((gpio_num_t)pin);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "remove GPIO ISR handler %" PRIu32 " failed: %s",
                     pin, esp_err_to_name(ret));
            return;
        }
        if (interrupt_slot.pin == pin) {
            interrupt_slot.pin = RADIOLIB_NC;
            interrupt_slot.callback = nullptr;
        }
    }

    void delay(unsigned long ms) override {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    void delayMicroseconds(unsigned long us) override {
        int64_t start = esp_timer_get_time();
        while ((esp_timer_get_time() - start) < (int64_t)us) {
            ;
        }
    }

    unsigned long millis() override {
        return (unsigned long)(esp_timer_get_time() / 1000);
    }

    unsigned long micros() override {
        return (unsigned long)esp_timer_get_time();
    }

    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override {
        if (pin == RADIOLIB_NC) {
            return 0;
        }

        pinMode(pin, INPUT);
        uint32_t start = micros();
        while (digitalRead(pin) == state) {
            if ((micros() - start) > timeout) {
                return 0;
            }
        }
        while (digitalRead(pin) != state) {
            if ((micros() - start) > timeout) {
                return 0;
            }
        }

        uint32_t pulse_start = micros();
        while (digitalRead(pin) == state) {
            if ((micros() - start) > timeout) {
                return 0;
            }
        }
        return micros() - pulse_start;
    }

    void spiBegin() override {
        if (spi != nullptr) {
            return;
        }
        /*
         * Board Manager owns bus initialization; this adapter only adds the
         * LR1121 device while holding the shared-resource mutex.
         */
        if (!board_spi_lock(BOARD_SPI_OWNER_RADIO, -1)) {
            ESP_LOGE(TAG, "lock shared SPI while adding LR1121 failed");
            return;
        }

        spi_device_interface_config_t devcfg = {};
        devcfg.clock_speed_hz = spi_speed_hz;
        devcfg.mode = 0;
        devcfg.spics_io_num = -1;
        devcfg.queue_size = 1;

        ESP_LOGI(TAG,
                 "LR1121 SPI host=%d pins: SCK=%d MISO=%d MOSI=%d "
                 "NSS=%d speed=%" PRIu32,
                 (int)spi_host, spi_sck, spi_miso, spi_mosi, radio_nss,
                 spi_speed_hz);
        esp_err_t ret = spi_bus_add_device(spi_host, &devcfg, &spi);
        board_spi_unlock(BOARD_SPI_OWNER_RADIO);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "add LR1121 SPI device failed: %s",
                     esp_err_to_name(ret));
            spi = nullptr;
        }
    }

    void spiBeginTransaction() override {
        if (transaction_locked) {
            return;
        }
        /* Hold ownership through NSS control and the complete byte sequence. */
        if (!board_spi_lock(BOARD_SPI_OWNER_RADIO, -1)) {
            ESP_LOGE(TAG, "lock shared SPI transaction failed");
            return;
        }
        transaction_locked = true;
    }

    uint8_t spiTransferByte(uint8_t value) {
        uint8_t input = 0;
        spiTransfer(&value, 1, &input);
        return input;
    }

    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override {
        if (spi == nullptr) {
            ESP_LOGE(TAG, "SPI transfer before LR1121 device init");
            return;
        }

        spi_transaction_t transaction = {};
        transaction.length = len * 8U;
        transaction.tx_buffer = out;
        transaction.rx_buffer = in;
        esp_err_t ret = spi_device_transmit(spi, &transaction);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "LR1121 SPI transfer failed: %s",
                     esp_err_to_name(ret));
        }
    }

    void spiEndTransaction() override {
        if (!transaction_locked) {
            return;
        }
        board_spi_unlock(BOARD_SPI_OWNER_RADIO);
        transaction_locked = false;
    }

    void spiEnd() override {
        if (spi == nullptr) {
            return;
        }

        /*
         * Reuse an active transaction lock when RadioLib terminates mid-flow;
         * otherwise acquire ownership before unregistering the device.
         */
        bool acquired_here = false;
        if (!transaction_locked) {
            if (!board_spi_lock(BOARD_SPI_OWNER_RADIO, -1)) {
                ESP_LOGE(TAG, "lock shared SPI while removing LR1121 failed");
                return;
            }
            acquired_here = true;
        }

        esp_err_t ret = spi_bus_remove_device(spi);
        spi = nullptr;
        if (transaction_locked) {
            board_spi_unlock(BOARD_SPI_OWNER_RADIO);
            transaction_locked = false;
        } else if (acquired_here) {
            board_spi_unlock(BOARD_SPI_OWNER_RADIO);
        }
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "remove LR1121 SPI device failed: %s",
                     esp_err_to_name(ret));
        }
    }

private:
    struct InterruptSlot {
        uint32_t pin;
        void (*callback)(void);
    };

    static void IRAM_ATTR gpioIsrTrampoline(void *arg) {
        InterruptSlot *slot = static_cast<InterruptSlot *>(arg);
        if (slot != nullptr && slot->callback != nullptr) {
            slot->callback();
        }
    }

    spi_host_device_t spi_host;
    gpio_num_t spi_sck;
    gpio_num_t spi_miso;
    gpio_num_t spi_mosi;
    uint32_t spi_speed_hz;
    gpio_num_t radio_nss;
    spi_device_handle_t spi;
    bool transaction_locked;
    bool isr_service_installed;
    InterruptSlot interrupt_slot = {RADIOLIB_NC, nullptr};
};

/**
 * @brief Forces RadioLib power checks and updates to use the high-power PA.
 *
 * This matches the RF path proven by the Starter example.
 */
class HighPowerLR1121 final : public LR1121 {
public:
    explicit HighPowerLR1121(Module *module) : LR1121(module) {}

    using LR1120::checkOutputPower;
    using LR1120::setOutputPower;

    int16_t setOutputPower(int8_t power) override {
        return LR1120::setOutputPower(power, true);
    }

    int16_t checkOutputPower(int8_t power, int8_t *clipped) override {
        return LR1120::checkOutputPower(power, clipped, true);
    }
};

/* Module-owned singleton objects; the LoRaWAN radio task is their sole caller. */
static board_radio_hw_config_t s_hw_config;
static EspIdfRadioHal *s_hal;
static Module *s_module;
static LR1121 *s_radio;
static bool s_initialized;
static int16_t s_last_radio_error = RADIOLIB_ERR_NONE;

static void gpio_hold_dis_if_supported(gpio_num_t gpio_num)
{
    /* Unsupported hold control is harmless; other failures remain visible. */
    esp_err_t err = gpio_hold_dis(gpio_num);
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "gpio_hold_dis(GPIO%d) failed: %s",
                 (int)gpio_num, esp_err_to_name(err));
    }
}

/* Release partial construction in reverse ownership order after any failure. */
static void destroy_radio_objects(bool terminate_module)
{
    if (terminate_module && s_module != nullptr) {
        s_module->term();
    }
    delete s_radio;
    delete s_module;
    delete s_hal;
    s_radio = nullptr;
    s_module = nullptr;
    s_hal = nullptr;
    s_initialized = false;
}

/* Construct the HAL, RadioLib module, and radio as one recoverable unit. */
static esp_err_t create_radio_objects(void)
{
    if (s_radio != nullptr) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(board_support_prepare(), TAG,
                        "prepare board support failed");
    ESP_RETURN_ON_ERROR(board_radio_get_hw_config(&s_hw_config), TAG,
                        "get LR1121 board config failed");

    const gpio_num_t radio_pins[] = {
        s_hw_config.dio1,
        s_hw_config.nss,
        s_hw_config.reset,
        s_hw_config.busy,
        s_hw_config.sck,
        s_hw_config.miso,
        s_hw_config.mosi,
    };
    for (size_t i = 0; i < sizeof(radio_pins) / sizeof(radio_pins[0]); ++i) {
        gpio_hold_dis_if_supported(radio_pins[i]);
    }

    s_hal = new (std::nothrow) EspIdfRadioHal(
        s_hw_config.spi_host,
        s_hw_config.sck,
        s_hw_config.miso,
        s_hw_config.mosi,
        s_hw_config.spi_speed_hz,
        s_hw_config.nss);
    if (s_hal == nullptr) {
        destroy_radio_objects(false);
        return ESP_ERR_NO_MEM;
    }

    s_module = new (std::nothrow) Module(
        s_hal,
        (uint32_t)s_hw_config.nss,
        (uint32_t)s_hw_config.dio1,
        (uint32_t)s_hw_config.reset,
        (uint32_t)s_hw_config.busy);
    if (s_module == nullptr) {
        destroy_radio_objects(false);
        return ESP_ERR_NO_MEM;
    }

    s_radio = new (std::nothrow) HighPowerLR1121(s_module);
    if (s_radio == nullptr) {
        destroy_radio_objects(false);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/*
 * DIO5 and DIO6 drive the board RF switch. These modes preserve the Starter
 * routing: RX selects the receive path, TX/HP selects the high-power path, and
 * inactive 2.4 GHz/GNSS/Wi-Fi modes leave both controls low.
 */
static const uint32_t s_rfswitch_dio_pins[] = {
    RADIOLIB_LR11X0_DIO5,
    RADIOLIB_LR11X0_DIO6,
    RADIOLIB_NC,
    RADIOLIB_NC,
    RADIOLIB_NC,
};

static const Module::RfSwitchMode_t s_rfswitch_table[] = {
    {LR11x0::MODE_STBY,  {LOW,  LOW}},
    {LR11x0::MODE_RX,    {HIGH, LOW}},
    {LR11x0::MODE_TX,    {LOW,  HIGH}},
    {LR11x0::MODE_TX_HP, {LOW,  HIGH}},
    {LR11x0::MODE_TX_HF, {LOW,  LOW}},
    {LR11x0::MODE_GNSS,  {LOW,  LOW}},
    {LR11x0::MODE_WIFI,  {LOW,  LOW}},
    END_OF_MODE_TABLE,
};

esp_err_t board_lorawan_radio_init(float initial_frequency_mhz,
                                   int8_t initial_power_dbm,
                                   LR1121 **radio)
{
    ESP_RETURN_ON_FALSE(radio != nullptr, ESP_ERR_INVALID_ARG, TAG,
                        "radio output is null");
    *radio = nullptr;

    if (s_initialized) {
        *radio = s_radio;
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(initial_frequency_mhz > 0.0f, ESP_ERR_INVALID_ARG,
                        TAG, "initial frequency is invalid");
    ESP_RETURN_ON_ERROR(create_radio_objects(), TAG,
                        "create LR1121 RadioLib objects failed");

    ESP_LOGI(TAG,
             "initializing LR1121 for LoRaWAN: %.4f MHz, %d dBm, "
             "HP PA forced, TCXO %.1f V",
             (double)initial_frequency_mhz, (int)initial_power_dbm,
             (double)EXPECTED_TCXO_VOLTAGE);

    /*
     * RadioLib configures and controls the board TCXO from the supplied 1.6 V
     * value. The RF switch table is installed only after begin succeeds.
     */
    int16_t state = s_radio->begin(
        initial_frequency_mhz,
        125.0f,
        7,
        5,
        RADIOLIB_LORAWAN_LORA_SYNC_WORD,
        initial_power_dbm,
        RADIOLIB_LORAWAN_LORA_PREAMBLE_LEN,
        EXPECTED_TCXO_VOLTAGE);
    s_last_radio_error = state;
    /* Preserve the RadioLib error for callers even when cleanup follows. */
    if (state == RADIOLIB_ERR_NONE) {
        s_radio->setRfSwitchTable(s_rfswitch_dio_pins, s_rfswitch_table);
    }
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "LR1121 initialization failed: RadioLib %d", state);
        destroy_radio_objects(true);
        return ESP_FAIL;
    }

    s_initialized = true;
    *radio = s_radio;
    return ESP_OK;
}

int16_t board_lorawan_radio_last_error(void)
{
    return s_last_radio_error;
}


