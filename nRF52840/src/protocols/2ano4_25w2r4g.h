#pragma once

#include <Arduino.h>
#include <nrf.h>
#include <string.h>

#include "protocol_interface.h"

// Experimental plugin for the remote researched under FCC ID
// 2ANO4-25W2R4G. The identifier is a convenient protocol name; the measured
// 2407 MHz carrier differs from the filing's nominal 2450 MHz value, so the
// physical remote has not yet been proven to be the exact certified model.
struct Fcc2ano425w2r4gProtocol {
  static constexpr const char *NAME = "2ANO4-25W2R4G";
  static constexpr const char *ADDRESS_TEXT = "4E22C32B40";

  static constexpr uint8_t RF_CHANNEL = 7; // measured 2406.943 MHz
  static constexpr uint32_t PACKET_INTERVAL_NS = 3406250;
  static constexpr uint32_t UART_BURST_GAP_US = 50000;
  static constexpr size_t RADIO_FRAME_BYTES = 188;
  static constexpr size_t MAX_PAYLOAD_BYTES = RADIO_FRAME_BYTES;
  static constexpr size_t RX_ACTION_BIT_OFFSET = 857;
  static constexpr size_t TX_ACTION_BIT_OFFSET = RX_ACTION_BIT_OFFSET;
  static constexpr size_t ACTION_BYTES = 8;
  static constexpr size_t ACTION_FRAME_SUFFIX_OFFSET = 107;
  static constexpr size_t ACTION_FRAME_SUFFIX_BYTES =
      RADIO_FRAME_BYTES - ACTION_FRAME_SUFFIX_OFFSET;
  static constexpr uint16_t MAX_REPEAT_COUNT = 500;

  struct __attribute__((packed)) Packet {
    uint8_t bytes[RADIO_FRAME_BYTES];
  };
  static_assert(sizeof(Packet) <= RADIO_PACKET_BUFFER_BYTES,
                "2ANO4 packet does not fit the shared radio buffer");

  static uint8_t reverseBits(uint8_t value) {
    value = uint8_t((value >> 4) | (value << 4));
    value = uint8_t(((value & 0xCC) >> 2) | ((value & 0x33) << 2));
    value = uint8_t(((value & 0xAA) >> 1) | ((value & 0x55) << 1));
    return value;
  }

  static uint8_t *radioAddress() {
    static uint8_t bytes[5] = {0x4E, 0x22, 0xC3, 0x2B, 0x40};
    return bytes;
  }

