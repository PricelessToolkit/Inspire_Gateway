# Ceiling-light remote commands

Send each command as one line ending with LF. The nRF UART/USB speed is
921600 baud. Commands produce no response.

## Remote 1 — `gdansk_inspire`, profile `9A62EB6F`

RF address: `55E96499B5`

```text
# ON
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A62EB6F8D8DA80FA1827D58

# OFF
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A62EB6F8D95B00FA181D528

# BRIGHTNESS UP
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A62EB6F8D9CB80FA087AB60

# BRIGHTNESS DOWN
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A62EB6F8DA4D00FA0D053A0

# WARMER
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A62EB6F8DACA00FA0AF9C80

# COLDER
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A62EB6F8DB4880FA09E0E08

# DEFAULT COLOR AND BRIGHTNESS
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A62EB6F8DECE80FA0A09758
```

## Remote 2 — `gdansk_inspire`, profile `9A65E36F`

RF address: `55E96499B5`

```text
# ON
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A65E36F8D8E780FA55B0268

# OFF
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A65E36F8D96800FA5BC9358

# BRIGHTNESS UP
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A65E36F8D9E880FA5BA7470

# BRIGHTNESS DOWN
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A65E36F8DA6900FA59F9010

# COLDER
TX protocol=gdansk_inspire address=55E96499B5 repeat=50 payload=9A65E36F8DB6980FA58782B0
```

`WARMER` and `DEFAULT` were not captured for remote 2. Learn those two complete
12-byte payloads from the physical remote; do not substitute remote-1 payloads
because the profile and CRC differ.

## Remote 3 — `2ANO4-25W2R4G`

RF address: `4E22C32B40`

```text
# ON
TX protocol=2ANO4-25W2R4G address=4E22C32B40 repeat=150 payload=38103B2650AB2B39

# OFF
TX protocol=2ANO4-25W2R4G address=4E22C32B40 repeat=150 payload=229D6A0896C4F0CB

# BRIGHTNESS UP
TX protocol=2ANO4-25W2R4G address=4E22C32B40 repeat=150 payload=00E028A6B3650AFC

# BRIGHTNESS DOWN
TX protocol=2ANO4-25W2R4G address=4E22C32B40 repeat=150 payload=260C7F154AB22929

# WARMER
TX protocol=2ANO4-25W2R4G address=4E22C32B40 repeat=150 payload=1238BD39812E228B

# COLDER
TX protocol=2ANO4-25W2R4G address=4E22C32B40 repeat=150 payload=13F018B8A0853B96

# DEFAULT COLOR AND BRIGHTNESS
TX protocol=2ANO4-25W2R4G address=4E22C32B40 repeat=150 payload=0513E01CE9636422
```

Wait about 210 ms after a 50-repeat `gdansk_inspire` command and about 525 ms
after a 150-repeat `2ANO4-25W2R4G` command before sending the next line.
