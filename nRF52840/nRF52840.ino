/*
 * Inspire Gateway nRF52840 RF modem with protocol plugins.
 *
 * The ESP32 owns all light profiles and payload interpretation. This firmware
 * only receives/transmits raw Nordic 1-Mbit frames using the active protocol
 * plugin's fixed RF settings.
 * See README.md for the newline-delimited UART protocol.
 */

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <nrf.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "src/protocol_registry.h"

using namespace GatewayConfig;

struct InputState {
  char line[MAX_LINE_LENGTH + 1];
  size_t length = 0;
  bool overflow = false;
};

RadioPacketBuffer radioPacket;
const RadioProtocol *activeProtocol = nullptr;
InputState uartInput;
InputState usbInput;

bool radioConfigured = true;
bool listening = false;
bool transmitting = false;
uint32_t receivedPackets = 0;
uint32_t transmittedPackets = 0;
uint32_t ledOffAt = 0;
uint32_t lastReceivedPacketUs = 0;
bool dumpNextReceivedPacket = false;
bool dumpEveryReceivedBurst = false;

void emitLine(bool sendToUart, const char *format, ...) {
  char output[512];
  va_list args;
  va_start(args, format);
  int count = vsnprintf(output, sizeof(output), format, args);
  va_end(args);
  if (count < 0) return;
  size_t length = min<size_t>(size_t(count), sizeof(output) - 1);
  if (sendToUart) {
    Serial1.write(reinterpret_cast<const uint8_t *>(output), length);
    Serial1.write('\n');
  }
  if (USB_MIRROR_ENABLED && Serial) {
    Serial.write(reinterpret_cast<const uint8_t *>(output), length);
    Serial.write('\n');
  }
}

void receiveLed(bool on) {
  if (on) NRF_P0->OUTSET = LED_PIN_MASK;
  else NRF_P0->OUTCLR = LED_PIN_MASK;
}

void stopRadio() {
  if ((NRF_RADIO->STATE & RADIO_STATE_STATE_Msk) != RADIO_STATE_STATE_Disabled) {
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE = 1;
    while (!NRF_RADIO->EVENTS_DISABLED) {}
  }
  listening = false;
}

void startHFClock() {
  if ((NRF_CLOCK->HFCLKSTAT & CLOCK_HFCLKSTAT_STATE_Msk) == 0) {
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;
    while (!NRF_CLOCK->EVENTS_HFCLKSTARTED) {}
  }
}

void configureRadioRegisters() {
  stopRadio();
  startHFClock();

  NRF_RADIO->POWER = 1;
  activeProtocol->configureRadio();
}

void armReceiver() {
  if (!radioConfigured || transmitting) return;
  memset(&radioPacket, 0, sizeof(radioPacket));
  NRF_RADIO->PACKETPTR = reinterpret_cast<uint32_t>(&radioPacket);
  NRF_RADIO->EVENTS_READY = 0;
  NRF_RADIO->EVENTS_ADDRESS = 0;
  NRF_RADIO->EVENTS_END = 0;
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->EVENTS_RSSIEND = 0;
  NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk |
                      RADIO_SHORTS_ADDRESS_RSSISTART_Msk |
                      RADIO_SHORTS_DISABLED_RSSISTOP_Msk |
                      RADIO_SHORTS_END_DISABLE_Msk;
  NRF_RADIO->TASKS_RXEN = 1;
  listening = true;
}

