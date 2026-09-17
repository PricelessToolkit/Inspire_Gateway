/*
 * Inspire Gateway ESP32-C3 controller.
 *
 * The ESP owns Wi-Fi, MQTT, Home Assistant discovery, validation, and decoding.
 * The nRF52840 is a runtime-plugin RF modem.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "web_ui.h"

using namespace GatewayConfig;

WiFiClient networkClient;
PubSubClient mqtt(networkClient);
WebServer webServer(80);
Preferences preferences;

struct RuntimeConfig {
  char wifiSsid[33];
  char wifiPassword[65];
  char mqttHost[65];
  uint16_t mqttPort;
  char mqttUsername[65];
  char mqttPassword[65];
} runtimeConfig;

char uartLine[UART_LINE_MAX + 1];
size_t uartLineLength = 0;
bool uartLineOverflow = false;

uint32_t lastWifiAttemptMs = 0;
uint32_t lastMqttAttemptMs = 0;
uint32_t nrfBusyUntilMs = 0;
bool discoveryPending = false;
bool rssiPending = false;
bool wifiConnectionReported = false;
int latestRssi = 0;
uint32_t lastRssiSampleMs = 0;
uint32_t restartAtMs = 0;
char lastRxJson[RX_JSON_MAX] = {};
size_t lastRxJsonLength = 0;
uint32_t lastRxAtMs = 0;
uint32_t lastRxSequence = 0;
portMUX_TYPE lastRxMux = portMUX_INITIALIZER_UNLOCKED;

char rxQueue[RX_QUEUE_DEPTH][RX_JSON_MAX];
size_t rxQueueLength[RX_QUEUE_DEPTH] = {};
size_t rxQueueHead = 0;
size_t rxQueueCount = 0;

int hexNibble(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

bool parseHex(const char *text, uint8_t *output, size_t capacity,
              size_t &outputLength, size_t requiredLength = 0) {
  if (!text) return false;
  const size_t characters = strlen(text);
  if (!characters || (characters & 1U)) return false;
  const size_t bytes = characters / 2;
  if (bytes > capacity || (requiredLength && bytes != requiredLength))
    return false;
  for (size_t index = 0; index < bytes; ++index) {
    const int high = hexNibble(text[index * 2]);
    const int low = hexNibble(text[index * 2 + 1]);
    if (high < 0 || low < 0) return false;
    output[index] = uint8_t((high << 4) | low);
  }
  outputLength = bytes;
  return true;
}

bool isExactHex(const char *text, size_t characters) {
  if (!text || strlen(text) != characters) return false;
  for (size_t index = 0; index < characters; ++index)
    if (hexNibble(text[index]) < 0) return false;
  return true;
}

bool isSafeProtocolName(const char *text) {
  if (!text || !*text || strlen(text) > 24) return false;
  for (const char *character = text; *character; ++character)
    if (!isalnum(static_cast<unsigned char>(*character)) &&
        *character != '_' && *character != '-')
      return false;
  return true;
}

void copySetting(char *destination, size_t capacity, const String &value) {
  value.toCharArray(destination, capacity);
}

void loadRuntimeConfig() {
  preferences.begin("gateway", true);
  copySetting(runtimeConfig.wifiSsid, sizeof(runtimeConfig.wifiSsid),
              preferences.getString("wifi_ssid", WIFI_SSID));
  copySetting(runtimeConfig.wifiPassword, sizeof(runtimeConfig.wifiPassword),
              preferences.getString("wifi_pass", WIFI_PASSWORD));
  copySetting(runtimeConfig.mqttHost, sizeof(runtimeConfig.mqttHost),
              preferences.getString("mqtt_host", MQTT_HOST));
  runtimeConfig.mqttPort = preferences.getUShort("mqtt_port", MQTT_PORT);
  copySetting(runtimeConfig.mqttUsername, sizeof(runtimeConfig.mqttUsername),
              preferences.getString("mqtt_user", MQTT_USERNAME));
  copySetting(runtimeConfig.mqttPassword, sizeof(runtimeConfig.mqttPassword),
              preferences.getString("mqtt_pass", MQTT_PASSWORD));
  preferences.end();
}

bool webAuthenticated() {
  if (webServer.authenticate(WEB_USERNAME, WEB_PASSWORD)) return true;
  webServer.requestAuthentication();
  return false;
}

void sendJsonDocument(int status, JsonDocument &document) {
  String body;
  serializeJson(document, body);
  webServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "0");
  webServer.send(status, "application/json", body);
}

void sendApiError(int status, const char *message) {
  JsonDocument document;
  document["ok"] = false;
  document["error"] = message;
  sendJsonDocument(status, document);
}

uint32_t getBitsMsb(const uint8_t *bytes, unsigned offset, unsigned count) {
  uint32_t result = 0;
  for (unsigned index = 0; index < count; ++index) {
    const unsigned bit = offset + index;
    result = (result << 1) |
             ((bytes[bit / 8] >> (7 - (bit % 8))) & 1U);
  }
  return result;
}

uint16_t crcFeedBit(uint16_t crc, uint8_t bit) {
  const bool feedback = ((crc >> 15) & 1U) ^ (bit & 1U);
  crc = uint16_t(crc << 1);
  if (feedback) crc ^= 0x1021;
  return crc;
}

uint16_t protocolCrc(const uint8_t address[5], const uint8_t payload[10]) {
  uint16_t crc = 0xFFFF;
  for (size_t byte = 0; byte < 5; ++byte)
    for (int bit = 7; bit >= 0; --bit)
      crc = crcFeedBit(crc, (address[byte] >> bit) & 1U);

  // PCF: length=12, PID=2, NO_ACK=1 -> 001100101.
  constexpr uint16_t pcf = 0b001100101;
  for (int bit = 8; bit >= 0; --bit)
    crc = crcFeedBit(crc, (pcf >> bit) & 1U);

  for (size_t byte = 0; byte < 10; ++byte)
    for (int bit = 7; bit >= 0; --bit)
      crc = crcFeedBit(crc, (payload[byte] >> bit) & 1U);
  return crc ^ 0xB294;
}

const char *actionName(uint16_t action) {
  switch (action) {
    case 0x11B1: return "ON";
    case 0x11B2: return "OFF";
    case 0x11B3: return "BRIGHT_UP";
    case 0x11B4: return "BRIGHT_DOWN";
    case 0x11B5: return "WARM";
    case 0x11B6: return "COLD";
    case 0x11BD: return "DEFAULT";
    default: return "OTHER";
  }
}

void queueRxJson(const char *json, size_t length) {
  if (length >= RX_JSON_MAX) return;
  if (rxQueueCount == RX_QUEUE_DEPTH) {
    rxQueueHead = (rxQueueHead + 1) % RX_QUEUE_DEPTH;
    --rxQueueCount;
    if (Serial) Serial.println("RX queue full: discarded oldest event");
  }
  const size_t tail = (rxQueueHead + rxQueueCount) % RX_QUEUE_DEPTH;
  memcpy(rxQueue[tail], json, length);
  rxQueue[tail][length] = 0;
  rxQueueLength[tail] = length;
  ++rxQueueCount;
}

void publishOrQueueRx(const char *json, size_t length) {
  if (mqtt.connected() &&
      mqtt.publish(MQTT_RX_TOPIC, reinterpret_cast<const uint8_t *>(json),
                   unsigned(length), false)) {
    if (Serial) {
      Serial.printf("MQTT RX published at %lu ms: ",
                    (unsigned long)millis());
      Serial.write(reinterpret_cast<const uint8_t *>(json), length);
      Serial.println();
    }
    return;
  }
  if (Serial) {
    Serial.printf("MQTT RX queued at %lu ms: ", (unsigned long)millis());
    Serial.write(reinterpret_cast<const uint8_t *>(json), length);
    Serial.println();
  }
  queueRxJson(json, length);
}

void flushRxQueue() {
  while (mqtt.connected() && rxQueueCount) {
    if (!mqtt.publish(MQTT_RX_TOPIC,
                      reinterpret_cast<const uint8_t *>(rxQueue[rxQueueHead]),
                      unsigned(rxQueueLength[rxQueueHead]), false))
      return;
    if (Serial) {
      Serial.printf("MQTT queued RX published at %lu ms: ",
                    (unsigned long)millis());
      Serial.write(reinterpret_cast<const uint8_t *>(rxQueue[rxQueueHead]),
                   rxQueueLength[rxQueueHead]);
      Serial.println();
    }
    rxQueueHead = (rxQueueHead + 1) % RX_QUEUE_DEPTH;
    --rxQueueCount;
    mqtt.loop();
  }
}

void addDiscoveryMetadata(JsonDocument &document) {
  JsonObject device = document["device"].to<JsonObject>();
  JsonArray identifiers = device["identifiers"].to<JsonArray>();
  identifiers.add(HA_DEVICE_ID);
  device["name"] = DEVICE_NAME;
  device["manufacturer"] = DEVICE_MANUFACTURER;
  device["model"] = DEVICE_MODEL;
  device["sw_version"] = FIRMWARE_VERSION;

  JsonObject origin = document["origin"].to<JsonObject>();
  origin["name"] = DEVICE_MANUFACTURER;
  origin["sw_version"] = FIRMWARE_VERSION;
}

bool publishDiscoveryDocument(const char *topic, JsonDocument &document) {
  const size_t length = measureJson(document);
  if (!length || !mqtt.beginPublish(topic, unsigned(length), true)) return false;
  const size_t written = serializeJson(document, mqtt);
  return written == length && mqtt.endPublish();
}

JsonObject addRxDiagnosticSensor(JsonObject components,
                                 const char *componentId,
                                 const char *name,
                                 const char *uniqueId,
                                 const char *field) {
  JsonObject sensor = components[componentId].to<JsonObject>();
  sensor["platform"] = "sensor";
  sensor["name"] = name;
  sensor["unique_id"] = uniqueId;
  sensor["state_topic"] = MQTT_RX_TOPIC;
  sensor["value_template"] = String("{{ value_json.") + field + " }}";
  return sensor;
}

bool publishHomeAssistantDiscovery() {
  if (!HA_DISCOVERY_ENABLED || !mqtt.connected()) return true;

  JsonDocument discovery;
  addDiscoveryMetadata(discovery);
  discovery["availability_topic"] = MQTT_AVAILABILITY_TOPIC;
  discovery["payload_available"] = "online";
  discovery["payload_not_available"] = "offline";

  JsonObject components = discovery["components"].to<JsonObject>();
  JsonObject rssi = components["wifi_rssi"].to<JsonObject>();
  rssi["platform"] = "sensor";
  rssi["name"] = "Wi-Fi RSSI";
  rssi["unique_id"] = "inspire_gateway_rssi";
  rssi["state_topic"] = MQTT_RSSI_TOPIC;
  rssi["device_class"] = "signal_strength";
  rssi["state_class"] = "measurement";
  rssi["unit_of_measurement"] = "dBm";
  rssi["icon"] = "mdi:signal";
  rssi["entity_category"] = "diagnostic";

  JsonObject rxAddress = components["rx_address"].to<JsonObject>();
  rxAddress["platform"] = "sensor";
  rxAddress["name"] = "Address";
  rxAddress["unique_id"] = "inspire_gateway_rx_address";
  rxAddress["state_topic"] = MQTT_RX_TOPIC;
  rxAddress["value_template"] = "{{ value_json.address }}";
  rxAddress["icon"] = "mdi:identifier";

  JsonObject rxProtocol = components["rx_protocol"].to<JsonObject>();
  rxProtocol["platform"] = "sensor";
  rxProtocol["name"] = "Protocol";
  rxProtocol["unique_id"] = "inspire_gateway_rx_protocol";
  rxProtocol["state_topic"] = MQTT_RX_TOPIC;
  rxProtocol["value_template"] = "{{ value_json.protocol }}";
  rxProtocol["icon"] = "mdi:access-point";

  JsonObject rxRssi = components["rx_rssi"].to<JsonObject>();
  rxRssi["platform"] = "sensor";
  rxRssi["name"] = "RF RSSI";
  rxRssi["unique_id"] = "inspire_gateway_rx_rssi";
  rxRssi["state_topic"] = MQTT_RX_TOPIC;
  rxRssi["value_template"] = "{{ value_json.rssi }}";
  rxRssi["device_class"] = "signal_strength";
  rxRssi["state_class"] = "measurement";
  rxRssi["unit_of_measurement"] = "dBm";
  rxRssi["icon"] = "mdi:signal-distance-variant";

  JsonObject rxPayload = components["rx_payload"].to<JsonObject>();
  rxPayload["platform"] = "sensor";
  rxPayload["name"] = "Payload";
  rxPayload["unique_id"] = "inspire_gateway_rx_payload";
  rxPayload["state_topic"] = MQTT_RX_TOPIC;
  rxPayload["value_template"] = "{{ value_json.payload }}";
  rxPayload["icon"] = "mdi:code-json";

  addRxDiagnosticSensor(components, "rx_length", "Payload Length",
                        "inspire_gateway_rx_length",
                        "length");
  addRxDiagnosticSensor(components, "rx_protocol_valid", "Protocol Valid",
                        "inspire_gateway_rx_protocol_valid",
                        "protocol_valid");
  addRxDiagnosticSensor(components, "rx_prefix", "Prefix",
                        "inspire_gateway_rx_prefix",
                        "prefix");
  addRxDiagnosticSensor(components, "rx_action", "Action",
                        "inspire_gateway_rx_action",
                        "action");
  addRxDiagnosticSensor(components, "rx_button", "Button",
                        "inspire_gateway_rx_button",
                        "button");
  addRxDiagnosticSensor(components, "rx_counter", "Counter",
                        "inspire_gateway_rx_counter",
                        "counter");
  addRxDiagnosticSensor(components, "rx_remaining", "Remote Packet Count",
                        "inspire_gateway_rx_remaining",
                        "remaining");
  addRxDiagnosticSensor(components, "rx_check", "Check",
                        "inspire_gateway_rx_check", "check");
  addRxDiagnosticSensor(components, "rx_crc_received", "Received CRC",
                        "inspire_gateway_rx_crc_received",
                        "crc_received");
  addRxDiagnosticSensor(components, "rx_crc_calculated", "Calculated CRC",
                        "inspire_gateway_rx_crc_calculated",
                        "crc_calculated");

  // Platform-only components remove entities published by older firmware.
  // Publish them once, then omit them from the authoritative device document.
  JsonObject removedRemote = components["remote"].to<JsonObject>();
  removedRemote["platform"] = "event";
  JsonObject removedS1 = components["rx_s1"].to<JsonObject>();
  removedS1["platform"] = "sensor";
  JsonObject removedLastRx = components["last_rx"].to<JsonObject>();
  removedLastRx["platform"] = "sensor";
  if (!publishDiscoveryDocument(HA_DEVICE_DISCOVERY_TOPIC, discovery)) return false;
  components.remove("remote");
  components.remove("rx_s1");
  components.remove("last_rx");
  if (!publishDiscoveryDocument(HA_DEVICE_DISCOVERY_TOPIC, discovery)) return false;

  if (Serial) Serial.println("Home Assistant discovery published");
  return true;
}

void publishRssiIfNeeded() {
  if (!rssiPending || !mqtt.connected()) return;
  char value[16];
  const int length = snprintf(value, sizeof(value), "%d", latestRssi);
  if (length > 0 && length < int(sizeof(value)) &&
      mqtt.publish(MQTT_RSSI_TOPIC,
                   reinterpret_cast<const uint8_t *>(value),
                   unsigned(length), true))
    rssiPending = false;
}

void sampleWifiRssiIfNeeded() {
  if (WiFi.status() != WL_CONNECTED) return;
  const uint32_t now = millis();
  if (lastRssiSampleMs &&
      uint32_t(now - lastRssiSampleMs) < WIFI_RSSI_PUBLISH_INTERVAL_MS)
    return;
  lastRssiSampleMs = now;
  latestRssi = WiFi.RSSI();
  rssiPending = true;
}

bool parseSigned(const char *text, int &result) {
  if (!text || !*text) return false;
  char *end = nullptr;
  const long parsed = strtol(text, &end, 10);
  if (!end || *end) return false;
  result = int(parsed);
  return true;
}

void handleNrfLine(char *line) {
  if (Serial) {
    Serial.print("nRF: ");
    Serial.println(line);
  }

  char original[UART_LINE_MAX + 1];
  strlcpy(original, line, sizeof(original));
  char *save = nullptr;
  char *type = strtok_r(line, " ", &save);
  if (!type || strcmp(type, "RX")) return;

  const char *addressText = nullptr;
  const char *payloadText = nullptr;
  const char *protocolText = nullptr;
  int rssi = 0;
  int length = -1;
  int s1 = -1;
  bool haveRssi = false;

  for (char *token = strtok_r(nullptr, " ", &save); token;
       token = strtok_r(nullptr, " ", &save)) {
    char *equals = strchr(token, '=');
    if (!equals) continue;
    *equals = 0;
    const char *value = equals + 1;
    if (!strcmp(token, "protocol")) protocolText = value;
    else if (!strcmp(token, "address")) addressText = value;
    else if (!strcmp(token, "payload")) payloadText = value;
    else if (!strcmp(token, "rssi")) haveRssi = parseSigned(value, rssi);
    else if (!strcmp(token, "len")) parseSigned(value, length);
    else if (!strcmp(token, "s1")) parseSigned(value, s1);
  }

  uint8_t address[5];
  uint8_t payload[12];
  size_t addressLength = 0;
  size_t payloadLength = 0;
  if (!haveRssi || !parseHex(addressText, address, sizeof(address),
                             addressLength, 5) ||
      !parseHex(payloadText, payload, sizeof(payload), payloadLength) ||
      length != int(payloadLength)) {
    if (Serial) {
      Serial.print("Ignored malformed nRF line: ");
      Serial.println(original);
    }
    return;
  }

  const bool protocolShape = protocolText &&
                             !strcmp(protocolText, "gdansk_inspire") &&
                             payloadLength == 12 && s1 == 5;
  const uint16_t receivedCrc = protocolShape
                                   ? uint16_t(payload[10] << 8) | payload[11]
                                   : 0;
  const uint16_t calculatedCrc = protocolShape ? protocolCrc(address, payload) : 0;
  const bool crcOk = protocolShape && receivedCrc == calculatedCrc;

  uint32_t prefix = 0;
  uint16_t action = 0;
  uint8_t counter = 0;
  uint16_t remaining = 0;
  uint16_t check = 0;
  if (protocolShape) {
    prefix = getBitsMsb(payload, 0, 32);
    action = uint16_t(getBitsMsb(payload, 32, 13));
    counter = uint8_t(getBitsMsb(payload, 45, 8));
    remaining = uint16_t(getBitsMsb(payload, 56, 13));
    check = uint16_t(getBitsMsb(payload, 69, 11));
  }

  JsonDocument document;
  document["protocol"] = protocolText ? protocolText : "unknown";
  document["address"] = addressText;
  document["rssi"] = rssi;
  document["length"] = payloadLength;
  document["s1"] = s1;
  document["payload"] = payloadText;
  document["protocol_valid"] = crcOk;
  document["event_type"] = protocolShape ? actionName(action) : "OTHER";
  if (protocolShape) {
    char prefixHex[9];
    char actionHex[7];
    char checkHex[6];
    snprintf(prefixHex, sizeof(prefixHex), "%08lX", (unsigned long)prefix);
    snprintf(actionHex, sizeof(actionHex), "0x%04X", action);
    snprintf(checkHex, sizeof(checkHex), "0x%03X", check);
    document["prefix"] = prefixHex;
    document["action"] = actionHex;
    document["button"] = actionName(action);
    document["counter"] = counter;
    document["remaining"] = remaining;
    document["check"] = checkHex;
    char receivedCrcHex[7];
    char calculatedCrcHex[7];
    snprintf(receivedCrcHex, sizeof(receivedCrcHex), "%04X", receivedCrc);
    snprintf(calculatedCrcHex, sizeof(calculatedCrcHex), "%04X", calculatedCrc);
    document["crc_received"] = receivedCrcHex;
    document["crc_calculated"] = calculatedCrcHex;
  }

  char json[RX_JSON_MAX];
  const size_t jsonLength = serializeJson(document, json, sizeof(json));
  if (jsonLength && jsonLength < sizeof(json)) {
    portENTER_CRITICAL(&lastRxMux);
    memcpy(lastRxJson, json, jsonLength + 1);
    lastRxJsonLength = jsonLength;
    lastRxAtMs = millis();
    ++lastRxSequence;
    portEXIT_CRITICAL(&lastRxMux);
    publishOrQueueRx(json, jsonLength);
  }
}

void processNrfUart() {
  while (Serial1.available()) {
    const char value = char(Serial1.read());
    if (value == '\r') continue;
    if (value == '\n') {
      if (!uartLineOverflow && uartLineLength) {
        uartLine[uartLineLength] = 0;
        handleNrfLine(uartLine);
      }
      uartLineLength = 0;
      uartLineOverflow = false;
    } else if (!uartLineOverflow) {
      if (uartLineLength < UART_LINE_MAX) uartLine[uartLineLength++] = value;
      else uartLineOverflow = true;
    }
  }
}

bool processTxJson(const uint8_t *input, size_t inputLength, String &message) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, input, inputLength);
  if (error || !document.is<JsonObject>()) {
    message = "Invalid JSON object";
    return false;
  }

  const char *protocol = document["protocol"] | static_cast<const char *>(nullptr);
  const char *address = document["address"] | static_cast<const char *>(nullptr);
  const char *payload = document["payload"] | static_cast<const char *>(nullptr);
  const int repeat = document["repeat"] | 0;
  if (!isSafeProtocolName(protocol) ||
      !isExactHex(address, 10) ||
      !payload || strlen(payload) < 2 || strlen(payload) > 376 ||
      (strlen(payload) & 1U) || !isExactHex(payload, strlen(payload)) ||
      repeat < 1 || repeat > 501) {
    message = "Protocol, address, payload, or repeat value is invalid";
    return false;
  }

  if (int32_t(millis() - nrfBusyUntilMs) < 0) {
    message = "nRF transmitter is still busy";
    return false;
  }

  char command[512];
  const int written = snprintf(command, sizeof(command),
      "TX protocol=%s address=%s repeat=%d payload=%s\n",
      protocol, address, repeat, payload);
  if (written <= 0 || written >= int(sizeof(command))) {
    message = "Generated nRF command is too long";
    return false;
  }
  Serial1.write(reinterpret_cast<const uint8_t *>(command), size_t(written));
  if (Serial) {
    Serial.print("Sent to nRF: ");
    Serial.print(command);
  }

  // nRF gives no completion response. Use the selected protocol's fixed
  // interval plus a small startup margin.
  const uint32_t intervalNs = !strcmp(protocol, "2ANO4-25W2R4G")
                                  ? 3406250UL
                                  : 3906250UL;
  nrfBusyUntilMs = millis() +
      uint32_t((uint64_t(repeat) * intervalNs) / 1000000ULL) + 10;
  message = "RF command accepted";
  return true;
}

void mqttCallback(char *topic, byte *input, unsigned int inputLength) {
  if (!strcmp(topic, HA_STATUS_TOPIC)) {
    if (inputLength == 6 && !memcmp(input, "online", 6))
      discoveryPending = true;
    return;
  }
  if (strcmp(topic, MQTT_TX_TOPIC)) return;

  String message;
  if (!processTxJson(input, inputLength, message) && Serial) {
    Serial.print("Rejected MQTT TX: ");
    Serial.println(message);
  }
}

void handleWebRoot() {
  if (!webAuthenticated()) return;
  webServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "0");
  webServer.send_P(200, "text/html; charset=utf-8", WEB_UI_HTML);
}

void handleApiStatus() {
  if (!webAuthenticated()) return;
  char rxSnapshot[RX_JSON_MAX] = {};
  size_t rxSnapshotLength;
  uint32_t rxSnapshotAtMs;
  uint32_t rxSnapshotSequence;
  portENTER_CRITICAL(&lastRxMux);
  rxSnapshotLength = lastRxJsonLength;
  rxSnapshotAtMs = lastRxAtMs;
  rxSnapshotSequence = lastRxSequence;
  if (rxSnapshotLength)
    memcpy(rxSnapshot, lastRxJson, rxSnapshotLength + 1);
  portEXIT_CRITICAL(&lastRxMux);

  JsonDocument document;
  JsonDocument latest;
  document["firmware"] = FIRMWARE_VERSION;
  document["uptime_ms"] = millis();
  document["free_heap"] = ESP.getFreeHeap();
  JsonObject wifi = document["wifi"].to<JsonObject>();
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  wifi["connected"] = wifiConnected;
  wifi["ssid"] = runtimeConfig.wifiSsid;
  wifi["ip"] = wifiConnected ? WiFi.localIP().toString() : "";
  wifi["rssi"] = wifiConnected ? WiFi.RSSI() : 0;
  JsonObject mqttStatus = document["mqtt"].to<JsonObject>();
  mqttStatus["connected"] = mqtt.connected();
  mqttStatus["host"] = runtimeConfig.mqttHost;
  mqttStatus["port"] = runtimeConfig.mqttPort;
  JsonObject rx = document["rx"].to<JsonObject>();
  rx["queued"] = rxQueueCount;
  rx["capacity"] = RX_QUEUE_DEPTH;
  rx["sequence"] = rxSnapshotSequence;
  if (rxSnapshotLength) {
    rx["last_age_ms"] = uint32_t(millis() - rxSnapshotAtMs);
    if (!deserializeJson(latest, rxSnapshot, rxSnapshotLength)) {
      rx["button"] = latest["button"] | latest["event_type"] | "OTHER";
    }
  } else {
    rx["last_age_ms"] = nullptr;
  }
  document["nrf_busy"] = int32_t(millis() - nrfBusyUntilMs) < 0;
  String body;
  serializeJson(document, body);
  if (rxSnapshotLength && body.endsWith("}")) {
    body.remove(body.length() - 1);
    body += F(",\"rx_data\":");
    body.concat(rxSnapshot, rxSnapshotLength);
    body += '}';
  }
  webServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "0");
  webServer.send(200, "application/json", body);
}

void handleApiLastRx() {
  if (!webAuthenticated()) return;
  char rxSnapshot[RX_JSON_MAX] = {};
  size_t rxSnapshotLength;
  portENTER_CRITICAL(&lastRxMux);
  rxSnapshotLength = lastRxJsonLength;
  if (rxSnapshotLength)
    memcpy(rxSnapshot, lastRxJson, rxSnapshotLength + 1);
  portEXIT_CRITICAL(&lastRxMux);
  if (!rxSnapshotLength) {
    JsonDocument document;
    document["available"] = false;
    sendJsonDocument(200, document);
    return;
  }
  webServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "0");
  webServer.send(200, "application/json", rxSnapshot);
}

void handleApiSettings() {
  if (!webAuthenticated()) return;
  JsonDocument document;
  document["wifi_ssid"] = runtimeConfig.wifiSsid;
  document["wifi_password_set"] = runtimeConfig.wifiPassword[0] != 0;
  document["mqtt_host"] = runtimeConfig.mqttHost;
  document["mqtt_port"] = runtimeConfig.mqttPort;
  document["mqtt_username"] = runtimeConfig.mqttUsername;
  document["mqtt_password_set"] = runtimeConfig.mqttPassword[0] != 0;
  sendJsonDocument(200, document);
}

void handleApiTx() {
  if (!webAuthenticated()) return;
  const String &body = webServer.arg("plain");
  String message;
  if (!processTxJson(reinterpret_cast<const uint8_t *>(body.c_str()),
                     body.length(), message)) {
    sendApiError(400, message.c_str());
    return;
  }
  JsonDocument response;
  response["ok"] = true;
  response["message"] = message;
  sendJsonDocument(202, response);
}

void handleApiSaveSettings() {
  if (!webAuthenticated()) return;
  JsonDocument document;
  if (deserializeJson(document, webServer.arg("plain")) ||
      !document.is<JsonObject>()) {
    sendApiError(400, "Invalid JSON object");
    return;
  }
  const char *ssid = document["wifi_ssid"] | "";
  const char *wifiPassword = document["wifi_password"] | "";
  const char *mqttHost = document["mqtt_host"] | "";
  const int mqttPort = document["mqtt_port"] | 0;
  const char *mqttUsername = document["mqtt_username"] | "";
  const char *mqttPassword = document["mqtt_password"] | "";
  if (!*ssid || strlen(ssid) > 32 || !*mqttHost || strlen(mqttHost) > 64 ||
      strlen(wifiPassword) > 64 || strlen(mqttUsername) > 64 ||
      strlen(mqttPassword) > 64 || mqttPort < 1 || mqttPort > 65535) {
    sendApiError(400, "One or more settings are invalid");
    return;
  }

  preferences.begin("gateway", false);
  preferences.putString("wifi_ssid", ssid);
  if (*wifiPassword) preferences.putString("wifi_pass", wifiPassword);
  preferences.putString("mqtt_host", mqttHost);
  preferences.putUShort("mqtt_port", uint16_t(mqttPort));
  preferences.putString("mqtt_user", mqttUsername);
  if (*mqttPassword) preferences.putString("mqtt_pass", mqttPassword);
  preferences.end();
  JsonDocument response;
  response["ok"] = true;
  response["message"] = "Settings saved; rebooting";
  sendJsonDocument(200, response);
  restartAtMs = millis() + 750;
}

void handleApiDefaults() {
  if (!webAuthenticated()) return;
  preferences.begin("gateway", false);
  preferences.clear();
  preferences.end();
  JsonDocument response;
  response["ok"] = true;
  response["message"] = "Compiled defaults restored; rebooting";
  sendJsonDocument(200, response);
  restartAtMs = millis() + 750;
}

void handleApiReboot() {
  if (!webAuthenticated()) return;
  JsonDocument response;
  response["ok"] = true;
  response["message"] = "Rebooting";
  sendJsonDocument(200, response);
  restartAtMs = millis() + 750;
}

void configureWebServer() {
  webServer.on("/", HTTP_GET, handleWebRoot);
  webServer.on("/api/status", HTTP_GET, handleApiStatus);
  webServer.on("/api/last-rx", HTTP_GET, handleApiLastRx);
  webServer.on("/api/settings", HTTP_GET, handleApiSettings);
  webServer.on("/api/settings", HTTP_POST, handleApiSaveSettings);
  webServer.on("/api/tx", HTTP_POST, handleApiTx);
  webServer.on("/api/defaults", HTTP_POST, handleApiDefaults);
  webServer.on("/api/reboot", HTTP_POST, handleApiReboot);
  webServer.onNotFound([]() {
    if (!webAuthenticated()) return;
    sendApiError(404, "Not found");
  });
  webServer.begin();
}

void webServerTask(void *) {
  for (;;) {
    webServer.handleClient();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void connectWifiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnectionReported) {
      wifiConnectionReported = true;
      if (Serial) {
        Serial.print("Wi-Fi connected, IP address: ");
        Serial.println(WiFi.localIP());
        Serial.print("Web interface: http://");
        Serial.print(WiFi.localIP());
        Serial.println('/');
      }
    }
    return;
  }
  wifiConnectionReported = false;
  const uint32_t now = millis();
  if (lastWifiAttemptMs &&
      uint32_t(now - lastWifiAttemptMs) < WIFI_RECONNECT_INTERVAL_MS)
    return;
  lastWifiAttemptMs = now;
  if (Serial) {
    Serial.print("Connecting Wi-Fi to ");
    Serial.println(runtimeConfig.wifiSsid);
  }
  WiFi.begin(runtimeConfig.wifiSsid, runtimeConfig.wifiPassword);
}

void makeClientId(char *output, size_t capacity) {
  uint64_t mac = ESP.getEfuseMac();
  snprintf(output, capacity, "%s-%06llX", MQTT_CLIENT_ID_PREFIX,
           (unsigned long long)(mac & 0xFFFFFFULL));
}

void connectMqttIfNeeded() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  const uint32_t now = millis();
  if (lastMqttAttemptMs &&
      uint32_t(now - lastMqttAttemptMs) < MQTT_RECONNECT_INTERVAL_MS)
    return;
  lastMqttAttemptMs = now;

  char clientId[64];
  makeClientId(clientId, sizeof(clientId));
  if (Serial) {
    Serial.print("Connecting MQTT as ");
    Serial.println(clientId);
  }
  const bool connected = mqtt.connect(clientId, runtimeConfig.mqttUsername,
                                      runtimeConfig.mqttPassword,
                                      MQTT_AVAILABILITY_TOPIC, 0, true,
                                      "offline");
  if (!connected) {
    if (Serial)
      Serial.printf("MQTT connection failed, state=%d\n", mqtt.state());
    return;
  }
  mqtt.publish(MQTT_AVAILABILITY_TOPIC, "online", true);
  mqtt.subscribe(MQTT_TX_TOPIC, 1);
  if (HA_DISCOVERY_ENABLED) mqtt.subscribe(HA_STATUS_TOPIC, 0);
  discoveryPending = HA_DISCOVERY_ENABLED;
  lastRssiSampleMs = 0;
  sampleWifiRssiIfNeeded();
  if (Serial) Serial.println("MQTT connected");
}

void setup() {
  Serial.begin(USB_SERIAL_BAUD);
  // USB can report connected even when the host is not consuming output.
  // Drop diagnostic output under backpressure instead of waiting in RX/TX.
  Serial.setTxTimeoutMs(0);
  delay(100);
  if (Serial) Serial.println("Inspire Gateway ESP32-C3 starting");

  loadRuntimeConfig();
  Serial1.begin(NRF_UART_BAUD, SERIAL_8N1, NRF_UART_RX_PIN, NRF_UART_TX_PIN);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  mqtt.setServer(runtimeConfig.mqttHost, runtimeConfig.mqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(MQTT_BUFFER_BYTES);
  mqtt.setKeepAlive(MQTT_KEEPALIVE_SECONDS);
  configureWebServer();
  xTaskCreate(webServerTask, "web-server", 8192, nullptr, 1, nullptr);
  connectWifiIfNeeded();
}

void loop() {
  processNrfUart();
  connectWifiIfNeeded();
  connectMqttIfNeeded();
  sampleWifiRssiIfNeeded();
  if (mqtt.connected()) {
    mqtt.loop();
    if (discoveryPending && publishHomeAssistantDiscovery())
      discoveryPending = false;
    publishRssiIfNeeded();
    flushRxQueue();
  }
  if (restartAtMs && int32_t(millis() - restartAtMs) >= 0) ESP.restart();
  delay(1);
}
