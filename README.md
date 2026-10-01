# C6 IKEA Matter Hub

One ESP32-C6. One KAJPLATS bulb. Three BILRESA remotes. Local On/Off, brightness
and colour control, with no always-on computer or smart-home hub.

**Working experimental prototype:** tested on an ESP32-C6-DevKitC-1 with 8 MB
flash, a KAJPLATS E27 1055 lm RGB+white bulb and three two-button BILRESAs. All
three remotes worked together after a charger power cycle. Use the board's
**UART / USB-UART socket for charger power**; native-socket charger startup was
intermittent. Long-term reliability and other models are not verified.

## Set one up

- **[USB setup wizard](https://postmanb.github.io/c6-ikea-matter-hub/setup/)** —
  pair the bulb and remotes, see device status, and test the light.
- **[First-install page](https://postmanb.github.io/c6-ikea-matter-hub/setup/install.html)** —
  install on a new 8 MB ESP32-C6 board. This erases that board's previous data.
- **[Windows quick start and console fallback](docs/QUICKSTART.md)** —
  hardware, pairing, charger power, offline setup and troubleshooting.

Use desktop Chrome or Edge with a USB data cable. First installation uses
UART/USB-UART; device setup uses native USB. Pair devices one at a time, then
move the ESP to charger power and place the remotes where they belong. Pairings
survive power loss. Do not reinstall an already paired hub just to reconnect it.

| Remote gesture | Upper button | Lower button |
| --- | --- | --- |
| Tap | On | Off |
| Hold, then release | Brighten | Dim |
| Double-press | Next colour / white tone | Previous colour / white tone |

The palette includes four white temperatures and red, green, blue, purple and
pink. The firmware reads the bulb's actual supported features and limits.
Two bulb flashes indicate startup readiness; a remote press during startup
cancels the flashes so the requested control takes priority.

## How a single C6 works

The ESP creates an isolated Thread network, runs its own SRP/DNS-SD services and
acts as the Matter commissioner/controller. Devices join its own Matter fabric.
A border router is unnecessary for traffic that stays inside this Thread network.
Wi-Fi is disabled. USB is needed for setup and diagnostics, not daily operation.

BILRESA exposes Generic Switch events. The observed two-button descriptor does
not provide the Binding/OnOff-client combination needed for direct bulb commands.
The ESP subscribes to all three remotes and relays acknowledged, idempotent
commands to the bulb. This preserves all three remotes on one fabric instead
of replacing the bulb's IKEA direct pairing each time.

This combination requires SDK patches; it is not an unmodified Espressif
single-C6 controller example. See the architectural investigation, pinned
versions, limitations and source links in [the development guide](docs/DEVELOPMENT.md).

## What is included

- Complete ESP-IDF/ESP-Matter source and reproducible SDK patches.
- Tested generic firmware, individual flash images, SHA-256 manifest and merged
  browser-install image. These contain **no commissioned device identity,
  Thread dataset, owner's Matter numbers or flash backups**.
- Local USB browser wizard with pairing status, On/Off, brightness/colour tests,
  diagnostics and console fallback. The pairing page has no analytics, remote
  API calls or browser storage. The separate installer loads pinned
  [ESP Web Tools](https://esphome.github.io/esp-web-tools/) from unpkg.com.
- Persistent random fabric identity/IPK, Thread dataset, endpoints, pairings and
  ICD registrations; three remote subscriptions reconnect after restart.
- Offline production PAA roots and enabled device attestation.
- Charger-safe console/log guards, bounded startup traces, duplicate-event
  suppression and finite held-button dimming with a 15-second safety limit.

Each new board creates its own random credentials. Runtime stays local;
Internet is used only to download the project or browser-installer dependencies.
The pairing wizard also works from localhost after downloading the repository.

## Build or contribute

The tested SDKs are **ESP-IDF v5.5.5** and ESP-Matter release/v1.5 at
`ae9001236dddd3f5fd953bed1c4f483c1b9beba3`, with connectedhomeip at
`392b307067a10514615ea4fbd3edae5da5b25133`. On Windows, use WSL Ubuntu 24.04;
[the development guide](docs/DEVELOPMENT.md) includes build and flashing steps.
Prebuilt firmware lets friends skip installing the SDKs.

Run the setup-protocol regression tests with Node.js:

```text
node --test tests/protocol.test.js tests/release.test.js
```

See [VALIDATION.md](VALIDATION.md) for measured build sizes, hardware results,
charger-power limitations and the separate validation status of the setup UI.
The USB wizard and browser installer still need an end-to-end run on a fresh
friend's board; their UI/protocol checks do not establish that hardware result.

Project source is licensed under [Apache-2.0](LICENSE). Third-party SDK code and
public attestation certificates retain their respective terms; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