void processReceivedPacket() {
  if (!listening || !NRF_RADIO->EVENTS_END) return;
  while (!NRF_RADIO->EVENTS_DISABLED) {}
  listening = false;

  int rssi = -int(NRF_RADIO->RSSISAMPLE);
  const ReceivedPacketView received = activeProtocol->receivedPacket(radioPacket);
  uint8_t length = received.length;
  uint8_t s1 = received.s1;
  if (!length) {
    armReceiver();
    return;
  }
  size_t shown = min<size_t>(length, activeProtocol->maxPayloadBytes);
  char payloadHex[RADIO_MAX_PAYLOAD_BYTES * 2 + 1];
  for (size_t i = 0; i < shown; ++i)
    snprintf(payloadHex + i * 2, 3, "%02X", received.payload[i]);
  payloadHex[shown * 2] = 0;

  ++receivedPackets;
  receiveLed(true);
  ledOffAt = millis() + RX_LED_TIME_MS;
  const uint32_t receivedAtUs = micros();
  const bool firstInBurst = !lastReceivedPacketUs ||
      uint32_t(receivedAtUs - lastReceivedPacketUs) >=
          activeProtocol->uartBurstGapUs;
  lastReceivedPacketUs = receivedAtUs;

  if ((dumpNextReceivedPacket ||
       (dumpEveryReceivedBurst && firstInBurst)) && Serial) {
    const size_t rawLength = activeProtocol->maxPayloadBytes;
    char rawHex[RADIO_MAX_PAYLOAD_BYTES * 2 + 1];
    for (size_t i = 0; i < rawLength; ++i)
      snprintf(rawHex + i * 2, 3, "%02X", radioPacket.bytes[i]);
    rawHex[rawLength * 2] = 0;
    // Avoid Serial.printf here: some TinyUSB builds use a small temporary
    // formatting buffer and corrupt lines this long.
    Serial.print("RAW_RX protocol=");
    Serial.print(activeProtocol->name);
    Serial.print(" len=");
    Serial.print(unsigned(rawLength));
    Serial.print(" action=");
    Serial.print(payloadHex);
    Serial.print(" payload=");
    Serial.println(rawHex);
    dumpNextReceivedPacket = false;
  }
  emitLine(firstInBurst,
           "RX protocol=%s time_us=%lu rssi=%d address=%s len=%u s1=%u payload=%s",
           activeProtocol->name, (unsigned long)receivedAtUs, rssi,
           activeProtocol->addressText, length, s1, payloadHex);
  armReceiver();
}

