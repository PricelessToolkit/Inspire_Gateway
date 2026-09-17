# `gdansk_inspire` RF protocol

`gdansk_inspire` is the first reverse-engineered 2.4 GHz protocol found in the
tested Inspire ceiling-light remotes. Two same-model remotes use this protocol.
They share the RF address and action codes but have different payload prefixes.

## RF and frame

| Setting | Value |
|---|---|
| Frequency | 2445 MHz, Nordic channel 45 |
| Modulation | Nordic proprietary 1 Mbit/s GFSK |
| Measured deviation | approximately +/-190 kHz |
| Address | `55E96499B5` |
| Preamble | `0x55` |
| Payload length | 12 bytes |
| PCF | length 12, PID 2, NO_ACK 1; S1=`5` |
| Whitening | disabled |
| nRF hardware CRC | disabled |
| TX power | nRF52840 maximum, +8 dBm |
| Packet interval | 3906.25 us |
| Frequency hopping | none observed |

The on-air frame is 153 bits: 8-bit preamble, 40-bit address, 9-bit packet
control field, and 96-bit payload. It lasts 153 us at 1 Mbit/s.

## Twelve-byte payload

Bit offsets are MSB-first from payload byte zero.

| Bits | Meaning |
|---:|---|
| 0..31 | Remote/light profile prefix |
| 32..44 | 13-bit action code |
| 45..52 | 8-bit press identifier |
| 53..55 | Reserved; observed zero |
| 56..68 | 13-bit remaining-packet countdown |
| 69..79 | 11-bit per-press check/state value |
| 80..95 | Big-endian protocol CRC-16 |

Known actions:

| Action | Meaning |
|---:|---|
| `0x11B1` | ON |
| `0x11B2` | OFF |
| `0x11B3` | Brightness up |
| `0x11B4` | Brightness down |
| `0x11B5` | Warmer |
| `0x11B6` | Colder |
| `0x11BD` | Default color and brightness |

Known profile prefixes are `9A62EB6F` for the first remote and `9A65E36F`
for the second remote. The press identifier is not an enforced rolling code:
old complete packets still work after newer physical button presses.

## CRC

The last two payload bytes contain a software CRC-16:

```text
polynomial: 0x1021
initial:    0xFFFF
xor-out:    0xB294
bit order:  MSB first
```

CRC input, in order, is the five address bytes `55E96499B5`, the nine PCF bits
`001100101`, and the first ten payload bytes. The nRF hardware CRC is disabled.
The gateway does not generate this CRC; callers send a complete 12-byte payload
with its valid CRC already appended.

## Repetition behavior

A physical remote normally sends 501 packets, with countdown values 500 through
zero, over about 1.953 seconds. Testing proved that the light also accepts many
copies of one unchanged CRC-valid packet:

- 5 copies were unreliable.
- 20 copies worked in the tested location.
- 50 copies are the recommended practical default.
- 501 identical copies also worked.

The nRF therefore repeats the supplied complete payload unchanged. It does not
modify the countdown, press identifier, check value, or CRC.

## Proven first-profile payloads

These complete packets use countdown 500 and include the CRC:

| Button | Complete payload |
|---|---|
| ON | `9A62EB6F8D8DA80FA1827D58` |
| OFF | `9A62EB6F8D95B00FA181D528` |
| Brightness up | `9A62EB6F8D9CB80FA087AB60` |
| Brightness down | `9A62EB6F8DA4D00FA0D053A0` |
| Warmer | `9A62EB6F8DACA00FA0AF9C80` |
| Colder | `9A62EB6F8DB4880FA09E0E08` |
| Default | `9A62EB6F8DECE80FA0A09758` |

## nRF plugin behavior

The implementation is [`gdansk_inspire.h`](gdansk_inspire.h). It configures the nRF52840
RADIO registers, packet layout, address, timing, repeat limits, and receive-burst
gap. It does not decode button fields or generate payloads.

Transmit with:

```text
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A62EB6F8D8DA80FA1827D58
```

Selecting `gdansk_inspire` for TX also leaves it active for reception afterwards.
It is the boot-default protocol. USB receives every matching RF packet, while
hardware UART receives only the first packet after at least 50 ms of RF silence.

The TX command always states the five-byte RF address. Known remotes use
`55E96499B5`; their four-byte payload prefixes distinguish individual profiles.
Because the application CRC includes the RF address, changing `address=` also
requires a payload whose CRC was generated for that address.
