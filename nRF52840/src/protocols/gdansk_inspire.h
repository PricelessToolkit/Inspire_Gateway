#pragma once

#include <Arduino.h>
#include <nrf.h>
#include <string.h>

#include "protocol_interface.h"

// Runtime nRF radio plugin for the reverse-engineered Inspire ceiling
// lights. It owns every protocol/RF-specific value; the main modem owns UART,
// USB, LEDs, command parsing, and the RX/TX state machine.
struct GdanskInspireProtocol {
  static constexpr const char *NAME = "gdansk_inspire";
  static constexpr const char *ADDRESS_TEXT = "55E96499B5";

  static constexpr uint8_t RF_CHANNEL = 45; // 2445 MHz
  static constexpr int8_t RF_TX_POWER_DBM = 8;
  static constexpr uint8_t TX_S1 = 5; // PID=2, NO_ACK=1
  static constexpr uint32_t PACKET_INTERVAL_NS = 3906250;
  static constexpr uint32_t UART_BURST_GAP_US = 50000;
  static constexpr size_t MAX_PAYLOAD_BYTES = 12;
  static constexpr uint16_t MAX_REPEAT_COUNT = 501;

  struct __attribute__((packed)) Packet {
    uint8_t length;
    uint8_t s1;
    uint8_t payload[MAX_PAYLOAD_BYTES];
  };
  static_assert(sizeof(Packet) <= RADIO_PACKET_BUFFER_BYTES,
                "Inspire packet does not fit the shared radio buffer");

  static uint8_t reverseBits(uint8_t value) {
    value = uint8_t((value >> 4) | (value << 4));
    value = uint8_t(((value & 0xCC) >> 2) | ((value & 0x33) << 2));
    value = uint8_t(((value & 0xAA) >> 1) | ((value & 0x55) << 1));
    return value;
  }

  static uint8_t *radioAddress() {
    static uint8_t bytes[5] = {0x55, 0xE9, 0x64, 0x99, 0xB5};
    return bytes;
  }

  static char *radioAddressText() {
    static char text[11] = "55E96499B5";
    return text;
  }

  static bool setAddress(const uint8_t *address, size_t length) {
    if (!address || length != 5) return false;
    memcpy(radioAddress(), address, 5);
    for (size_t index = 0; index < 5; ++index)
      snprintf(radioAddressText() + index * 2, 3, "%02X", address[index]);
    return true;
  }

  static void configureRadio() {
    const uint8_t *address = radioAddress();

    NRF_RADIO->MODE = RADIO_MODE_MODE_Nrf_1Mbit;
    NRF_RADIO->FREQUENCY = RF_CHANNEL;
    NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_Pos8dBm;

    NRF_RADIO->PCNF0 =
        (6UL << RADIO_PCNF0_LFLEN_Pos) |
        (0UL << RADIO_PCNF0_S0LEN_Pos) |
        (3UL << RADIO_PCNF0_S1LEN_Pos) |
        (RADIO_PCNF0_S1INCL_Include << RADIO_PCNF0_S1INCL_Pos) |
        (RADIO_PCNF0_PLEN_8bit << RADIO_PCNF0_PLEN_Pos);
    NRF_RADIO->PCNF1 =
        (MAX_PAYLOAD_BYTES << RADIO_PCNF1_MAXLEN_Pos) |
        (0UL << RADIO_PCNF1_STATLEN_Pos) |
        (4UL << RADIO_PCNF1_BALEN_Pos) |
        (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos) |
        (RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos);

    NRF_RADIO->BASE0 = uint32_t(reverseBits(address[0])) |
                       (uint32_t(reverseBits(address[1])) << 8) |
                       (uint32_t(reverseBits(address[2])) << 16) |
                       (uint32_t(reverseBits(address[3])) << 24);
    NRF_RADIO->PREFIX0 = reverseBits(address[4]);
    NRF_RADIO->TXADDRESS = 0;
    NRF_RADIO->RXADDRESSES = 1;

    NRF_RADIO->CRCCNF = RADIO_CRCCNF_LEN_Disabled;
    NRF_RADIO->CRCINIT = 0;
    NRF_RADIO->CRCPOLY = 0;
    NRF_RADIO->DATAWHITEIV = 0;
  }

  static Packet &packet(RadioPacketBuffer &buffer) {
    return *reinterpret_cast<Packet *>(buffer.bytes);
  }

  static const Packet &packet(const RadioPacketBuffer &buffer) {
    return *reinterpret_cast<const Packet *>(buffer.bytes);
  }

  static void prepareTransmit(RadioPacketBuffer &buffer, const uint8_t *payload,
                              size_t payloadLength) {
    Packet &packet = GdanskInspireProtocol::packet(buffer);
    packet.length = uint8_t(payloadLength);
    packet.s1 = TX_S1;
    memset(packet.payload, 0, sizeof(packet.payload));
    memcpy(packet.payload, payload, payloadLength);
  }

  static bool acceptsPayload(const uint8_t *, size_t payloadLength) {
    return payloadLength == MAX_PAYLOAD_BYTES;
  }

  static ReceivedPacketView receivedPacket(const RadioPacketBuffer &buffer) {
    const Packet &value = packet(buffer);
    return {uint8_t(value.length & 0x3F), uint8_t(value.s1 & 0x07),
            value.payload};
  }

  static const RadioProtocol &plugin() {
    static const RadioProtocol definition = {
        NAME, radioAddressText(), 5, MAX_PAYLOAD_BYTES, MAX_REPEAT_COUNT,
        PACKET_INTERVAL_NS, UART_BURST_GAP_US, setAddress, configureRadio,
        acceptsPayload, prepareTransmit, nullptr, receivedPacket};
    return definition;
  }
};
