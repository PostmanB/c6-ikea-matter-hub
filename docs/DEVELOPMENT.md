# C6 local Matter hub — experimental full prototype

**Status: On/Off, brightness and colour controls work locally on one C6, including charger power through the UART/USB-UART socket. Use that socket for standalone power; the native USB socket's charger startup has been inconsistent. Pairings survive ESP restart.**
The physical C6 has passed
the separate native Thread diagnostic: leader/router mode, SRP server, local
DNS-SD self-test, and preservation of its random Thread dataset over reset.
It now runs this full Matter hub. The hub became Thread leader with its SRP
server running and its own Matter services registered. Console status reported
202,440 bytes of internal heap free, with a 176,128-byte largest free block.
The retail bulb passed production attestation, joined Thread, completed a secure
CASE session and commissioning, and acknowledged Off and On. All three remotes
were commissioned and subscribed concurrently; their button events produced
acknowledged On/Off commands and the user confirmed physical control from all
three. Long-term reliability remains unverified.

## Architecture

The C6 runs a Thread Full Thread Device and SRP/DNS-SD server on its
native radio, plus a Matter commissioner/controller with BLE central support.
All four IKEA nodes join its Thread network and its own Matter fabric. Wi-Fi is
disabled. A border router is unnecessary for communication within this isolated
Thread network, provided the controller's Thread DNS backend can resolve the
devices through the local SRP server. This interaction passed with the actual
KAJPLATS and all three BILRESAs. With all three subscriptions active, internal
heap free was 197,300 bytes and the largest block was 172,032 bytes. Shared radio
scheduling passed sequential commissioning while previously paired remotes
remained subscribed. After an ESP reset, all three subscriptions recovered
automatically within 28 seconds. Standalone charger operation passed after
USB console initialization and logging were gated on an actual computer connection.
The earlier saved charger trace showed all three subscriptions active and 15
successful bulb commands, with no failures and no USB host. With the final
brightness/colour extension, native-socket charger startup failed before recording
the first application trace. The same firmware then passed standalone control
when the charger cable was moved to the UART/USB-UART socket. The exact cause
of that native-socket startup failure has not been established.

This is a deliberate prototype of a combination that Espressif does not supply
as a validated single-C6 controller example. Their controller example is the
basis for the commissioning APIs. A build failure or runtime allocation failure
will be recorded and investigated; the existence of source code does not prove
that the complete combination fits or works.

BILRESA uses Generic Switch events, so this project interprets single-press,
double-press and hold/release events.
The published two-button descriptor lacks the Binding/OnOff client combination
needed to make a remote send commands directly to the bulb. Groups do not turn
a Generic Switch event server into an OnOff command client. The hub therefore
subscribes to all three remotes and sends acknowledged, idempotent On/Off,
absolute brightness and colour commands to the single bulb. Upper/lower
endpoints are discovered by reading Descriptor ServerList;
when there are exactly two switch endpoints, the lower endpoint number is mapped
to On and the higher to Off. Verify the physical mapping and reverse it if needed.

## What is implemented

- BLE commissioning using the printed Matter QR payload or manual setup code,
  with the active Thread dataset supplied directly by the C6.
- Offline production PAA trust certificates, with attestation enabled.
- Durable CA/controller key, fabric/IPK, SDK ICD registrations and four device slots.
- A random persistent fabric IPK replacing the upstream example's public test IPK.
  The custom issuer sends this same key with every device's certificate chain.
- Automatic Descriptor endpoint discovery, plus a console override.
- Three switch-event subscriptions with retry after connection loss; wake a remote while
  restoring a subscription after the hub has restarted.
- Bulb brightness/colour attribute subscription reads actual supported features,
  brightness limits and physical white-temperature limits. This bulb reported
  ColourControl features/capabilities `0x1f`, brightness `1..254`, and white
  temperature `153..555` mired. Each BILRESA reported Switch features `0x1e`.
- Suppression of priming history and duplicate event numbers. The counter baseline
  restarts with each subscription, allowing a rebooted remote to work again.
- Latest requested On/Off state and absolute brightness/colour targets, with
  serialized commands and at most three attempts. New requests supersede old
  targets; acknowledgement of an old command cannot discard a newer target.
- Held-button dimming uses finite half-second absolute-level transitions, roughly
  13% per second. Release/disconnection cancels pending hold steps; a 15-second
  timeout limits a lost release. Dimming does not continuously write flash.
- A shared colour palette cycles four white tones and five colours, skipping
  unsupported features. Colour changes save the palette cursor for restart.
  All three remotes control the same cursor and actual bulb state.
- Status/heap diagnostics and removal of a reachable device's fabric before re-pairing.
- Two startup bulb flashes once all three remote subscriptions and the bulb
  state subscription are active. The
  acknowledged sequence is Off, On, Off, On, with at least one second between
  steps; it leaves the bulb on. It runs once per ESP boot, including charger
  power-up. A button press or manual lighting command skips or cancels it to give user
  control priority. It waits if a remote is unavailable, rather than claiming
  readiness early. Command failures use the normal bounded retry policy.

