# Inspire Gateway ESP32-C3 controller

The ESP32-C3 owns credentials, MQTT, light commands, RF payload validation, and
decoding. The nRF52840 remains a raw RF modem with generic burst suppression.

## Configure

Edit `config.h` for installation defaults:

- Wi-Fi SSID and password
- MQTT broker host/IP, port, username, and password
- MQTT client-ID prefix and topics
- Home Assistant device identity and discovery topics
- UART pins/timing if the hardware changes
- Web sign-in

The defaults use:

```text
TX: inspire-gateway/tx
RX: inspire-gateway/rx
RSSI: inspire-gateway/rssi
availability: inspire-gateway/availability
```

The availability topic is retained and contains `online` or `offline`.

## Web interface

Once connected to Wi-Fi, open the IP address printed on USB Serial or shown by
your router. The default HTTP Basic Auth sign-in is `admin` /
`inspire-gateway`; both values are defined in `config.h`.

The single embedded page provides live gateway status, every field from the
latest received packet, manual RF command-line transmission, and Wi-Fi/MQTT
settings. Browser transmissions pass through the same validation and nRF busy
check as MQTT commands.

Settings saved from the page are stored in NVS and take effect after its
automatic reboot. Empty password fields retain the existing passwords, and
the API never sends stored passwords back to the browser. **Restore compiled
defaults** clears the saved overrides and returns to the values in `config.h`.
The firmware remains station-only and does not create a setup access point.

## Home Assistant discovery

MQTT discovery is enabled by default. The ESP publishes retained discovery
configurations after every MQTT connection and again when Home Assistant sends
its `online` birth message on `homeassistant/status`.

Home Assistant creates one device from the retained discovery document at
`homeassistant/device/inspire-gateway/config`:

```text
Name:         Inspire-Gateway
Manufacturer: PricelessToolkit
Model:        Inspire Gateway
```

It exposes:

- `Wi-Fi RSSI` sensor in dBm, using `MQTT_RSSI_TOPIC`. It reports the ESP32's
  Wi-Fi signal every 30 seconds. The most recent value is retained so it is
  restored after Home Assistant restarts. The separate `rssi` value inside an
  RX JSON message remains the nRF receiver's signal strength for that RF packet.
  This entity is categorized as diagnostic in Home Assistant.
- Separate sensors make the useful received-packet fields visible on the device
  page: `Address`, `Protocol`, `RF RSSI`, `Payload Length`, `Payload`,
  `Protocol Valid`, `Prefix`, `Action`, `Button`, `Counter`,
  `Remote Packet Count`, `Check`, `Received CRC`, and `Calculated CRC`.
The raw TX topic is not exposed as a Home Assistant entity because it controls
multiple possible lights and requires structured JSON. Publish commands through
an automation, script, or MQTT action. Never publish TX messages as retained.

## Wiring

Both boards use 3.3 V logic and must share ground:

```text
XIAO ESP32-C3 D9 / GPIO9 TX  -> nRF52840 P0.06 RX
XIAO ESP32-C3 D10 / GPIO10 RX <- nRF52840 P0.08 TX
XIAO ESP32-C3 GND            <-> nRF52840 GND
```

UART is fixed at 921600 baud, 8N1 by default.

## MQTT TX command

Publish a non-retained JSON object to the configured TX topic:

```json
{"protocol":"gdansk_inspire","address":"55E96499B5","repeat":50,"payload":"9A62EB6F8D8DA80FA1827D58"}
```

That is the proven first-light ON command. OFF is:

```json
{"protocol":"gdansk_inspire","address":"55E96499B5","repeat":50,"payload":"9A62EB6F8D95B00FA181D528"}
```

The fields are deliberately explicit:

- `protocol`: registered nRF radio plugin name
- `address`: required five-byte RF address / exactly ten hexadecimal characters
- `repeat`: plugin-specific repeat count
- `payload`: protocol-specific hexadecimal payload; up to 188 bytes

The five-byte address is forwarded on every command and becomes the nRF's
active receive address afterward. Known `gdansk_inspire` remotes share
`55E96499B5`; their payload prefixes identify individual profiles. A changed
address also needs a payload/CRC valid for that address.

Example for the long-frame plugin:

```json
{"protocol":"2ANO4-25W2R4G","address":"4E22C32B40","repeat":150,"payload":"38103B2650AB2B39"}
```

**Never retain a message on the TX topic.** MQTT brokers deliver retained
messages again after the ESP reconnects and subscribes, which would repeat an
old light command unexpectedly. RX events are also published non-retained.

Example Home Assistant `mqtt.publish` action data:

```yaml
topic: inspire-gateway/tx
payload: >-
  {"protocol":"gdansk_inspire","address":"55E96499B5","repeat":50,"payload":"9A62EB6F8D8DA80FA1827D58"}
retain: false
```

## MQTT RX event

The nRF prints every received RF frame to its USB diagnostics but forwards only
the first packet of each burst over hardware UART. The ESP therefore normally
publishes one event per physical button press:

```json
{"protocol":"gdansk_inspire","address":"55E96499B5","rssi":-52,"length":12,"s1":5,"payload":"9A62EB6F8D8DA80FA1827D58","protocol_valid":true,"event_type":"ON","prefix":"9A62EB6F","action":"0x11B1","button":"ON","counter":181,"remaining":500,"check":"0x182","crc_received":"7D58","crc_calculated":"7D58"}
```

Known button names are `ON`, `OFF`, `BRIGHT_UP`, `BRIGHT_DOWN`, `WARM`, `COLD`,
and `DEFAULT`; unknown action codes are reported as `OTHER`. The raw payload is
always included so it can be copied into another controller.

The decoder validates the protocol CRC using the address from the received
frame. Malformed UART lines are ignored. Correctly formed but unfamiliar frames
can still be published with `protocol_valid:false` for diagnosis.

Up to eight decoded RX events are buffered in RAM while MQTT is disconnected;
if that queue fills, the oldest event is discarded.

When a USB terminal is connected, USB Serial at 115200 baud logs
`MQTT RX published` with the ESP32 millisecond timestamp and JSON payload after
an immediate MQTT publish succeeds. It logs `MQTT RX queued` when an event must
be buffered and `MQTT queued RX published` when a buffered event is subsequently
sent. All USB writes are skipped when no terminal is connected so diagnostics
can never delay MQTT publication.

## Build

Required Arduino libraries:

- PubSubClient 2.8
- ArduinoJson 7.x

Tested to compile with ESP32 Arduino core 3.3.11:

```sh
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3 Inspire_Gateway/esp32
```

USB Serial diagnostics use 115200 baud. The nRF UART uses 921600 baud.
