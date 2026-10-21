#include "board_radio.h"

#include <inttypes.h>
#include <new>
#include <stdio.h>
#include <string.h>

#include <RadioLib.h>

#include "board_support.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LOW     0
#define HIGH    1
#define INPUT   0x01
#define OUTPUT  0x03
#define RISING  0x01
#define FALLING 0x02

static const char *TAG = "board_radio";

#define BOARD_RADIO_TX_TIMEOUT_MS 5000

class EspIdfRadioHal : public RadioLibHal {
public:
    EspIdfRadioHal(spi_host_device_t host, gpio_num_t sck, gpio_num_t miso,
                   gpio_num_t mosi, uint32_t speed_hz, gpio_num_t nss)
        : RadioLibHal(INPUT, OUTPUT, LOW, HIGH, RISING, FALLING),
          spi_host(host), spi_sck(sck), spi_miso(miso), spi_mosi(mosi),
          spi_speed_hz(speed_hz), radio_nss(nss), spi(nullptr),
          isr_service_installed(false) {}

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
            ESP_LOGE(TAG, "configure GPIO%" PRIu32 " failed: %s", pin, esp_err_to_name(ret));
        }
    }

    void digitalWrite(uint32_t pin, uint32_t value) override {
        if (pin == RADIOLIB_NC) {
            return;
        }
        esp_err_t ret = gpio_set_level((gpio_num_t)pin, value);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "set GPIO%" PRIu32 " failed: %s", pin, esp_err_to_name(ret));
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

        esp_err_t ret = gpio_set_intr_type((gpio_num_t)pin, (gpio_int_type_t)mode);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "set GPIO interrupt type %" PRIu32 " failed: %s", pin, esp_err_to_name(ret));
            return;
        }

        if (!isr_service_installed) {
            ret = gpio_install_isr_service(0);
            if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
                ESP_LOGE(TAG, "install GPIO ISR service failed: %s", esp_err_to_name(ret));
                return;
            }
            isr_service_installed = true;
        }

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
            ESP_LOGW(TAG, "remove GPIO ISR handler %" PRIu32 " failed: %s", pin, esp_err_to_name(ret));
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

    void spiBegin() {
        if (spi != nullptr) {
            return;
        }

        spi_device_interface_config_t devcfg = {};
        devcfg.clock_speed_hz = spi_speed_hz;
        devcfg.mode = 0;
        devcfg.spics_io_num = -1;
        devcfg.queue_size = 1;

        ESP_LOGI(TAG, "LR1121 shared SPI pins: SCK=%d MISO=%d MOSI=%d NSS=%d",
                 spi_sck, spi_miso, spi_mosi, radio_nss);
        esp_err_t ret = spi_bus_add_device(spi_host, &devcfg, &spi);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "add SPI device failed: %s", esp_err_to_name(ret));
            spi = nullptr;
        }
    }

    void spiBeginTransaction() {}

    uint8_t spiTransferByte(uint8_t b) {
        uint8_t rx = 0;
        spiTransfer(&b, 1, &rx);
        return rx;
    }

    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) {
        if (!spi) {
            ESP_LOGE(TAG, "SPI transfer before init");
            return;
        }

        spi_transaction_t t = {};
        t.length = len * 8;
        t.tx_buffer = out;
        t.rx_buffer = in;
        esp_err_t ret = spi_device_transmit(spi, &t);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPI transfer failed: %s", esp_err_to_name(ret));
        }
    }

    void spiEndTransaction() {}

    void spiEnd() {
        if (spi) {
            esp_err_t ret = spi_bus_remove_device(spi);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "remove SPI device failed: %s", esp_err_to_name(ret));
            }
            spi = nullptr;
        }
    }