Subscriptions currently use a requested maximum interval of 300 seconds. Actual
negotiated sleepy-device behavior must be measured. Presses during commissioning,
disconnection, or subscription priming may be missed. The simple relay deliberately
does not blindly retry Toggle or an unbounded continuous brightness movement.

| Action on any BILRESA | Upper button | Lower button |
| --- | --- | --- |
| Single press | On | Off |
| Double press | Next colour/white preset | Previous colour/white preset |
| Hold | Brighter | Dimmer |

Release ends dimming. A single press is recognized after the remote's multi-press
window closes, so a double press does not briefly switch the bulb off. Colour
selection turns the bulb on. The palette is 2200 K, 2700 K, 4000 K, 6500 K,
red, green, blue, purple and pink. Colour commands use Hue/Saturation where
available, with an XY fallback; a white-only bulb cycles only white tones.
These gestures match the [BILRESA manual](https://www.ikea.com/qa/en/manuals/bilresa-remote-control-white-smart-dual-button__AA-2646178-1-100.pdf).

## Pinned build

Use ESP-IDF **v5.5.5** and ESP-Matter release/v1.5 commit
`ae9001236dddd3f5fd953bed1c4f483c1b9beba3`, whose connectedhomeip submodule is
`392b307067a10514615ea4fbd3edae5da5b25133`. These versions were checked against the
release README and source APIs. Do not silently substitute another SDK version.

On this PC, WSL 3.0.1 and Ubuntu 24.04 are installed and running. The full build
uses `/opt/c6-local-hub` in Ubuntu, running as root for tool installation. For a
fresh Windows setup, install Ubuntu after enabling WSL and restarting:

```powershell
wsl --install --distribution Ubuntu-24.04 --no-launch --web-download
wsl -d Ubuntu-24.04
```

Follow Ubuntu's first-run prompts. In its terminal, run the project's setup script
from its Windows location. Use quoted paths if the folder name contains spaces:

```bash
cd /mnt/c/path/to/c6-ikea-matter-hub
bash scripts/setup-wsl.sh
```

The script puts SDKs and the project on the Linux filesystem under
`~/c6-local-hub`, installs the pinned dependencies, builds, and exports the images
and exact flash map back into this project's `firmware` folder. Setup requires
Internet access to obtain dependencies and approved production attestation roots.
The resulting firmware uses neither Internet nor cloud services.

For later rebuilds, run `scripts/build-wsl.sh` inside Ubuntu with
`C6_HUB_BUILD_ROOT` set to your SDK workspace. Reproducible SDK corrections
are applied by `patch-sdk.py`: qualify the transport address type for Thread-only
compilation, handle missing fabric lookups during commissioning failure, and fill
the entire ICD symmetric-key buffer with random bytes. The Bluetooth patch
accepts an already-started MTU exchange and reports connection failures without
deinitializing the host. Operational DNS retries tolerate the delay between
joining Thread and publishing the device's SRP service; retries are bounded and
cancelled when the lookup is no longer needed. The OpenThread DNS response is
initialized and errors are reported without attempting an invalid address lookup.
The application also handles PASE failure and has a discovery deadline so initial
pairing can be retried. The SDK cancellation patch stops its BLE scan instead of
returning NotImplemented and leaving discovery active.

For a default non-root installation, source `~/c6-local-hub/esp-idf/export.sh` and
`~/c6-local-hub/esp-matter/export.sh`, synchronize edited sources into the Linux
project, and build there. Never commit or share flash backups or NVS images.

## Flashing from Windows

Only proceed after a successful full build. Close other serial monitors.
The board's native USB port is **COM8** on this PC; Device Manager can confirm it.
Bulk reads stalled on native USB during this session. Use the board's other
USB-C socket, labelled UART/USB-UART, for flashing; it appears as **COM9** here.
The backup helper limits reads to one packet in flight and verifies the stub's
checksum before allowing any write.
For new boards, follow [the quick start](QUICKSTART.md), which installs the
tested esptool version and takes a backup before a full first-install erase.
Use your actual UART COM number and Python environment:

```powershell
.\scripts\flash-windows.ps1 -Port COM9 -Python .\.venv\Scripts\python.exe -FirstInstall
```

For later firmware updates, omit both `-FirstInstall` and
`-InitializeMatterStorage` to retain all pairings.
The script verifies image hashes and takes a full 8 MB private flash backup before
writing. Updates preserve the existing Thread NVS at `0x9000` and Matter NVS at
`0x10000`. The legacy `-InitializeMatterStorage` switch only erases Matter NVS;
it was used to migrate the original diagnostic prototype and is not the normal
first-install procedure. Flashing is native Windows; USB forwarding into WSL
is unnecessary.

## Pairing and operation

After flashing, move the cable back to the original native USB socket.
Open COM8 at **115200 baud**, then reset the C6. Wait around 40 seconds for it
to become Thread leader. Run:

```text
matter esp hub status
```

Additional diagnostics/control commands, after the bulb state subscription is
ready, are `matter esp hub level 50` (brightness percent, 0 switches off),
`matter esp hub color next` and `matter esp hub color prev`. Status reports
the measured brightness range, RGB/white support, selected palette and hold owner.

Status should report a Thread leader/router and a running SRP server. If the
firmware fails initialization or reports low memory, stop and capture the log.
Factory-reset the IKEA bulb/remotes using their model's manual and put each one
into commissioning mode individually. Direct IKEA remote-to-bulb pairing must
be replaced by commissioning all devices onto this fabric. Use each device's own
printed setup code; the words in angle brackets below are placeholders:

For the BILRESA two-button remote, hold its system button inside the battery
compartment for 10 seconds until the red LED stops blinking. It then offers
Matter pairing for 15 minutes. A single system-button press reopens that window.
These are the [IKEA manual's instructions](https://www.ikea.com/ie/en/manuals/bilresa-remote-control-kit-dual-button-mixed-colours__AA-2664471-2-100.pdf).

```text
matter esp hub pair bulb <BULB-CODE>
matter esp hub on
matter esp hub off
matter esp hub pair door <DOOR-CODE>
matter esp hub pair desk <DESK-CODE>
matter esp hub pair bedside <BEDSIDE-CODE>
matter esp hub status
```

Wait for commissioning success and endpoint discovery before pairing the next
device. Keep a battery remote awake while commissioning or restoring a subscription.
Press its buttons and check that its log says `listening` before expecting control.
On this exact hardware, all three subscriptions returned automatically within
28 seconds after a hub reset. Allow approximately 45 seconds after power-up;
a remote asleep for longer may still need a button press to wake it.
Manual codes should be entered as uninterrupted digits; QR payloads begin `MT:`.
The console can echo codes while they are entered: keep commissioning logs private.

If discovery found multiple possible bulb endpoints, select the one printed with
server `0x6`. If physical remote buttons are reversed, select the endpoint order:

```text
matter esp hub map bulb 1
matter esp hub map door 2 1
```

Those endpoint numbers are examples only; use the actual descriptor output. For
a reachable device, `matter esp hub remove door` removes the fabric and its saved
slot only after remote acknowledgement. A device that has already been factory
reset may need a recovery workflow; do not erase the entire controller identity
to recover one remote.

For charger-startup diagnostics, `matter esp hub history` prints the saved
previous boot and current boot summaries. Up to ten timed snapshots are saved
in the first five minutes of each boot; this is a bounded startup trace, not
continuous flash logging. `host=0` means no native USB host was detected,
`role=4` means Thread leader, `SRP=1` means running, `paired=0xf` means all four
slots are saved and `listening=0xe` means all three remotes are subscribed.
Startup stages are 1=storage, 2=Matter stack, 3=Thread enabled, 4=controller ready,
5=control loop running, 6=console setup returned. Button and command counters
help distinguish a connection failure from a bulb-command failure.
The command counter includes On/Off, brightness and colour commands.
The control loop starts before the optional console task. That task waits for
an actual native USB host before initializing the console; stage 5 is therefore
normal on a charger once timed snapshots and network activity are advancing.
ESP log messages and the SDK's direct CHIP printf prefixes are suppressed when
no USB host is connected; operational traffic stays on Thread.

After all four are working, unplug the computer connection and power the C6 from
a USB supply through the **UART/USB-UART socket**. Keep using the native USB
socket for PC setup/diagnostics. On this board, charger startup through native
USB has been inconsistent; the UART power connection passed with the full
brightness/colour firmware and does not need a computer or serial connection.
Wait for the two bulb flashes to indicate startup readiness;
the board's red LED only indicates power. Normally this takes roughly 30–45 seconds,
but a sleeping or unavailable remote may delay the signal. Validate all three
remotes, then power-cycle the hub, wake the
remotes to restore subscriptions, and repeat. The computer is needed only for
setup and diagnostics.

## Required acceptance tests

1. Full build for C6 succeeds with the intended Kconfig options and flash layout.
2. Boot with BLE controller + Thread FTD + SRP server has adequate free/largest heap.
3. Retail KAJPLATS attestation succeeds; commissioning, DNS resolution, CASE and
   console On/Off work without an external border router.
4. Each BILRESA commissions and exposes compatible switch endpoints/events.
5. Three simultaneous subscriptions report button presses promptly.
6. Hub power loss preserves identity/dataset/pairings and restores operation.
7. Remote battery removal, loss of radio coverage, and repeated presses recover;
   old priming events never change the bulb state.
8. Entire system operates from USB power with the PC disconnected.

If the single-C6 combination cannot pass these tests, the smallest Espressif
reference fallback is an ESP32-S3 with PSRAM as controller/Thread host plus this
C6 as the 802.15.4 RCP. That is one additional board, not an always-on PC. Its
complete application would still need validation with the actual IKEA devices.

Sources: [Espressif build/Windows guidance](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/developing.html),
[pinned controller example](https://github.com/espressif/esp-matter/tree/ae9001236dddd3f5fd953bed1c4f483c1b9beba3/examples/controller),
[BILRESA descriptor survey](https://matter-survey.org/de/device/bilresa-dual-button-4476-32769),
[CSA production DCL](https://on.dcl.csa-iot.org).

