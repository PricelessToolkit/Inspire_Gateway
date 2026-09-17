#pragma once

#include <Arduino.h>

// The 2ANO4 capture contains a long static frame. Keep one shared DMA-safe
// buffer large enough for that protocol while smaller plugins use only the
// leading bytes.
constexpr size_t RADIO_PACKET_BUFFER_BYTES = 208;
constexpr size_t RADIO_MAX_PAYLOAD_BYTES = 188;
constexpr size_t RADIO_MAX_ADDRESS_BYTES = 5;

struct alignas(4) RadioPacketBuffer {
  uint8_t bytes[RADIO_PACKET_BUFFER_BYTES];
};

struct ReceivedPacketView {
  uint8_t length;
  uint8_t s1;
  const uint8_t *payload;
};

struct RadioProtocol {
  const char *name;
  const char *addressText;
  size_t addressBytes;
  size_t maxPayloadBytes;
  uint16_t maxRepeatCount;
  uint32_t packetIntervalNs;
  uint32_t uartBurstGapUs;
  bool (*setAddress)(const uint8_t *, size_t);
  void (*configureRadio)();
  bool (*acceptsPayload)(const uint8_t *, size_t);
  void (*prepareTransmit)(RadioPacketBuffer &, const uint8_t *, size_t);
  // Optional protocol-specific physical transmission. A null pointer uses the
  // modem's normal single-packet transmitter.
  void (*transmitPrepared)(RadioPacketBuffer &);
  ReceivedPacketView (*receivedPacket)(const RadioPacketBuffer &);
};