bool parseUnsigned(const char *text, uint32_t &value) {
  if (!text || !*text || *text == '-') return false;
  char *end = nullptr;
  unsigned long parsed = strtoul(text, &end, 10);
  if (!end || *end) return false;
  value = uint32_t(parsed);
  return true;
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseHex(const char *text, uint8_t *output, size_t outputCapacity,
              size_t &outputLength, size_t requiredLength = 0) {
  if (!text) return false;
  size_t chars = strlen(text);
  if (!chars || (chars & 1)) return false;
  size_t bytes = chars / 2;
  if (bytes > outputCapacity || (requiredLength && bytes != requiredLength))
    return false;
  for (size_t i = 0; i < bytes; ++i) {
    int high = hexNibble(text[i * 2]);
    int low = hexNibble(text[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    output[i] = uint8_t((high << 4) | low);
  }
  outputLength = bytes;
  return true;
}

void waitUntilMicros(uint32_t target) {
  while (int32_t(target - micros()) > 1000) delay(1);
  while (int32_t(target - micros()) > 0) {}
}

void transmitOnePacket() {
  NRF_RADIO->PACKETPTR = reinterpret_cast<uint32_t>(&radioPacket);
  NRF_RADIO->EVENTS_READY = 0;
  NRF_RADIO->EVENTS_END = 0;
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk |
                      RADIO_SHORTS_END_DISABLE_Msk;
  NRF_RADIO->TASKS_TXEN = 1;
  while (!NRF_RADIO->EVENTS_END) {}
  while (!NRF_RADIO->EVENTS_DISABLED) {}
}

void handleTxCommand(char *save) {
  uint32_t repeat = 0;
  bool haveRepeat = false;
  bool havePayload = false;
  bool haveAddress = false;
  const RadioProtocol *requestedProtocol = nullptr;
  bool haveProtocol = false;
  bool badKey = false;
  uint8_t payload[RADIO_MAX_PAYLOAD_BYTES];
  size_t payloadLength = 0;
  uint8_t address[RADIO_MAX_ADDRESS_BYTES];
  size_t addressLength = 0;

  for (char *token = strtok_r(nullptr, " ", &save); token;
       token = strtok_r(nullptr, " ", &save)) {
    char *equals = strchr(token, '=');
    if (!equals) { badKey = true; continue; }
    *equals = 0;
    const char *value = equals + 1;
    if (!strcasecmp(token, "repeat"))
      haveRepeat = parseUnsigned(value, repeat);
    else if (!strcasecmp(token, "protocol")) {
      haveProtocol = true;
      requestedProtocol = findProtocol(value);
    }
    else if (!strcasecmp(token, "payload"))
      havePayload = parseHex(value, payload, sizeof(payload), payloadLength);
    else if (!strcasecmp(token, "address"))
      haveAddress = parseHex(value, address, sizeof(address), addressLength);
    else badKey = true;
  }

  if (badKey) return;
  if (!haveProtocol || !requestedProtocol || !haveAddress || !haveRepeat ||
      !havePayload) return;
  if (addressLength != requestedProtocol->addressBytes) return;
  if (payloadLength > requestedProtocol->maxPayloadBytes) return;
  if (requestedProtocol->acceptsPayload &&
      !requestedProtocol->acceptsPayload(payload, payloadLength)) return;
  if (repeat < 1 || repeat > requestedProtocol->maxRepeatCount) {
    return;
  }
  if (!requestedProtocol->setAddress ||
      !requestedProtocol->setAddress(address, addressLength)) return;

  activeProtocol = requestedProtocol;
  transmitting = true;
  ledOffAt = 0;
  receiveLed(true);
  configureRadioRegisters();
  activeProtocol->prepareTransmit(radioPacket, payload, payloadLength);
  uint32_t firstStart = micros() + 3000;
  for (uint32_t packet = 0; packet < repeat; ++packet) {
    uint32_t offsetUs = uint32_t(
        (uint64_t(packet) * activeProtocol->packetIntervalNs) / 1000ULL);
    waitUntilMicros(firstStart + offsetUs);
    if (activeProtocol->transmitPrepared)
      activeProtocol->transmitPrepared(radioPacket);
    else
      transmitOnePacket();
    ++transmittedPackets;
  }

  transmitting = false;
  receiveLed(false);
  // A protocol may use a TX-only packet layout. Restore its normal receiver
  // configuration before returning to listen mode.
  configureRadioRegisters();
  armReceiver();
}

void handleCommand(char *line) {
  char *save = nullptr;
  char *command = strtok_r(line, " ", &save);
  if (!command || !*command) return;
  if (!strcasecmp(command, "TX")) {
    handleTxCommand(save);
  } else if (!strcasecmp(command, "DUMP_RX")) {
    dumpNextReceivedPacket = true;
  } else if (!strcasecmp(command, "DUMP_RX_ALL")) {
    dumpEveryReceivedBurst = true;
  } else if (!strcasecmp(command, "DUMP_RX_STOP")) {
    dumpEveryReceivedBurst = false;
    dumpNextReceivedPacket = false;
  }
}

void processInput(Stream &stream, InputState &state) {
  while (stream.available()) {
    char c = char(stream.read());
    if (c == '\r') continue;
    if (c == '\n') {
      if (!state.overflow) {
        state.line[state.length] = 0;
        handleCommand(state.line);
      }
      state.length = 0;
      state.overflow = false;
    } else if (!state.overflow) {
      if (state.length < MAX_LINE_LENGTH) state.line[state.length++] = c;
      else state.overflow = true;
    }
  }
}

void setup() {
  NRF_P0->DIRSET = LED_PIN_MASK;
  receiveLed(false);

  Serial.begin(USB_BAUD);
  Serial1.setPins(UART_RX_ARDUINO_PIN, UART_TX_ARDUINO_PIN);
  Serial1.begin(UART_BAUD);
  delay(50);
  activeProtocol = defaultProtocol();
  configureRadioRegisters();
  armReceiver();
}

void loop() {
  processReceivedPacket();
  processInput(Serial1, uartInput);
  if (USB_MIRROR_ENABLED && Serial) processInput(Serial, usbInput);

  if (ledOffAt && int32_t(millis() - ledOffAt) >= 0) {
    receiveLed(false);
    ledOffAt = 0;
  }
}
