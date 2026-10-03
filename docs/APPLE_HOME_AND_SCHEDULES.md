# Wi-Fi, daily schedules and Apple Home (experimental)

This branch adds optional Wi-Fi and Espressif's native HomeKit/HAP SDK alongside
the existing Matter-over-Thread commissioner/controller. Compilation and host
tests pass. Combined radio operation, iPhone pairing and power-cycle clock
recovery require physical validation before this becomes the default release.

## What runs where

The C6 retains its isolated Thread network and Matter fabric. The remotes and
bulb remain commissioned to it. Wi-Fi is used for NTP time synchronization and
local HomeKit connections. This is a HomeKit light accessory forwarding commands
to the existing bulb, rather than adding that Thread bulb directly to Apple Home.
It does not expose an Internet control service, use an external automation
server or start ESP Insights/cloud telemetry.

Use an iPhone on the same LAN for local Apple Home controls and Siri. A HomePod
or Apple TV is unnecessary for local HAP accessory control. Apple Home's own
time/location automations and away-from-home control need a home hub; this
project's daily schedule runs directly on the ESP instead. Siri's voice service
and Apple's account requirements are controlled by the iPhone, not this firmware.

The HomeKit SDK is intended for DIY development here. Apple Home presents an
Uncertified Accessory notice: choose Add Anyway for your own board.

## Configure over USB

Flash with `scripts/flash-windows.ps1` using its normal update mode. **Do not use
FirstInstall or erase_flash on an already paired hub.** The new `home_nvs`
partition occupies previously unused flash at 0x6b0000; Thread/Matter offsets,
credentials and records remain unchanged. A full backup precedes flashing.

Start `scripts/open-setup.ps1`, open the local USB setup page in Chrome or Edge,
and connect native USB. Under Clock and Apple Home:

1. Enter the 2.4 GHz Wi-Fi SSID/password and Save Wi-Fi. Wait for its save message.
2. Choose a unique eight-digit HomeKit PIN, note it privately, and enable HomeKit.
   Wait for “HomeKit verifier saved.” The firmware stores only the SRP salt and
   verifier, not the raw PIN. Re-entering a PIN on a configured board is refused.
3. Optionally set daily turn-on to 06:00 (or your preferred time). Leaving this
   field unsaved keeps the schedule disabled.
4. Restart the ESP. Wait for connected Wi-Fi, a synchronized clock, a live bulb,
   restored remote subscriptions and HomeKit running in diagnostics.
5. On the iPhone: Home > Add Accessory > More Options > C6 IKEA Light > Add
   Anyway. Enter the PIN. Assign a room/name and test On/Off, brightness and colour.
6. Test all three remotes again. Then power the ESP through UART/USB-UART and
   repeat after a true power cut with the PC disconnected.

The USB page keeps settings only in page memory and sends them over USB. It
does not upload credentials or use browser storage. Serial command echoes are
redacted in its displayed diagnostics. Do not share raw serial captures/backups:
those may contain secrets. HomeKit pairing keys and configuration are persistent
in `home_nvs`; SDK auto-erase-on-NVS-error recovery is disabled.

### If Wi-Fi does not connect

The setup page reports saved settings separately from active Wi-Fi and HomeKit.
"Saved" does not mean a connection succeeded. Network diagnostics include an
initialization step, ESP error and last disconnect reason without exposing the
password or PIN. Reason 201 means the network was not found: check the exact
SSID, 2.4 GHz availability and range. Reason 202 means authentication failed.
Re-enter Wi-Fi settings, save, then restart. Keep the existing HomeKit PIN;
Wi-Fi correction does not require a new identity or Matter commissioning.

## Clock and schedule behavior

NTP server: `pool.ntp.org`. Internet is used to synchronize the clock, including
after a power cut; thereafter the local clock runs while powered. Loss of Wi-Fi
after synchronization does not stop the schedule or physical remotes. HomeKit
requires the local Wi-Fi connection. A power cut with no subsequent successful
time sync suppresses the schedule until the clock is known.

Timezone: Europe/Bratislava using `CET-1CEST,M3.5.0/2,M10.5.0/3`, including DST.
The firmware defaults to a disabled daily schedule, suggested as 06:00 in the
page. Configuring it enables a daily **On** at the bulb's existing level/colour.
It does not replace this with Toggle or a new brightness preset.

A schedule may run up to 119 seconds late if startup, time sync or bulb recovery
finishes in that window. It never replays an occurrence hours later. The local
calendar day is saved before command dispatch; restarts, backward NTP corrections
and a repeated DST hour cannot repeat an occurrence. If storage fails the action
is suppressed. If power fails between saving the occurrence and sending it,
that occurrence is skipped rather than replayed. Matter On uses the existing
three-attempt acknowledgment/retry path. The next calendar day remains eligible.
If a selected time does not exist on the spring DST transition, it is skipped.

## Console fallback

The following commands use the existing `matter esp hub` prefix:

```text
matter esp hub clock
matter esp hub home
matter esp hub schedule 06:00
matter esp hub schedule off
matter esp hub restart
```

Wi-Fi provisioning uses `wifi SSID_UTF8_HEX PASSWORD_UTF8_HEX`; `-` means an open
network password. Hex avoids shell splitting of spaces and punctuation. It is
encoding, not encryption: enter credentials through the local USB page rather
than pasting them into public logs. `home EIGHT_DIGITS` creates the verifier
once. Wi-Fi/PIN changes require restart; schedule changes apply immediately.

## Architectural limits and validation

The pinned ESP-Matter Kconfig prevents `ESP_MATTER_COMMISSIONER_ENABLE` with
`ESP_MATTER_ENABLE_MATTER_SERVER`. Enabling a Matter bridge would require SDK
changes to its server/controller ownership. A separate HAP protocol avoids
that conflict; Matter's network commissioning remains Thread-only.

The C6 has a shared Wi-Fi/BLE/802.15.4 radio. ESP-IDF categorizes Wi-Fi STA with
a Thread router as supported with unstable performance (C1). Software radio
coexistence is enabled, Wi-Fi power saving is disabled, and buffers are bounded.
These settings do not establish reliable coexistence: test actual remotes,
bulb commands, HomeKit traffic, reconnection and power cycles before distributing
this firmware. If it fails, a separate Wi-Fi HomeKit processor would keep the
proven C6 Thread hub isolated from Wi-Fi radio traffic.

Pinned HomeKit SDK: `676fabac4a4a05184be020611cb069faa0016411`. Build setup fetches
it and applies fail-closed NVS and charger-safe logging patches.

Primary references:

- [Espressif HomeKit SDK and setup information](https://github.com/espressif/esp-homekit-sdk)
- [ESP-IDF C6 system time](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c6/api-reference/system/system_time.html)
- [ESP-IDF C6 radio coexistence](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c6/api-guides/coexist.html)
- [Apple home hubs and automations](https://support.apple.com/en-us/102557)
