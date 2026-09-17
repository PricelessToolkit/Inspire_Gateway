#pragma once

#include <Arduino.h>

namespace GatewayConfig {

// ---------------------------------------------------------------------------
// Edit this one file for your installation.
// Credentials are stored as plain text in the ESP32 flash and in this source.
// ---------------------------------------------------------------------------

constexpr char WIFI_SSID[] = "HATEST";
constexpr char WIFI_PASSWORD[] = "Hatest086852A";

// An IP address (for example "192.168.1.10") or DNS name is accepted.
constexpr char MQTT_HOST[] = "192.168.99.2";
constexpr uint16_t MQTT_PORT = 1883;
constexpr char MQTT_USERNAME[] = "test";
constexpr char MQTT_PASSWORD[] = "test123";
constexpr char MQTT_CLIENT_ID_PREFIX[] = "inspire-gateway";

// Local web interface. Change these before sharing the device on an
// untrusted network. The interface never creates its own access point.
constexpr char WEB_USERNAME[] = "admin";
constexpr char WEB_PASSWORD[] = "inspire-gateway";

// Home Assistant device identity and MQTT discovery.
constexpr char DEVICE_NAME[] = "Inspire-Gateway";
constexpr char DEVICE_MANUFACTURER[] = "PricelessToolkit";
constexpr char DEVICE_MODEL[] = "Inspire Gateway";
constexpr char FIRMWARE_VERSION[] = "1.0.0";
constexpr char HA_DEVICE_ID[] = "inspire_gateway";
constexpr bool HA_DISCOVERY_ENABLED = true;

constexpr char MQTT_TX_TOPIC[] = "inspire-gateway/tx";
constexpr char MQTT_RX_TOPIC[] = "inspire-gateway/rx";
constexpr char MQTT_RSSI_TOPIC[] = "inspire-gateway/rssi";
constexpr char MQTT_AVAILABILITY_TOPIC[] = "inspire-gateway/availability";
constexpr char HA_STATUS_TOPIC[] = "homeassistant/status";
constexpr char HA_DEVICE_DISCOVERY_TOPIC[] = "homeassistant/device/inspire-gateway/config";

// XIAO ESP32-C3: D10/GPIO10 is RX and D9/GPIO9 is TX.
constexpr int NRF_UART_RX_PIN = 10; // connect to nRF P0.08 TX
constexpr int NRF_UART_TX_PIN = 9; // connect to nRF P0.06 RX
constexpr uint32_t NRF_UART_BAUD = 921600;
constexpr uint32_t USB_SERIAL_BAUD = 115200;

constexpr uint16_t MQTT_BUFFER_BYTES = 3072;
constexpr uint16_t MQTT_KEEPALIVE_SECONDS = 30;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;
constexpr uint32_t WIFI_RSSI_PUBLISH_INTERVAL_MS = 30000;
constexpr uint32_t MQTT_RECONNECT_INTERVAL_MS = 5000;

constexpr size_t UART_LINE_MAX = 255;
constexpr size_t RX_QUEUE_DEPTH = 8;
constexpr size_t RX_JSON_MAX = 384;

} // namespace GatewayConfig
