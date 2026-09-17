# Local AI guide: reverse engineering 2.4 GHz remote controls

This is a standalone, protocol-neutral workflow for analyzing an unknown
2.4 GHz remote with HackRF, Python, and an nRF52840. It deliberately contains
no project filenames, learned payloads, device-specific addresses, or previous
device results.

Only receive or transmit devices you own or are authorized to test. Keep tests
local and use the shortest practical transmission time.

## Required hardware

- HackRF One or compatible SDR
- suitable 2.4 GHz antenna
- computer with USB
- remote and its receiving appliance
- nRF52840 development board for Nordic-compatible GFSK experiments


## Required software

- HackRF host tools: `hackrf_info`, `hackrf_transfer`, `hackrf_sweep`
- Python 3
- NumPy
- SciPy
- Matplotlib
- pySerial
- Arduino CLI and an nRF52840 board core
- optional Gqrx, SDR++, Inspectrum, Universal Radio Hacker, or GNU Radio

Example Python environment:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install numpy scipy matplotlib pyserial
```

Confirm the SDR:

```sh
hackrf_info
```

Inspect the installed command syntax when uncertain:

```sh
hackrf_transfer --help
hackrf_sweep --help
```

## Rules for an AI agent

1. Do not assume Bluetooth, Wi-Fi, Zigbee, nRF24, OOK, or any named protocol
   before examining the signal.
2. Preserve every original IQ file unchanged.
3. Record center frequency, sample rate, gains, button, press order, and time.
4. Separate measured facts, strong inferences, and untested hypotheses.
5. Confirm results across multiple frames and multiple physical presses.
6. Change one receiver or transmitter hypothesis at a time.
7. Prove reception before attempting transmission.
8. Replay a complete captured frame before trying to generate fields.
9. Never call a protocol decoded merely because one noisy bitstream looks
   plausible.
10. Keep failed hypotheses in notes so another model does not repeat them.

## 1. Locate the RF signal

### Search before narrowing

Product documentation and regulatory filings provide leads, not proof. Search
the full relevant band while making quick press-and-release actions.

An optional sweep command has this general form:

```sh
hackrf_sweep -f 2400:2485 -w 1000000 -l 24 -g 24
```

Verify the precise options against the installed HackRF version. Waterfall
software is often easier for interactive discovery.

Cheap remotes may transmit only after release or may behave differently while
held. Try quick taps, not only long holds.

### Broad IQ capture

Example 10 MHz capture:

```sh
hackrf_transfer \
  -r broad_capture.iq \
  -f 2425000000 \
  -s 10000000 \
  -l 24 \
  -g 24 \
  -n 100000000
```

Important values:

- `-f`: center frequency in Hz
- `-s`: complex sample rate in samples/second
- `-l`: receive LNA gain
- `-g`: receive VGA gain
- `-n`: number of complex samples
- `-r`: raw output filename

Duration is:

```text
duration_seconds = sample_count / sample_rate
```

HackRF raw RX uses two signed 8-bit bytes per complex sample, so expected size
is:

```text
file_bytes = sample_count * 2
```

### Recenter and capture clean button presses

After finding the signal, choose a center that places it slightly away from
zero frequency. This avoids the HackRF DC spike. Use a sample rate at least two
to four times the occupied bandwidth.

Capture:

1. a quiet baseline;
2. one isolated quick tap;
3. two or three separated taps of the same button;
4. every button in a written order;
5. a second pass of every button to test stability.

Leave enough silence between presses to distinguish physical presses from the
device's internal retransmissions.

## 2. Set gain correctly

Maximum gain is usually wrong for a nearby transmitter.

- Begin low when the remote or transmitter is close to the HackRF.
- Increase gain only until bursts are clearly above the noise floor.
- Capture a baseline with exactly the same settings.
- If the waveform clips near signed values −128 or +127, lower gain.
- If the spectrum rises across most of the capture bandwidth during TX, suspect
  front-end overload.
- A clipped capture can prove that RF energy exists, but it cannot reliably
  prove occupied bandwidth, deviation, or bits.

For comparing an nRF transmitter located close to HackRF, start with zero LNA
and zero VGA gain.

## 3. Load HackRF IQ in Python

HackRF stores interleaved signed 8-bit I/Q values:

```text
I0 Q0 I1 Q1 I2 Q2 ...
```

Correct loader:

```python
from pathlib import Path
import numpy as np

