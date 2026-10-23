# LR1121 LoRaWAN example

This ESP-IDF project runs a LoRaWAN Class A end device on the onboard LR1121.
It uses OTAA, bounded join retries, periodic binary uplinks, RX1/RX2 downlinks,
and NVS-backed RadioLib nonce/session persistence. The onboard e-paper presents
a seven-line status page using the repository's full/fast refresh policy.

## Hardware and radio adapter

The pin values come from the board definitions in the downloaded component:

- `managed_components/waveshare__waveshare_boards/boards/esp32_s3_lr1121_epaper_2_9/board_peripherals.yaml`
- `managed_components/waveshare__waveshare_boards/boards/esp32_s3_lr1121_epaper_2_9/board_devices.yaml`

| Signal | ESP32-S3 connection |
| --- | --- |
| SPI host | `SPI2_HOST` |
| SCK | GPIO40 |
| MISO | GPIO46 |
| MOSI | GPIO45 |
| NSS | GPIO42 |
| DIO1 | GPIO6 |
| RESET | GPIO39 |
| BUSY | GPIO41 |
| DIO7 / DIO8 | Not connected |
| SPI clock | 8 MHz, mode 0 |
| TCXO | 3.0 V |

`board_lorawan_radio.cpp` reuses the board's `RadioLibHal`, `Module`, LR1121
pins, and internal DIO5/DIO6 RF-switch table. As in the known-working Starter,
the LR1121 adapter forces RadioLib's high-power PA path through
`setOutputPower()` and `checkOutputPower()`. This selects the board's verified
PA path; it does not bypass the configured power or regional maximum. SPI
transactions use the same board mutex as the e-paper driver. RadioLib owns
LoRaWAN modulation, regional channels, RX1/RX2 timing, and frame processing.

The repository does not contain a schematic or other authoritative hardware
design file. The table above is consistent with the current board YAML and the
existing LR1121 example, but it has not been independently checked against a
schematic.

## Dependencies

The component manifests select:

- ESP-IDF `>=5.5.0,<6.3.0`; see [framework compatibility](../../../docs/idf-compatibility.md).
- `jgromes/radiolib` 7.7.1, pinned exactly by this example.
- `waveshare/waveshare_boards`, which supplies Board Manager transitively.
- The project-local e-paper driver/font and their managed dependencies.

