#pragma once

#include <Arduino.h>

namespace GatewayConfig {

// Hardware UART between the nRF52840 and ESP32-C3.
// This generic board is compiled with the Feather nRF52840 variant, where:
//   Arduino D11 -> physical nRF P0.06 -> nRF RX (connect to ESP TX)
//   Arduino D12 -> physical nRF P0.08 -> nRF TX (connect to ESP RX)
constexpr uint8_t UART_RX_ARDUINO_PIN = 11;
constexpr uint8_t UART_TX_ARDUINO_PIN = 12;
constexpr uint32_t UART_BAUD = 921600;

// USB is only a diagnostic mirror and accepts the same commands as UART.
constexpr uint32_t USB_BAUD = 921600;
constexpr bool USB_MIRROR_ENABLED = true;

// Generic modem parser limit. RF protocol values live in src/protocols/.
constexpr size_t MAX_LINE_LENGTH = 512;

// Confirmed active-high user LED on the SNE-TB170 / Nice!Nano-compatible board.
constexpr uint32_t LED_PIN_MASK = 1UL << 15; // physical P0.15
constexpr uint32_t RX_LED_TIME_MS = 25;

} // namespace GatewayConfig
