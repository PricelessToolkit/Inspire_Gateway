# Inspire Gateway nRF52840 RF modem

The nRF52840 is the RF modem. The ESP32-C3 owns light profiles, repeat
quantities, Wi-Fi, MQTT, UI, and persistent storage. Most plugins pass complete
payloads unchanged. A plugin may also expand a compact action fingerprint when
the physical protocol requires a much longer encoded frame, as
`2ANO4-25W2R4G` does.

Files:

- `nRF52840.ino` — reusable modem engine and UART protocol
- `config.h` — board UART, USB, and LED settings only
- `src/protocol_registry.h` — registers plugins and selects the boot default
- `src/protocols/gdansk_inspire.h` — Gdansk Inspire RF protocol plugin
- `src/protocols/gdansk_inspire.md` — concise RF protocol specification
- `src/protocols/2ano4_25w2r4g.h` — proven long-frame protocol plugin
- `src/protocols/2ano4_25w2r4g.md` — confirmed settings, actions, and tests
- `src/protocols/protocol_interface.h` — shared plugin API, not an RF protocol

The end-to-end capture, Python analysis, hardware-validation, and plugin
development workflow is documented in
[`../AI_AGENT_RF_REVERSE_ENGINEERING_GUIDE.md`](../AI_AGENT_RF_REVERSE_ENGINEERING_GUIDE.md).

## Boot-default `gdansk_inspire` settings

These values belong to the selected `src/protocols/gdansk_inspire.h` plugin:

```text
RF frequency/channel: 2445 MHz / channel 45
RF mode:              Nordic proprietary 1 Mbit/s
RF address:           55E96499B5
TX power:             +8 dBm
S1:                   5 (`101`: PID 2, NO_ACK 1)
Packet interval:      3906250 ns (3906.25 us)
Whitening:            disabled
Hardware radio CRC:   disabled
Maximum payload:      12 bytes
Maximum repeats:      501
UART:                 921600 baud, 8N1, 3.3 V
```

Both tested same-model remotes use the same on-air address `55E96499B5`; their
different four-byte payload prefixes identify the individual remote/light
profiles. This is the plugin's boot default, but every TX command must state
the five-byte address explicitly. The nRF begins listening immediately at boot.

## Protocol plugins

The modem keeps a pointer to the active runtime plugin. A plugin provides its
packet structure, payload/repeat limits, RF register configuration, address,
S1, packet interval, burst gap, and small TX/RX helpers. It does not contain
Wi-Fi, MQTT, light profiles, learned commands, or application payload decoding.

To add a protocol, create another header under `src/protocols/` with the same
interface as `gdansk_inspire.h`, then register it in `src/protocol_registry.h`.
The protocol named by the latest TX becomes the active listening protocol after
that transmission. This does not scan several channels simultaneously.

Every distinct RF protocol gets its own file; protocol implementations are
never combined by brand. Plugin IDs and filenames use lowercase
`brand_number`, for example:

```text
src/protocols/gdansk_inspire.h -> protocol=gdansk_inspire
src/protocols/inspire_2.h  -> protocol=inspire_2
src/protocols/dogy_1.h     -> protocol=dogy_1
```

## Wiring

```text
XIAO ESP32-C3 D9 / GPIO9 TX   -> nRF52840 P0.06 RX
XIAO ESP32-C3 D10 / GPIO10 RX <- nRF52840 P0.08 TX
XIAO ESP32-C3 GND             <-> nRF52840 GND
```

On the Nice!Nano-compatible Pro Micro header, P0.06 is the top-left signal pin
(D3 position) and P0.08 is the next signal pin below it (D2 position).

Because this generic board uses the Adafruit Feather nRF52840 build variant,
`config.h` selects Arduino D11 for physical P0.06 and D12 for physical P0.08.

USB Serial mirrors received RF events for diagnostics and accepts the same TX
command as hardware UART.

## UART protocol

ASCII text, one LF-terminated command or event per line. CRLF is also accepted.

The production ESP-to-nRF command is:

```text
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A62EB6F8D8DA80FA1827D58
```

Fields:

- `protocol`: registered nRF radio plugin name, currently `gdansk_inspire` or
  `2ANO4-25W2R4G`.
- `address`: exactly five RF-address bytes / ten hexadecimal characters. It is
  required on every TX and becomes the active RX address afterward.
