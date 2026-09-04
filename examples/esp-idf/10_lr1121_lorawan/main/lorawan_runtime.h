/*
 * Long-running LoRaWAN Class A workflow and timing helpers.
 *
 * The runtime serializes radio, NVS, and e-paper work on one task so display
 * updates cannot overlap RadioLib's synchronous RX1/RX2 windows.
 */
#pragma once

#include <stdint.h>

#include "lorawan_node.h"
#include "lorawan_ui.h"

/** Convert common RadioLib states into short diagnostic strings. */
const char *radiolib_state_name(int16_t state);

/** Convert seconds to milliseconds with saturation at UINT32_MAX. */
uint32_t lorawan_seconds_to_ms(uint32_t seconds);
/** Delay for an arbitrary 32-bit duration using bounded FreeRTOS chunks. */
void lorawan_delay_ms(uint32_t duration_ms);

/** Stop radio progress after a non-recoverable local configuration error. */
[[noreturn]] void lorawan_wait_for_configuration(void);

/**
 * Join or restore the network, then run periodic Class A uplinks forever.
 */
[[noreturn]] void lorawan_runtime_run(ConfiguredLoRaWANNode &node,
                                      LR1121 &radio,
                                      LoRaWANStatusUi &ui);