The board definition is supplied through the downloaded Waveshare component.
Follow the [board preparation instructions](../README.md#board-manager-integration)
for the temporary manual-copy step until the component publishes this product.

The implementation uses the RadioLib 7.7.1 APIs verified in the local component
source: `LR1121`, `LoRaWANNode`, `beginOTAA()`, `activateOTAA()`,
`sendReceive()`, `setADR()`, `setTxPower()`, `timeUntilUplink()`,
`getBufferNonces()`,
`setBufferNonces()`, `getBufferSession()`, `setBufferSession()`, and
`clearSession()`.

## Source layout

- `main.cpp`: validates provisioning, starts NVS and LR1121, calls
  `beginOTAA()`, restores state, and enters the runtime.
- `lorawan_node.*`: owns the regional band and channel/power/ADR policy.
- `lorawan_runtime.*`: owns join retries, the Class A send/receive loop,
  recovery delays, and reliable state commits.
- `lorawan_payload.*`: encodes uplinks and logs/parses bounded downlinks.
- `lorawan_session.*`: stores RadioLib nonce/session buffers and validates the
  saved session against the configured channel policy.
- `lorawan_ui.*`: owns the status model and full/fast e-paper refresh policy.

The startup order remains visible in `main.cpp`; protocol details are grouped
by responsibility so radio policy, payload format, persistence, and display
changes can be reviewed independently.

## Required configuration

Copy `main/lorawan_secrets.h.example` to `main/lorawan_secrets.h`, then enter
the four OTAA identity values from the network server. The real secrets file is
ignored by `.gitignore`; do not commit it. Edit `main/lorawan_config.h` for
the non-secret deployment policy.

| Setting | Purpose |
| --- | --- |
| `LORAWAN_JOIN_EUI` | JoinEUI, historically called AppEUI |
| `LORAWAN_DEV_EUI` | Device EUI |
| `LORAWAN_APP_KEY_BYTES` | 128-bit application root key |
| `LORAWAN_NWK_KEY_BYTES` | 128-bit network root key for LoRaWAN 1.1 |
| `LORAWAN_USE_VERSION_1_1` | `1` for 1.1, `0` for 1.0.x |
| `LORAWAN_REGION_SELECTION` | RadioLib regional channel plan |
| `LORAWAN_SUB_BAND` | Fixed-plan sub-band, or `0` for dynamic plans |
| `LORAWAN_SINGLE_CHANNEL_MODE` | Compatibility mode for the target one-channel gateway |
| `LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ` | Compatibility uplink frequency |
| `LORAWAN_SINGLE_CHANNEL_DR` | Fixed compatibility uplink data rate |
| `LORAWAN_TX_POWER_DBM` | Requested transmit power |
| `LORAWAN_UPLINK_PORT` | Application uplink FPort |
| `LORAWAN_UPLINK_INTERVAL_SECONDS` | Minimum application interval |
| `LORAWAN_CONFIRMED_UPLINK` | Confirmed (`1`) or unconfirmed (`0`) uplink |
| `LORAWAN_ADR_ENABLED` | Adaptive data rate |

The checked-in policy uses LoRaWAN 1.1.0, EU868, sub-band 0, standard
multichannel mode, ADR disabled, unconfirmed FPort 1 uplinks every 60 seconds,
and a requested power of 16 dBm. This is the appropriate mode for a normal
multichannel EU868 gateway. The known-working Starter's one-channel
compatibility behavior remains available by setting
`LORAWAN_SINGLE_CHANNEL_MODE=1`; its frequency and data rate then come from
`LORAWAN_SINGLE_CHANNEL_FREQUENCY_HZ` and `LORAWAN_SINGLE_CHANNEL_DR`.

Select the plan that is legal at the deployment location and configure the
same plan in the network server. US915 and AU915 require sub-band 1 through 8;
CN470 requires sub-band 1 through 12. The other included plans require
sub-band 0. Compile-time checks reject inconsistent selections.

The firmware does not override RadioLib's regional duty-cycle or dwell-time
settings. Before an uplink it checks `timeUntilUplink()` and waits when the
scheduler reports a remaining delay. Local regulation, antenna gain, network
policy, and the regional maximum still apply.

With compatibility mode enabled, the node copies the EU868 regional table but
keeps only 868.1 MHz at DR3. It disables ADR and re-locks RadioLib's dynamic
channel masks before every join attempt, after JoinAccept, and before every
uplink. RX2 remains the EU868 default.

With compatibility mode disabled, RadioLib starts EU868 with the three
mandatory uplink/join channels: 868.1, 868.3, and 868.5 MHz. It selects from
the enabled channels for successive uplinks; the order is intentionally
randomized. A JoinAccept CFList or later network MAC commands may add the
remaining EU868 channels. The LR1121 initialization message still shows
868.1000 MHz because that is only the bootstrap frequency, not a fixed uplink
frequency.

Changing the region, sub-band, or single-channel policy invalidates only the
saved `sess_sc` channel/session state. The firmware retains the OTAA `nonce`,
writes the new `chan_policy` marker, and performs one normal OTAA rejoin so an
old one-channel mask cannot leak into multichannel operation.

The example secrets template and the fallback definitions use conspicuous
`0xFF` placeholders. At startup the application also rejects all-zero keys, an
unset DevEUI, and an out-of-plan transmit power. It identifies the two
configuration files to edit and does not transmit a JoinRequest while
credentials are invalid. Keys are never printed.

## ChirpStack device profile

For the default LoRaWAN 1.1 configuration, use these values in ChirpStack:

| ChirpStack field | Value |
| --- | --- |
| Region | Same as `LORAWAN_REGION_SELECTION` (`EU868` by default) |
| MAC version | `LoRaWAN 1.1.0` |
| Regional parameters revision | `RP002-1.0.4` |
| Activation | OTAA / Class A |
| DevEUI | Same 16 hexadecimal digits as `LORAWAN_DEV_EUI` |
| Application key | Same byte order as `LORAWAN_APP_KEY_BYTES` |
| Network key | Same byte order as `LORAWAN_NWK_KEY_BYTES` |

RadioLib 7.7.1 selects LoRaWAN 1.1 when `beginOTAA()` receives a non-null
NwkKey. It serializes EUI integer values into the LoRaWAN wire order itself, so
do not reverse `LORAWAN_JOIN_EUI` or `LORAWAN_DEV_EUI`. Key byte arrays are
entered in the same displayed order as ChirpStack and must not be reversed.

If `LORAWAN_USE_VERSION_1_1` is changed to `0`, configure the ChirpStack
profile for `LoRaWAN 1.0.4`; RadioLib then passes no NwkKey and authenticates
the JoinRequest with AppKey. `reg_params_revision` is not sent as a field in
an uplink. It is server-side device-profile metadata, while this firmware
obtains the corresponding behavior from RadioLib's compiled regional tables.
RadioLib 7.7.1 identifies those tables as RP002 Regional Parameters 1.0.4.

For an internal ChirpStack join-server, JoinEUI is normally only relevant if
the server is configured to route that JoinEUI to an external join-server.
When such routing is configured, the firmware JoinEUI and routing entry must
agree.

During commissioning, keep both the gateway and device `LoRaWAN frames` views
open before powering the device:

1. No JoinRequest at the gateway means an RF plan, data-rate, antenna, distance,
   or gateway-online problem.
2. JoinRequest at the gateway but not at the device means the DevEUI does not
   match the provisioned device.
3. JoinRequest without JoinAccept means ChirpStack rejected it (commonly MAC
   version, key, or DevNonce) or the gateway rejected the scheduled downlink.
4. JoinAccept in ChirpStack but `no-join-accept` on the device points to the
   gateway downlink or RX1/RX2 radio plan.

## OTAA and recovery flow

Startup performs these steps:

1. Validate credentials and regional policy.
2. Render the initial e-paper status frame.
3. Initialize NVS and the board-defined LR1121 adapter.
4. Create `LoRaWANNode` for the selected region and sub-band.
5. Call `beginOTAA()` with the configured identity and keys.
6. Restore `nonce` using RadioLib's native buffer.
7. Compare `chan_policy`; discard only an incompatible `sess_sc`.
8. Restore a compatible `sess_sc` using RadioLib's native buffer.
9. Apply ADR, transmit power, and the selected channel policy.
10. Restore the prior session or perform an OTAA join.

Each join batch has a fixed maximum number of attempts. Failed attempts use
exponential backoff with jitter and also respect `timeUntilUplink()`. After a
batch is exhausted, the application waits for the configured cooldown before
trying another bounded batch.

After joining, `sendReceive()` sends an uplink and opens both Class A receive
windows. A return value of 1 or 2 identifies RX1 or RX2; zero means both windows
completed without a downlink. The log includes event error codes, FPort,
frequency, data rate, transmit power, frame counters, RSSI, and SNR.

In standard EU868 mode, each successful uplink also updates a non-invasive
coverage counter such as `default-seen=2/3`. This reports which of 868.1,
868.3, and 868.5 MHz have actually been observed; it does not override
RadioLib or network-provided channel masks.

Transient send failures wait for the normal application interval. After the
configured consecutive-failure limit, the active session is cleared while
OTAA nonces are retained, followed by a controlled rejoin delay. No failure
path enters a high-speed radio loop.

The LR1121 is put into sleep during application, join-backoff, and recovery
waits. FreeRTOS delays keep the example straightforward and preserve the USB
serial console.

## E-paper status UI

The display shows the Join/Joined/session state, selected region and sub-band,
ADR/confirmed mode, successful uplink/downlink counts, last uplink frame counter,
application sequence, last valid RSSI/SNR, and the last status/error code.
Before the first uplink, the last line shows FPort and period. Afterwards it
shows the actual TX frequency and, in standard EU868 mode, channel coverage
such as `TX 868.3000MHz CH 2/3`.

The first status frame uses a full waveform. The shared driver then performs up
to ten fast full-screen refreshes before automatically inserting another full
waveform to limit ghosting. Join and radio status changes are refreshed before a
new radio operation or after the synchronous RadioLib call has completed.
Uplink-result and downlink pages are rendered only after both Class A receive
windows have closed, so e-paper SPI traffic cannot overlap RX1/RX2 timing.

The example intentionally keeps the e-paper controller awake between uplinks:
putting it to sleep resets the driver's fast-refresh budget and makes the next
frame a full refresh. The LR1121 still sleeps during the wait. Applications
that prioritize minimum standby power can instead sleep the display and accept
a full refresh after every wake.

Display failures are warnings only and do not stop LoRaWAN. The LR1121 and
e-paper share the board-managed SPI2 bus and the same mutex, so every refresh
finishes and releases the bus before the next radio transaction.

## NVS persistence

This example keeps the known-working Starter's native RadioLib persistence
buffers byte-for-byte unchanged. It opens the NVS namespace `lorawan` and
stores those two buffers plus one non-secret channel-policy marker:

| NVS key | Size | Contents |
| --- | ---: | --- |
| `nonce` | 14 bytes | OTAA nonce/base state and provisioning binding |
| `sess_sc` | 302 bytes | Session keys, channel state, and frame counters |
| `chan_policy` | 8 bytes | Schema, region, sub-band, and single-channel policy |

The restore order is fixed: `beginOTAA()` first establishes the current
JoinEUI, DevEUI, keys, mode, and region; the firmware then loads `nonce` with
`setBufferNonces()`. Before loading `sess_sc`, it compares `chan_policy` with
the current region, sub-band, and single-channel selection. A matching session
is passed unchanged to `setBufferSession()`. RadioLib still validates the
credential/mode/region binding and nonce/session signature.

The marker contains no credentials or session keys. If it is missing or
different, the firmware erases only `sess_sc`, retains `nonce`, commits the
new marker, and continues with one fresh OTAA join. This one-time migration
prevents a session created with mask `0x0001` from restoring single-channel
behavior after `LORAWAN_SINGLE_CHANNEL_MODE` changes to `0`. A RadioLib buffer
that is missing or rejected is also logged and falls back to OTAA.

Every `activateOTAA()` result is followed immediately by a committed `nonce`
write. When a JoinRequest was transmitted, RadioLib has already advanced
DevNonce; saving after every result keeps the restore and fresh-join paths
simple and safe. A new or restored session is then committed as `nonce`,
`sess_sc`, and `chan_policy`.

After `sendReceive()`, the full state is committed whenever the transaction
completes normally or RadioLib's internal next `FCntUp` has advanced. The
second condition also covers negative TX/RX results where a frame may already
have been sent, preventing a counter rollback after reboot. NVS write failures
pause further radio operations and retry at a bounded interval.

After the configured consecutive-send-failure limit, controlled recovery calls
`clearSession()`, erases only `sess_sc`, keeps `nonce` and `chan_policy`, and
waits before OTAA. A normal boot with a matching marker restores state without
erasure. The LoRaWAN restore logic does not inspect other keys in the NVS
partition.

As in the Starter, `nvs_flash_init()` reinitializes the default NVS partition
only if ESP-IDF reports no free pages or a newer incompatible NVS format. A
complete `idf.py erase-flash` also removes LoRaWAN state and should be used
only for deliberate recommissioning together with the corresponding
network-server device state; it is not a normal recovery step.

The session blob contains derived session keys, while the configured root keys
remain in application flash. Production firmware should enable the ESP32-S3
security features appropriate to its threat model, such as flash encryption,
encrypted NVS, secure boot, and protected key provisioning.

## Uplink payload

The application sends a fixed 15-byte, big-endian payload:

| Byte | Length | Meaning |
| --- | ---: | --- |
| 0 | 1 | Payload format version (`0x01`) |
| 1 | 1 | Flags: bit 0 confirmed, bit 1 restored session, bit 2 prior downlink |
| 2 | 4 | Application sequence number |
| 6 | 4 | Uptime in seconds |
| 10 | 4 | Free heap in bytes |
| 14 | 1 | ESP-IDF reset-reason value |

The application sequence starts at zero after boot and advances after each
send attempt. An `uplink-unavailable` result does not advance it because no
frame was transmitted. LoRaWAN frame counters remain in `sess_sc`.

## Downlinks

Downlinks are logged with their FPort, RX window, length, frame counter, RSSI,
SNR, and bounded hexadecimal chunks. FPort `LORAWAN_COMMAND_PORT` recognizes
only demo byte `0x01`, which writes a log message and changes no device
configuration. Unknown commands are ignored. Arbitrary downlink data is never
executed or applied as radio policy.

## Build, flash, and monitor

Prepare the component board definition as linked above, then run in this example
directory with an activated ESP-IDF environment (Ubuntu Bash):

```bash
idf.py bmgr -b esp32_s3_lr1121_epaper_2_9
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with the board's serial port. A configured standard-EU868 device
should list 868.1, 868.3, and 868.5 MHz at startup. After successful uplinks,
the event log shows the actual TX frequency and the coverage line advances
toward `default-seen=3/3`; the e-paper mirrors this as `CH 3/3`. The log also
shows policy migration, NVS restore status, bounded OTAA attempts, RX1/RX2
results, and the next regulatory/application delay. No expected log or screen
contains AppKey or NwkKey.