- `repeat`: protocol-bounded number of transmissions, chosen by the ESP.
- `payload`: protocol-specific hexadecimal bytes. `gdansk_inspire` expects a
  complete 12-byte payload. `2ANO4-25W2R4G` accepts a known eight-byte action
  fingerprint or a complete 188-byte radio body.

The nRF activates the named plugin, prepares and sends its RF frame, and then
listens using the same plugin. That remains the active listening protocol until
another valid TX names a protocol. At cold boot, the registry default is
`gdansk_inspire` and reception begins immediately.

The nRF sends **no command responses**: no READY, OK, TX_START, TX_DONE, status,
or errors. Invalid commands are silently ignored. The ESP must wait long enough
before sending another command:

```text
wait_ms >= repeat * protocol_interval_ms + 5
```

For `gdansk_inspire`, 50 repeats need at least 201 ms; 210 ms is convenient.
For `2ANO4-25W2R4G`, 150 repeats need at least 516 ms; 525 ms is convenient.

### USB-only capture diagnostics

These newline-terminated commands are intended for reverse engineering through
native USB Serial. They do not produce OK/error responses and do not change the
normal hardware-UART RX format:

```text
DUMP_RX
DUMP_RX_ALL
DUMP_RX_STOP
```

- `DUMP_RX` prints one complete raw body from the next valid received packet.
- `DUMP_RX_ALL` prints one complete raw body at the start of every physical
  receive burst.
- `DUMP_RX_STOP` disables both dump modes.

Example diagnostic output:

```text
RAW_RX protocol=2ANO4-25W2R4G len=188 action=229D6A0896C4F0CB payload=<376 hex characters>
```

USB `RAW_RX` output can be long. Read at 921600 baud and use line-oriented
software. The implementation deliberately writes the long hexadecimal body in
pieces because `Serial.printf` on some TinyUSB builds corrupts long lines.

## Received radio events

Only actual received RF packets are written by the nRF:

```text
RX protocol=gdansk_inspire time_us=150695312 rssi=-52 address=55E96499B5 len=12 s1=5 payload=9A62EB6F8D8DA80FA1827D58
```

USB Serial prints every address-matched RF packet for diagnostics. Hardware UART
prints only the first packet in a receive burst, so the ESP normally receives
one line per physical press. A gap of at least 50 ms begins a new burst; native
packets inside one press are only 3.90625 ms apart. The ESP validates the first
packet's application CRC and extracts its profile and action.

The nRF is in receive mode after boot and automatically returns to receive mode
whenever it finishes transmitting.

## Proven long-frame commands

```text
TX protocol=2ANO4-25W2R4G address=4E22C32B40 repeat=150 payload=38103B2650AB2B39
TX protocol=2ANO4-25W2R4G address=4E22C32B40 repeat=150 payload=229D6A0896C4F0CB
TX protocol=2ANO4-25W2R4G address=4E22C32B40 repeat=150 payload=00E028A6B3650AFC
TX protocol=2ANO4-25W2R4G address=4E22C32B40 repeat=150 payload=260C7F154AB22929
TX protocol=2ANO4-25W2R4G address=4E22C32B40 repeat=150 payload=1238BD39812E228B
TX protocol=2ANO4-25W2R4G address=4E22C32B40 repeat=150 payload=13F018B8A0853B96
TX protocol=2ANO4-25W2R4G address=4E22C32B40 repeat=150 payload=0513E01CE9636422
```

The plugin expands these compact fingerprints into genuine captured 188-byte
frames and emits the required wake leader before each normal packet. See
[`src/protocols/2ano4_25w2r4g.md`](src/protocols/2ano4_25w2r4g.md).

## Proven first-light payloads

| Command | Raw payload |
|---|---|
| ON | `9A62EB6F8D8DA80FA1827D58` |
| OFF | `9A62EB6F8D95B00FA181D528` |

ON:

```text
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A62EB6F8D8DA80FA1827D58
```

Wait at least 210 ms, then OFF:

```text
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A62EB6F8D95B00FA181D528
```

Five repeats were unreliable, 20 worked in the tested location, and 501 worked.
The ESP chooses the reliability/speed tradeoff; 50 is a practical starting
value and is not a modem default.

## Build

Tested with Adafruit nRF52 Boards core 1.7.0:

```sh
arduino-cli compile --fqbn adafruit:nrf52:feather52840 Inspire_Gateway/nRF52840
arduino-cli upload -p /dev/ttyACM0 \
  --fqbn adafruit:nrf52:feather52840 Inspire_Gateway/nRF52840
```