private:
    struct InterruptSlot {
        uint32_t pin;
        void (*callback)(void);
    };

    static void IRAM_ATTR gpioIsrTrampoline(void *arg) {
        InterruptSlot *slot = static_cast<InterruptSlot *>(arg);
        if (slot && slot->callback) {
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
    bool isr_service_installed;
    InterruptSlot interrupt_slot = {RADIOLIB_NC, nullptr};
};

static board_radio_hw_config_t s_hw_config;
static EspIdfRadioHal *s_hal;
static Module *s_module;
static LR1121 *s_radio;

static esp_err_t release_radio_pin(gpio_num_t gpio_num);

static void destroy_radio_objects(bool terminate_module)
{
    if (terminate_module && s_module) {
        s_module->term();
    }
    delete s_radio;
    delete s_module;
    delete s_hal;
    s_radio = nullptr;
    s_module = nullptr;
    s_hal = nullptr;
}

static esp_err_t ensure_radio_objects(void)
{
    if (s_radio) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(board_support_prepare(), TAG,
                        "prepare board support failed");
    ESP_RETURN_ON_ERROR(board_radio_get_hw_config(&s_hw_config),
                        TAG, "get LR1121 board config failed");

    const gpio_num_t radio_pins[] = {
        s_hw_config.dio1,
        s_hw_config.nss,
        s_hw_config.reset,
        s_hw_config.busy,
        s_hw_config.dio7,
        s_hw_config.dio8,
        s_hw_config.sck,
        s_hw_config.miso,
        s_hw_config.mosi,
    };
    for (size_t i = 0; i < sizeof(radio_pins) / sizeof(radio_pins[0]); i++) {
        ESP_RETURN_ON_ERROR(release_radio_pin(radio_pins[i]), TAG,
                            "release LR1121 GPIO%d isolation failed",
                            (int)radio_pins[i]);
    }

    s_hal = new (std::nothrow) EspIdfRadioHal(
        s_hw_config.spi_host,
        s_hw_config.sck,
        s_hw_config.miso,
        s_hw_config.mosi,
        s_hw_config.spi_speed_hz,
        s_hw_config.nss);
    if (!s_hal) {
        destroy_radio_objects(false);
        return ESP_ERR_NO_MEM;
    }
    s_module = new (std::nothrow) Module(s_hal,
                                         (uint32_t)s_hw_config.nss,
                                         (uint32_t)s_hw_config.dio1,
                                         (uint32_t)s_hw_config.reset,
                                         (uint32_t)s_hw_config.busy);
    if (!s_module) {
        destroy_radio_objects(false);
        return ESP_ERR_NO_MEM;
    }
    s_radio = new (std::nothrow) LR1121(s_module);
    if (!s_radio) {
        destroy_radio_objects(false);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

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

static bool s_radio_initialized;
static board_radio_config_t s_config = BOARD_RADIO_CONFIG_DEFAULT();
static uint32_t s_tx_count;
static uint32_t s_rx_count;
static float s_last_rssi;
static float s_last_snr;
static volatile bool s_operation_done;

static void radio_operation_done(void)
{
    s_operation_done = true;
}

static bool wait_for_radio_operation(uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms > 0 && timeout_ticks == 0) {
        timeout_ticks = 1;
    }

    while (!s_operation_done) {
        if (timeout_ms > 0 && (xTaskGetTickCount() - start) >= timeout_ticks) {
            return false;
        }
        vTaskDelay(1);
    }
    return true;
}

static int16_t configure_radio(void)
{
    s_radio->setRfSwitchTable(s_rfswitch_dio_pins, s_rfswitch_table);
    s_radio->setIrqAction(radio_operation_done);

    int16_t state = s_radio->setFrequency(s_config.frequency_mhz);
    if (state != RADIOLIB_ERR_NONE) {
        return state;
    }
    state = s_radio->setOutputPower(s_config.output_power_dbm);
    if (state != RADIOLIB_ERR_NONE) {
        return state;
    }
    state = s_radio->setBandwidth(s_config.bandwidth_khz);
    if (state != RADIOLIB_ERR_NONE) {
        return state;
    }
    state = s_radio->setSpreadingFactor(s_config.spreading_factor);
    if (state != RADIOLIB_ERR_NONE) {
        return state;
    }
    state = s_radio->setCodingRate(s_config.coding_rate);
    if (state != RADIOLIB_ERR_NONE) {
        return state;
    }
    state = s_radio->setSyncWord(RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE);
    if (state != RADIOLIB_ERR_NONE) {
        return state;
    }
    return s_radio->setPreambleLength(s_config.preamble_len);
}

static void fill_status(board_radio_status_t *status, int16_t state, const char *payload)
{
    if (!status) {
        return;
    }
    status->state = state;
    status->initialized = s_radio_initialized;
    status->tx_count = s_tx_count;
    status->rx_count = s_rx_count;
    status->last_rssi = s_last_rssi;
    status->last_snr = s_last_snr;
    status->config = s_config;
    if (payload) {
        snprintf(status->last_payload, sizeof(status->last_payload), "%s", payload);
    }
}

esp_err_t board_radio_init(const board_radio_config_t *config, board_radio_status_t *status)
{
    if (s_radio_initialized) {
        fill_status(status, RADIOLIB_ERR_NONE, NULL);
        return ESP_OK;
    }

    if (config) {
        s_config = *config;
    }
    ESP_RETURN_ON_ERROR(ensure_radio_objects(), TAG, "create LR1121 adapter failed");

    ESP_LOGI(TAG, "LR1121 cfg: %.1f MHz BW=%.1f kHz SF=%u CR=%u PWR=%d TCXO=%.1fV",
             (double)s_config.frequency_mhz,
             (double)s_config.bandwidth_khz,
             (unsigned int)s_config.spreading_factor,
             (unsigned int)s_config.coding_rate,
             (int)s_config.output_power_dbm,
             (double)s_config.tcxo_voltage);

    s_operation_done = false;
    if (!board_spi_lock(BOARD_SPI_OWNER_RADIO, -1)) {
        fill_status(status, RADIOLIB_ERR_UNKNOWN, "spi lock timeout");
        return ESP_ERR_TIMEOUT;
    }

    int16_t state = s_radio->begin(s_config.frequency_mhz,
                                   s_config.bandwidth_khz,
                                   s_config.spreading_factor,
                                   s_config.coding_rate,
                                   RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE,
                                   s_config.output_power_dbm,
                                   s_config.preamble_len,
                                   s_config.tcxo_voltage);
    if (state == RADIOLIB_ERR_NONE) {
        state = configure_radio();
    }
    board_spi_unlock(BOARD_SPI_OWNER_RADIO);

    fill_status(status, state, NULL);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "LR1121 init failed: %d", state);
        destroy_radio_objects(true);
        return ESP_FAIL;
    }

    s_radio_initialized = true;
    fill_status(status, state, "ready");
    return ESP_OK;
}

