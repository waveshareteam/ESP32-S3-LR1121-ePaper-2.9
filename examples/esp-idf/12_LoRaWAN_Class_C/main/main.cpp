/**
 * @file main.cpp
 * @brief ESP-IDF entry point for the LoRaWAN Class C example.
 */

#include "lorawan_app.h"

extern "C" void app_main(void)
{
    lorawan::start_application();
}