path = Path("capture.iq")
sample_rate = 4_000_000.0
capture_center_hz = 2_407_000_000.0

raw = np.memmap(path, dtype=np.int8, mode="r")
raw = raw[: len(raw) // 2 * 2]
pairs = raw.reshape(-1, 2).astype(np.float32)
iq = pairs[:, 0] + 1j * pairs[:, 1]
```

Never interpret this format as unsigned bytes, float IQ, or 16-bit IQ.

For many analyses, suppress the static DC term:

```python
iq_no_dc = iq - np.mean(iq)
```

Do not blindly remove the mean when exact raw amplitude or hardware replay is
the goal. Use it only in the analysis path.

## 4. Plot magnitude versus time

Use 0.1–1 ms power bins for an overview:

```python
import matplotlib.pyplot as plt

bin_seconds = 0.001
bin_samples = int(sample_rate * bin_seconds)
count = len(iq) // bin_samples
blocks = iq[: count * bin_samples].reshape(count, bin_samples)
power = np.mean(np.abs(blocks) ** 2, axis=1)
power_db = 10 * np.log10(power + 1e-12)
time_s = np.arange(count) * bin_seconds

plt.plot(time_s, power_db)
plt.xlabel("Time (s)")
plt.ylabel("Mean power (dB, relative)")
plt.grid(alpha=0.25)
plt.tight_layout()
plt.show()
```

Use quiet/active percentiles to choose a threshold. Avoid a universal fixed
threshold because gain and interference vary.

From detected runs calculate:

- burst start/end times;
- RF-active duration;
- start-to-start interval;
- number of retransmissions per physical press;
- entire press duration.

Merge only short holes and reject short impulses. Always visually check the
detected runs against the original plot.

## 5. Create a spectrogram/waterfall

```python
from scipy.signal import spectrogram

frequency, time_s, psd = spectrogram(
    iq,
    fs=sample_rate,
    window="hann",
    nperseg=256,
    noverlap=224,
    return_onesided=False,
    mode="psd",
)

order = np.argsort(frequency)
frequency = frequency[order]
psd = psd[order]
psd_db = 10 * np.log10(psd + 1e-12)

plt.pcolormesh(
    time_s,
    (capture_center_hz + frequency) / 1e6,
    psd_db,
    shading="auto",
)
plt.xlabel("Time (s)")
plt.ylabel("Frequency (MHz)")
plt.colorbar(label="Relative PSD (dB)")
plt.tight_layout()
plt.show()
```

Use two views:

- a long, lower-resolution overview for physical presses;
- a short zoom showing individual packets and frequency states.

Nearby Wi-Fi is wide and bursty. Do not confuse unrelated Wi-Fi bursts with
remote transmissions merely because they overlap in time.

## 6. Measure carrier offset and occupied bandwidth

Compute a Welch PSD over detected active samples and compare it with a quiet
baseline captured using identical settings:

```python
from scipy.signal import welch

frequency, active_psd = welch(
    active_iq,
    fs=sample_rate,
    window="hann",
    nperseg=16384,
    noverlap=8192,
    return_onesided=False,
)
_, quiet_psd = welch(
    quiet_iq,
    fs=sample_rate,
    window="hann",
    nperseg=16384,
    noverlap=8192,
    return_onesided=False,
)
```

Sort by frequency and calculate positive excess power:

```python
order = np.argsort(frequency)
frequency = frequency[order]
excess = np.maximum(active_psd[order] - quiet_psd[order], 0)

carrier_offset_hz = np.sum(frequency * excess) / np.sum(excess)
estimated_carrier_hz = capture_center_hz + carrier_offset_hz
```

For occupied bandwidth, form the cumulative sum of `excess` and find the
frequencies enclosing the center 95% or 99% of power.

Absolute carrier estimates include HackRF and remote oscillator error. Record
both the measured value and the likely nominal channel.

## 7. Identify modulation without guessing

Compare magnitude, phase, and instantaneous frequency.

- OOK: RF power switches between carrier-present and carrier-absent.
- ASK: two or more amplitude levels with similar carrier phase/frequency.
- 2-FSK: nearly constant envelope and two frequency levels.
- GFSK: two frequency levels with smooth Gaussian-shaped transitions.
- PSK: nearly constant envelope and discrete phase changes.
- wide or multi-level signals require additional hypotheses.

Quadrature discriminator:

```python
instantaneous_frequency_hz = (
    np.angle(iq[1:] * np.conj(iq[:-1]))
    * sample_rate
    / (2 * np.pi)
)

instantaneous_frequency_hz = np.convolve(
    instantaneous_frequency_hz,
    np.ones(3) / 3,
    mode="same",
)
```

Before discrimination, mix the measured carrier to baseband and low-pass it:

```python
from scipy.signal import butter, sosfilt

offset_hz = estimated_carrier_hz - capture_center_hz
n = np.arange(len(iq))
mixed = iq * np.exp(-2j * np.pi * offset_hz * n / sample_rate)

sos = butter(6, lowpass_hz, fs=sample_rate, output="sos")
filtered = sosfilt(sos, mixed)
```

Choose `lowpass_hz` wider than the signal but narrow enough to reject unrelated
channels. Discard filter startup samples.

If DC still dominates, offset-tune the original capture or explicitly notch a
small band around zero. Do not mistake the HackRF DC spike for an FSK state.

## 8. Estimate symbol/bit rate

Use several independent methods:

1. measure common transition spacing in the discriminator;
2. inspect its spectrum for clock features;
3. test likely samples-per-symbol values;
4. test every possible symbol phase;
5. choose the rate/phase that maximizes two-level separation and repeated-frame
   agreement.

For a candidate rate:

```text
samples_per_symbol = sample_rate / symbol_rate
```

If it is an integer, average samples near each symbol center. Try all starting
phases. Threshold between the low/high discriminator clusters. Try both bit
polarities because discriminator polarity is arbitrary.

Do not round a clearly non-integer relationship without checking clock error
and resampling. Long recordings may drift slightly because the HackRF and
remote use independent oscillators.

## 9. Build repeated-frame consensus

One physical press may contain hundreds of retransmissions. Use them to reduce
noise:

1. detect each frame start;
2. demodulate every candidate symbol phase;
3. align candidates by small positive and negative bit shifts;
4. test inverted polarity;
5. majority-vote each bit;
6. retain per-bit confidence;
7. compare consensus frames from separate physical presses.

Do not allow circular shifts to make unrelated data appear equal at the frame
edges. Ignore wrapped edge regions when scoring.

Useful questions:

- Is the entire frame identical within one press?
- Is it identical across later presses?
- Which bits change every retransmission?
- Which bits change only when the button changes?
- Which bits identify the remote or paired light?
- Are changing fields counters, countdowns, CRCs, or whitening artifacts?

Long repeated constants or alternating runs may indicate preamble and address,
but confirm them with hardware reception.

## 10. Detect frequency hopping

Track the same packet train across frequency, not all energy in the waterfall.

Evidence for hopping requires the remote's characteristic timing/data to move
among distinct carriers. Unrelated Wi-Fi energy is not evidence.

Compare every physical press and every retransmission. Report:

- no hopping observed within captured bandwidth/time;
- fixed carrier;
- deterministic channel sequence;
- or insufficient capture bandwidth.

## 11. Use nRF52840 as a receiver

The nRF52840 RADIO peripheral is useful when measurements suggest Nordic-like
1 Mbit or 2 Mbit GFSK. Configure one hypothesis at a time:

- RF channel/frequency;
- `Nrf_1Mbit` or another measured mode;
- 8-bit or 16-bit preamble;
- three-to-five-byte address;
- bit endianness;
- fixed or dynamic payload length;
- whitening on/off;
- hardware CRC on/off and its parameters.

A correct hypothesis should produce:

- LED activity only during real presses;
- stable packets across retransmissions;
- timing matching the IQ recording;
- consistent action data across duplicate presses;
- few errors at good RSSI.

A receiver that triggers on background noise is not proof. A candidate address
must remain stable across clean captures and preferably work in hardware.

## 12. Build and upload nRF firmware

Example for a Nice!Nano-compatible generic nRF52840 board using the Adafruit
Feather variant:

```sh
arduino-cli compile \
  --fqbn adafruit:nrf52:feather52840 \
  <firmware-directory>

arduino-cli upload \
  -p /dev/ttyACM0 \
  --fqbn adafruit:nrf52:feather52840 \
  <firmware-directory>
```

After upload, the USB port may disappear and reappear. Find it with:

```sh
arduino-cli board list
```

Check port ownership if opening fails:

```sh
fuser -v /dev/ttyACM0
```

Terminate only a confirmed stale serial-monitor process.

## 13. Serial control for the nRF modem firmware

Use newline-delimited ASCII at 921600 baud. Native USB usually ignores the
physical baud electrically, but configure the terminal consistently.

### Transmit command

```text
TX protocol=<plugin-name> address=<10-hex> repeat=<decimal> payload=<hexadecimal>
```

Example with placeholders:

```text
TX protocol=protocol_1 address=0102030405 repeat=100 payload=A1B2C3D4
```

Rules:

- end every command with LF; CRLF is accepted;
- `address` is required, contains exactly five RF-address bytes, and becomes
  the active receive address after TX;
- hexadecimal must contain complete byte pairs;
- `repeat` must be within the plugin's limit;
- payload length/content must pass the plugin validator;
- invalid commands are silently ignored;
- there are no READY, OK, or TX_DONE responses;
- the selected TX protocol remains active for reception afterward;
- the radio automatically returns to RX after transmission.

Wait before sending another command:

```text
wait_ms >= repeat * packet_interval_ms + 5
```

Python sender:

```python
import serial
import time

port = serial.Serial("/dev/ttyACM0", 921600, timeout=0.1)
time.sleep(0.2)
port.write(
    b"TX protocol=protocol_1 address=0102030405 "
    b"repeat=100 payload=A1B2C3D4\n"
)
port.flush()
time.sleep(0.5)
port.close()
```

### Normal receive output

```text
RX protocol=<name> time_us=<time> rssi=<dBm> address=<hex> len=<n> s1=<n> payload=<hex>
```

USB may print every received RF packet. Hardware UART should normally forward
only the first packet in a physical press to avoid flooding an ESP controller.

### Full-frame diagnostic commands

```text
DUMP_RX
DUMP_RX_ALL
DUMP_RX_STOP
```

- `DUMP_RX`: print one complete raw body from the next valid packet.
- `DUMP_RX_ALL`: print one raw body at the start of each receive burst.
- `DUMP_RX_STOP`: disable both dump modes.

Example format:

```text
RAW_RX protocol=<name> len=<bytes> action=<compact-hex> payload=<full-hex>
```

These are USB diagnostics. Keep the hardware-UART format compact for the ESP.
Write very long USB lines in chunks; some TinyUSB `printf` implementations use
small temporary buffers and corrupt long formatted strings.

For button learning:

1. select the desired RX plugin;
2. issue `DUMP_RX_ALL` over USB;
3. press every button twice in a written order;
4. use quick press/release and about one second separation;
5. save all output to a log;
6. issue `DUMP_RX_STOP`;
7. group full frames by compact action;
8. compare unique full-frame variants.

## 14. Validate transmission

Follow this order:

1. Verify the command parser and TX LED.
2. Capture the nRF transmission with HackRF at low gain.
3. Compare carrier, deviation, packet duration, and interval with the remote.
4. Compare demodulated emitted bits against intended bits.
5. Replay one exact complete received frame.
6. Confirm the real appliance responds.
7. Only then generate fields or use compact action commands.

The TX LED proves only that firmware entered its transmit loop. It does not
prove correct RF frequency, framing, payload, CRC, leader, or modulation.

### Synchronized HackRF/nRF capture

Start HackRF first, wait briefly, transmit through serial, and let the capture
finish. Use low gain and a unique filename.

Conceptual shell sequence:

```sh
hackrf_transfer -r nrf_test.iq -f <hz> -s <rate> -l 0 -g 0 -n <samples> &
hackrf_pid=$!
sleep 0.3
python3 send_command.py
wait "$hackrf_pid"
```

Do not run destructive cleanup commands around captures. Preserve failed test
files until their cause is understood.

## 15. Long leaders and unusual framing

Some receivers need energy or a bit leader before the normal address-bearing
packet. If an exact body receives correctly but direct TX does not control the
appliance, inspect symbols preceding the hardware preamble.

Possible approaches:

- a separate wake/AGC transmission followed immediately by the real packet;
- a protocol-specific custom TX callback;
- raw IQ replay if packet hardware cannot represent the waveform.

Do not embed a second desired preamble inside one already-started dummy packet
and assume the receiver will re-lock. Many packet receivers remain committed to
the first detected packet until it ends.

If a compact action appears inside a long body, do not patch only that field.
Capture and compare complete frames. Other portions may contain repeated
encodings, integrity fields, counters, or alternate representations.

## 16. Protocol plugin design

Keep each physical protocol in its own source file. A plugin should define:

- stable protocol name;
- displayed RF address;
- payload and repeat limits;
- packet interval and receive-burst gap;
- radio register configuration;
- payload validator;
- TX preparation;
- optional custom physical transmitter;
- RX packet view/compact extraction.

Recommended callbacks:

```text
configureRadio()
acceptsPayload(payload, length)
prepareTransmit(buffer, payload, length)
transmitPrepared(buffer)       # optional
receivedPacket(buffer)
```

The validator must reject incorrect lengths and unknown compact actions before
the radio begins. Never let an invalid short payload silently become another
valid action.

After adding a plugin:

1. register it in the protocol registry;
2. compile;
3. upload;
4. prove RX;
5. capture complete frames;
6. prove exact-frame TX;
7. prove compact/generated TX;
8. document confirmed values and unknowns.

## 17. Decide which radio to use

### nRF24L01

Use when the protocol matches supported Nordic packet framing, payload length,
addressing, rate, whitening, and CRC behavior. It is cheap but restrictive.

### nRF52840

Use for Nordic-like GFSK requiring flexible preambles, addresses, CRC settings,
longer fixed packets, direct RADIO register access, or custom multi-stage TX.
It is generally the best first embedded replacement for an unknown Nordic-like
2.4 GHz light remote.

### CC2500

Use when its configurable packet engine, frequencies, modulation, and deviation
match the signal. Check the actual frequency range and required protocol before
buying hardware.

### ESP32 built-in radio

Use primarily for Wi-Fi, MQTT, application logic, and UART coordination. Its
built-in radio is not a convenient arbitrary Nordic packet transmitter.

### HackRF/raw IQ replay

Use when the waveform cannot be represented by available packet radios. It is
flexible but more expensive, less power-efficient, timing-sensitive, and less
suitable for a small always-on gateway.

## 18. Troubleshooting checklist

### No visible signal

- verify antenna and `hackrf_info`;
- search the whole band;
- try quick release instead of holding;
- replace the remote battery;
- increase gain gradually;
- move the remote closer, but avoid overload;
- verify sample rate and tuned frequency units.

### Waterfall shows bursts but Python does not

- confirm signed `int8` interleaved IQ;
- verify sample count and duration;
- lower/raise threshold based on percentiles;
- inspect a short time window;
- remove DC only in the analysis copy;
- isolate the correct frequency channel before measuring magnitude.

### nRF LED blinks on remote presses but decoded bytes vary

- check RSSI and distance;
- verify address and preamble alignment;
- test bit endianness;
- check whitening;
- check static/dynamic length configuration;
- confirm CRC setting;
- reject false internal locks in long repetitive payloads;
- compare complete packets, not only extracted action bytes.

### TX LED works but appliance does nothing

- capture the emitted RF with HackRF;
- verify actual channel and frequency error;
- compare deviation and symbol rate;
- verify payload bit order;
- verify full packet, CRC, and companion fields;
- look for a wake/AGC leader;
- confirm sufficient repeats and interval;
- test an exact full received frame before generated data.

### Serial port is silent

- use the correct `/dev/ttyACM*` device;
- wait after upload/open;
- set 921600 baud;
- terminate a stale monitor holding the port;
- send LF after commands;
- remember that invalid commands have no error response.

## 19. Evidence required before declaring success

- clean labeled IQ from multiple physical presses;
- measured burst times and repetition interval;
- measured carrier offset and occupied bandwidth;
- modulation identified from amplitude and phase evidence;
- symbol rate confirmed by repeated-frame agreement;
- hopping assessed across the capture bandwidth;
- address/framing proven with hardware RX;
- complete frames captured for every button at least twice;
- changing fields documented;
- exact full-frame TX controls the appliance;
- compact/generated TX controls the appliance;
- receiver automatically resumes after TX;
- firmware compiles and uploads;
- commands and protocol documentation match the actual implementation.