esp_err_t board_radio_send(board_radio_status_t *status, const char *payload)
{
    ESP_RETURN_ON_FALSE(payload && payload[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "payload is empty");
    ESP_RETURN_ON_ERROR(board_radio_init(NULL, status), TAG, "radio init failed");

    const size_t max_payload_len = sizeof(((board_radio_status_t *)nullptr)->last_payload);
    size_t payload_len = strnlen(payload, max_payload_len);
    ESP_RETURN_ON_FALSE(payload_len < max_payload_len, ESP_ERR_INVALID_SIZE, TAG, "payload is too long");

    s_operation_done = false;
    if (!board_spi_lock(BOARD_SPI_OWNER_RADIO, -1)) {
        fill_status(status, RADIOLIB_ERR_UNKNOWN, "spi lock timeout");
        return ESP_ERR_TIMEOUT;
    }

    int16_t state = s_radio->startTransmit((const uint8_t *)payload, payload_len);
    board_spi_unlock(BOARD_SPI_OWNER_RADIO);

    if (state == RADIOLIB_ERR_NONE) {
        bool completed = wait_for_radio_operation(BOARD_RADIO_TX_TIMEOUT_MS);
        if (!board_spi_lock(BOARD_SPI_OWNER_RADIO, -1)) {
            state = RADIOLIB_ERR_UNKNOWN;
        } else {
            int16_t finish_state = s_radio->finishTransmit();
            board_spi_unlock(BOARD_SPI_OWNER_RADIO);
            if (!completed) {
                state = RADIOLIB_ERR_TX_TIMEOUT;
            } else if (finish_state != RADIOLIB_ERR_NONE) {
                state = finish_state;
            }
        }
    }

    if (state == RADIOLIB_ERR_NONE) {
        s_tx_count++;
        fill_status(status, state, payload);
        return ESP_OK;
    }

    fill_status(status, state, payload);
    ESP_LOGW(TAG, "transmit failed: %d", state);
    return ESP_FAIL;
}

esp_err_t board_radio_ping(board_radio_status_t *status)
{
    char payload[48];
    snprintf(payload, sizeof(payload), "PING %" PRIu32, (uint32_t)(s_tx_count + 1));
    return board_radio_send(status, payload);
}

esp_err_t board_radio_receive(board_radio_status_t *status, uint32_t timeout_ms)
{
    ESP_RETURN_ON_ERROR(board_radio_init(NULL, status), TAG, "radio init failed");

    char payload[80] = {0};
    s_operation_done = false;
    if (!board_spi_lock(BOARD_SPI_OWNER_RADIO, -1)) {
        fill_status(status, RADIOLIB_ERR_UNKNOWN, "spi lock timeout");
        return ESP_ERR_TIMEOUT;
    }

    int16_t state = s_radio->startReceive();
    board_spi_unlock(BOARD_SPI_OWNER_RADIO);

    if (state != RADIOLIB_ERR_NONE) {
        fill_status(status, state, board_radio_state_name(state));
        return ESP_FAIL;
    }

    if (!wait_for_radio_operation(timeout_ms)) {
        if (!board_spi_lock(BOARD_SPI_OWNER_RADIO, -1)) {
            fill_status(status, RADIOLIB_ERR_UNKNOWN, "spi lock timeout");
            return ESP_ERR_TIMEOUT;
        }
        int16_t finish_state = s_radio->finishReceive();
        board_spi_unlock(BOARD_SPI_OWNER_RADIO);
        state = (finish_state == RADIOLIB_ERR_NONE) ? RADIOLIB_ERR_RX_TIMEOUT : finish_state;
        fill_status(status, state, board_radio_state_name(state));
        return (state == RADIOLIB_ERR_RX_TIMEOUT) ? ESP_ERR_TIMEOUT : ESP_FAIL;
    }

    if (!board_spi_lock(BOARD_SPI_OWNER_RADIO, -1)) {
        fill_status(status, RADIOLIB_ERR_UNKNOWN, "spi lock timeout");
        return ESP_ERR_TIMEOUT;
    }

    size_t payload_len = s_radio->getPacketLength();
    if (payload_len >= sizeof(payload)) {
        payload_len = sizeof(payload) - 1;
    }
    state = s_radio->readData((uint8_t *)payload, payload_len);
    payload[payload_len] = '\0';
    if (state == RADIOLIB_ERR_NONE) {
        s_last_rssi = s_radio->getRSSI();
        s_last_snr = s_radio->getSNR();
    }
    int16_t finish_state = s_radio->finishReceive();
    board_spi_unlock(BOARD_SPI_OWNER_RADIO);

    if (state == RADIOLIB_ERR_NONE && finish_state != RADIOLIB_ERR_NONE) {
        state = finish_state;
    }

    if (state == RADIOLIB_ERR_NONE) {
        s_rx_count++;
        fill_status(status, state, payload);
        return ESP_OK;
    }

    fill_status(status, state, board_radio_state_name(state));
    return (state == RADIOLIB_ERR_RX_TIMEOUT) ? ESP_ERR_TIMEOUT : ESP_FAIL;
}

esp_err_t board_radio_sleep(void)
{
    if (!s_radio_initialized) {
        return ESP_OK;
    }

    if (!board_spi_lock(BOARD_SPI_OWNER_RADIO, -1)) {
        return ESP_ERR_TIMEOUT;
    }

    int16_t state = s_radio->sleep();
    s_radio->clearIrqAction();
    s_module->term();
    board_spi_unlock(BOARD_SPI_OWNER_RADIO);
    s_radio_initialized = false;
    s_operation_done = false;
    destroy_radio_objects(false);

    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGW(TAG, "LR1121 sleep returned %d", state);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t release_radio_pin(gpio_num_t gpio_num)
{
    if (gpio_num == GPIO_NUM_NC) {
        return ESP_OK;
    }

    esp_err_t err = gpio_hold_dis(gpio_num);
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
        return err;
    }
    if (rtc_gpio_is_valid_gpio(gpio_num)) {
        return rtc_gpio_deinit(gpio_num);
    }
    return ESP_OK;
}

static esp_err_t hold_radio_pin(gpio_num_t gpio_num)
{
    esp_err_t err = gpio_hold_en(gpio_num);
    return err == ESP_ERR_NOT_SUPPORTED ? ESP_OK : err;
}

static esp_err_t hold_radio_output(gpio_num_t gpio_num, uint32_t level)
{
    if (gpio_num == GPIO_NUM_NC) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(release_radio_pin(gpio_num), TAG,
                        "release GPIO%d hold failed", (int)gpio_num);
    ESP_RETURN_ON_ERROR(gpio_set_level(gpio_num, level), TAG,
                        "preload GPIO%d for deep sleep failed", (int)gpio_num);
    gpio_config_t config = {};
    config.pin_bit_mask = BIT64(gpio_num);
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG,
                        "configure GPIO%d for deep sleep failed", (int)gpio_num);
    ESP_RETURN_ON_ERROR(gpio_set_level(gpio_num, level), TAG,
                        "drive GPIO%d for deep sleep failed", (int)gpio_num);
    return hold_radio_pin(gpio_num);
}