  static char *radioAddressText() {
    static char text[11] = "4E22C32B40";
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

    // The IQ consensus has a 26-symbol alternating run at symbols 77..102.
    // The final 16 symbols are used as the hardware preamble; the following
    // 40 bits are the address. The remaining measured frame is 188 bytes.
    NRF_RADIO->PCNF0 =
        (0UL << RADIO_PCNF0_LFLEN_Pos) |
        (0UL << RADIO_PCNF0_S0LEN_Pos) |
        (0UL << RADIO_PCNF0_S1LEN_Pos) |
        (RADIO_PCNF0_S1INCL_Automatic << RADIO_PCNF0_S1INCL_Pos) |
        (RADIO_PCNF0_PLEN_16bit << RADIO_PCNF0_PLEN_Pos);
    NRF_RADIO->PCNF1 =
        (RADIO_FRAME_BYTES << RADIO_PCNF1_MAXLEN_Pos) |
        (RADIO_FRAME_BYTES << RADIO_PCNF1_STATLEN_Pos) |
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

  static const uint8_t *onTemplate() {
    static constexpr uint8_t bytes[RADIO_FRAME_BYTES] = {
      0x67, 0xCB, 0x9A, 0xD4, 0xAF, 0x84, 0x06, 0x0E, 0x3B, 0x8B, 0x26, 0x91,
      0xC5, 0xE9, 0xCC, 0x09, 0x71, 0x14, 0x58, 0x4E, 0x22, 0xC3, 0x2B, 0x40,
      0x67, 0xCB, 0x9A, 0x4E, 0x22, 0xC3, 0x2B, 0x40, 0x67, 0xCB, 0x9A, 0xD4,
      0xAF, 0x84, 0x06, 0x0E, 0x3B, 0x8B, 0x26, 0x91, 0xC5, 0xE9, 0xCC, 0x09,
      0x71, 0x14, 0x58, 0x4E, 0x22, 0xC3, 0x2B, 0x40, 0x67, 0xCB, 0x9B, 0x42,
      0x89, 0xF0, 0x0E, 0x74, 0xB1, 0xB2, 0x11, 0x24, 0xE2, 0x2C, 0x32, 0xB4,
      0x06, 0x7C, 0xB9, 0xA4, 0xE2, 0x2C, 0x32, 0xB4, 0x06, 0x7C, 0xB9, 0xB3,
      0x06, 0x3F, 0x8A, 0xA5, 0x59, 0x14, 0x94, 0x80, 0x70, 0x14, 0x53, 0x59,
      0xB2, 0x85, 0x7E, 0x28, 0xB8, 0x7D, 0x00, 0xA9, 0x0B, 0xBC, 0xE6, 0x9C,
      0x08, 0x1D, 0x93, 0x28, 0x55, 0x95, 0x9C, 0xBC, 0x08, 0x1D, 0x93, 0x28,
      0x55, 0x95, 0x9C, 0xA0, 0x70, 0x14, 0x53, 0x59, 0xB2, 0x85, 0x7E, 0x24,
      0xE2, 0x2C, 0x32, 0xB4, 0x06, 0x7C, 0xB9, 0xA2, 0x89, 0xF0, 0x0E, 0x74,
      0xB1, 0xB2, 0x11, 0x3C, 0x08, 0x1D, 0x93, 0x28, 0x55, 0x95, 0x9C, 0xB3,
      0x06, 0x3F, 0x8A, 0xA5, 0x59, 0x14, 0x94, 0x80, 0x70, 0x14, 0x53, 0x59,
      0xB2, 0x85, 0x7E, 0x23, 0x2C, 0x29, 0x22, 0x9F, 0x89, 0x11, 0x8E, 0x31,
      0x3C, 0xC0, 0xC1, 0x29, 0x7B, 0x35, 0x57, 0x24,
    };
    return bytes;
  }

  static void writeBits(uint8_t *destination, size_t destinationBit,
                        const uint8_t *source, size_t bitCount) {
    for (size_t bit = 0; bit < bitCount; ++bit) {
      const uint8_t value =
          uint8_t((source[bit / 8] >> (7 - (bit % 8))) & 1U);
      const size_t outputBit = destinationBit + bit;
      const uint8_t mask = uint8_t(1U << (7 - (outputBit % 8)));
      if (value) destination[outputBit / 8] |= mask;
      else destination[outputBit / 8] &= uint8_t(~mask);
    }
  }

  static void readBits(const uint8_t *source, size_t sourceBit,
                       uint8_t *destination, size_t bitCount) {
    memset(destination, 0, (bitCount + 7) / 8);
    for (size_t bit = 0; bit < bitCount; ++bit) {
      const size_t inputBit = sourceBit + bit;
      const uint8_t value =
          uint8_t((source[inputBit / 8] >> (7 - (inputBit % 8))) & 1U);
      destination[bit / 8] |= uint8_t(value << (7 - (bit % 8)));
    }
  }

  struct KnownActionFrame {
    uint8_t action[ACTION_BYTES];
    const char *suffixHex;
  };

  static uint8_t hexNibble(char value) {
    if (value >= '0' && value <= '9') return uint8_t(value - '0');
    if (value >= 'A' && value <= 'F') return uint8_t(value - 'A' + 10);
    return uint8_t(value - 'a' + 10);
  }

  static bool expandKnownAction(uint8_t *frame, const uint8_t *action) {
    // One genuine 188-byte frame was captured directly by the nRF receiver
    // for every remote button. Bytes 0..106 are common; byte 107 onward holds
    // the action plus its repeated/encoded companion fields. Replacing only
    // the visible 8-byte action creates an invalid hybrid frame.
    static constexpr KnownActionFrame known[] = {
      {{0x38, 0x10, 0x3B, 0x26, 0x50, 0xAB, 0x2B, 0x39},
       "9C081D932855959CBC081D932855959CB13CC0C1297B355724E22C32B4067CB9A2"
       "89F00E74B1B2113C081D932855959CA32C29229F89118E2070145359B2857E28B"
       "87D00A90BBCE68289F00E74B1B21124"},
      {{0x22, 0x9D, 0x6A, 0x08, 0x96, 0xC4, 0xF0, 0xCB},
       "914EB5044B627865DC081D932855959CB13CC0C1297B355724E22C32B4067CB9A2"
       "89F00E74B1B2113C081D932855959CA91C5E9CC0971145914EB5044B627865CD4"
       "AF84060E3B8B264E22C32B4067CB9A4"},
      {{0x00, 0xE0, 0x28, 0xA6, 0xB3, 0x65, 0x0A, 0xFC},
       "8070145359B2857E3C081D932855959CB13CC0C1297B355724E22C32B4067CB9A2"
       "89F00E74B1B2113C081D932855959CB8CC4900A34417E5F8CC4900A34417E5F30"
       "63F8AA5591494832C29229F89118E24"},
      {{0x26, 0x0C, 0x7F, 0x15, 0x4A, 0xB2, 0x29, 0x29},
       "93063F8AA55914949C081D932855959CB13CC0C1297B355724E22C32B4067CB9A2"
       "89F00E74B1B2113C081D932855959CA4E22C32B4067CB9B13CC0C1297B355724C"
       "0D786D0E57100BC081D932855959CA4"},
      {{0x12, 0x38, 0xBD, 0x39, 0x81, 0x2E, 0x22, 0x8B},
       "891C5E9CC09711459C081D932855959CB13CC0C1297B355724E22C32B4067CB9A2"
       "89F00E74B1B2113C081D932855959CB13CC0C1297B355733063F8AA559149482C6"
       "2AA61B0CC0B7B8CC4900A34417E5E4"},
      {{0x13, 0xF0, 0x18, 0xB8, 0xA0, 0x85, 0x3B, 0x96},
       "89F80C5C50429DCB3C081D932855959CB13CC0C1297B355724E22C32B4067CB9A2"
       "89F00E74B1B2113C081D932855959CA9F80C5C50429DCB33063F8AA559149488B8"
       "7D00A90BBCE689F80C5C50429DCB24"},
      {{0x05, 0x13, 0xE0, 0x1C, 0xE9, 0x63, 0x64, 0x22},
       "8289F00E74B1B211280307B4E223A40BB13CC0C1297B355724E22C32B4067CB9A2"
       "89F00E74B1B2113C081D932855959CA070145359B2857E24E22C32B4067CB9A2C6"
       "2AA61B0CC0B7A80307B4E223A40BA4"},
    };

    for (const KnownActionFrame &candidate : known) {
      if (memcmp(action, candidate.action, ACTION_BYTES) != 0) continue;
      if (frame) {
        for (size_t byte = 0; byte < ACTION_FRAME_SUFFIX_BYTES; ++byte) {
          frame[ACTION_FRAME_SUFFIX_OFFSET + byte] = uint8_t(
              (hexNibble(candidate.suffixHex[byte * 2]) << 4) |
              hexNibble(candidate.suffixHex[byte * 2 + 1]));
        }
      }
      return true;
    }
    return false;
  }

  static bool acceptsPayload(const uint8_t *payload, size_t payloadLength) {
    return payloadLength == RADIO_FRAME_BYTES ||
           (payloadLength == ACTION_BYTES &&
            expandKnownAction(nullptr, payload));
  }

  static void prepareTransmit(RadioPacketBuffer &buffer, const uint8_t *payload,
                              size_t payloadLength) {
    Packet &value = packet(buffer);
    if (payloadLength == RADIO_FRAME_BYTES) {
      memcpy(value.bytes, payload, RADIO_FRAME_BYTES);
    } else {
      memcpy(value.bytes, onTemplate(), RADIO_FRAME_BYTES);
      if (payloadLength == ACTION_BYTES &&
          !expandKnownAction(value.bytes, payload))
        writeBits(value.bytes, TX_ACTION_BIT_OFFSET, payload, ACTION_BYTES * 8);
    }
  }

  static void transmitHardwarePacket(void *packetPointer) {
    NRF_RADIO->PACKETPTR = reinterpret_cast<uint32_t>(packetPointer);
    NRF_RADIO->EVENTS_READY = 0;
    NRF_RADIO->EVENTS_END = 0;
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk |
                        RADIO_SHORTS_END_DISABLE_Msk;
    NRF_RADIO->TASKS_TXEN = 1;
    while (!NRF_RADIO->EVENTS_END) {}
    while (!NRF_RADIO->EVENTS_DISABLED) {}
  }

  static void transmitPrepared(RadioPacketBuffer &buffer) {
    // The real remote sends an 87-bit wake/AGC leader before its normal
    // 16-bit preamble and 40-bit address. A previous experiment embedded the
    // real preamble inside one larger dummy packet; a packet receiver cannot
    // re-lock while it is already consuming that packet. Send the leader as a
    // separate short transmission, then immediately send a correctly framed
    // 188-byte packet using the measured hardware address.
    alignas(4) static uint8_t wakePacket[11] = {
        0x01, 0xC0, 0x0F, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFD, 0x55};

    NRF_RADIO->BASE0 = 0;
    NRF_RADIO->PREFIX0 = 0;
    NRF_RADIO->PCNF1 =
        (sizeof(wakePacket) << RADIO_PCNF1_MAXLEN_Pos) |
        (sizeof(wakePacket) << RADIO_PCNF1_STATLEN_Pos) |
        (4UL << RADIO_PCNF1_BALEN_Pos) |
        (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos) |
        (RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos);
    transmitHardwarePacket(wakePacket);

    configureRadio();
    transmitHardwarePacket(&buffer);
  }

  static ReceivedPacketView receivedPacket(const RadioPacketBuffer &buffer) {
    const Packet &value = packet(buffer);
    // Reject internal false locks: the long payload contains address-like
    // patterns, but a true frame start has this fixed prefix before the action.
    if (memcmp(value.bytes, onTemplate(), RX_ACTION_BIT_OFFSET / 8) != 0)
      return {0, 0, value.bytes};
    static uint8_t action[ACTION_BYTES];
    readBits(value.bytes, RX_ACTION_BIT_OFFSET, action, ACTION_BYTES * 8);
    return {ACTION_BYTES, 0, action};
  }

  static const RadioProtocol &plugin() {
    static const RadioProtocol definition = {
        NAME, radioAddressText(), 5, MAX_PAYLOAD_BYTES, MAX_REPEAT_COUNT,
        PACKET_INTERVAL_NS, UART_BURST_GAP_US, setAddress, configureRadio,
        acceptsPayload, prepareTransmit, transmitPrepared, receivedPacket};
    return definition;
  }
};