static esp_err_t disable_radio_pin(gpio_num_t gpio_num)
{
    if (gpio_num == GPIO_NUM_NC) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(release_radio_pin(gpio_num), TAG,
                        "release GPIO%d hold failed", (int)gpio_num);
    gpio_config_t config = {};
    config.pin_bit_mask = BIT64(gpio_num);
    config.mode = GPIO_MODE_DISABLE;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG,
                        "disable GPIO%d for deep sleep failed", (int)gpio_num);
    return hold_radio_pin(gpio_num);
}

static esp_err_t hold_radio_control_pins(void)
{
    // NSS must stay high so a falling edge cannot wake LR1121 from Sleep.
    ESP_RETURN_ON_ERROR(hold_radio_output(s_hw_config.nss, 1), TAG,
                        "hold LR1121 NSS high failed");
    ESP_RETURN_ON_ERROR(hold_radio_output(s_hw_config.reset, 1), TAG,
                        "hold LR1121 RESET high failed");

    // These pins are driven by LR1121 or high-impedance while it sleeps.
    ESP_RETURN_ON_ERROR(disable_radio_pin(s_hw_config.dio1), TAG,
                        "disable LR1121 DIO1 failed");
    ESP_RETURN_ON_ERROR(disable_radio_pin(s_hw_config.busy), TAG,
                        "disable LR1121 BUSY failed");
    ESP_RETURN_ON_ERROR(disable_radio_pin(s_hw_config.miso), TAG,
                        "disable LR1121 MISO failed");

    return ESP_OK;
}

esp_err_t board_radio_hold_sleep_pins(void)
{
    ESP_RETURN_ON_ERROR(board_radio_get_hw_config(&s_hw_config), TAG,
                        "get LR1121 board config failed");
    ESP_RETURN_ON_ERROR(hold_radio_control_pins(), TAG,
                        "hold LR1121 control pins failed");
    ESP_RETURN_ON_ERROR(hold_radio_output(s_hw_config.sck, 0), TAG,
                        "hold LR1121 SCK low failed");
    ESP_RETURN_ON_ERROR(hold_radio_output(s_hw_config.mosi, 0), TAG,
                        "hold LR1121 MOSI low failed");
    return ESP_OK;
}

esp_err_t board_radio_isolate_powered_off_pins(void)
{
    esp_err_t ret = board_radio_get_hw_config(&s_hw_config);
    if (ret != ESP_OK) {
        return ret;
    }

    const gpio_num_t radio_pins[] = {
        s_hw_config.dio1,
        s_hw_config.nss,
        s_hw_config.reset,
        s_hw_config.busy,
        s_hw_config.dio7,
        s_hw_config.dio8,
        s_hw_config.sck,
        s_hw_config.miso,
        s_hw_config.mosi,
    };
    for (size_t i = 0; i < sizeof(radio_pins) / sizeof(radio_pins[0]); i++) {
        ret = disable_radio_pin(radio_pins[i]);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    return ESP_OK;
}

esp_err_t board_radio_prepare_deep_sleep(void)
{
    ESP_RETURN_ON_ERROR(board_radio_sleep(), TAG, "put LR1121 to sleep failed");
    ESP_RETURN_ON_ERROR(board_radio_get_hw_config(&s_hw_config), TAG,
                        "get LR1121 board config failed");

    // Shared SCK/MOSI may still be needed by the e-paper. Hold them only after
    // every SPI user is shut down by calling board_radio_hold_sleep_pins().
    return hold_radio_control_pins();
}

const char *board_radio_state_name(int16_t state)
{
    switch (state) {
    case RADIOLIB_ERR_NONE:
        return "OK";
    case RADIOLIB_ERR_CHIP_NOT_FOUND:
        return "chip not found";
    case RADIOLIB_ERR_RX_TIMEOUT:
        return "rx timeout";
    case RADIOLIB_ERR_TX_TIMEOUT:
        return "tx timeout";
    case RADIOLIB_ERR_CRC_MISMATCH:
        return "crc mismatch";
    case RADIOLIB_ERR_SPI_CMD_FAILED:
        return "spi cmd failed";
    case RADIOLIB_ERR_INVALID_FREQUENCY:
        return "invalid freq";
    case RADIOLIB_ERR_INVALID_BANDWIDTH:
        return "invalid bw";
    case RADIOLIB_ERR_INVALID_SPREADING_FACTOR:
        return "invalid sf";
    case RADIOLIB_ERR_INVALID_OUTPUT_POWER:
        return "invalid power";
    default:
        return "error";
    }
}
