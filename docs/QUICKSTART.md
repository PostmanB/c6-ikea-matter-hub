# Set up a hub for a friend

You need an **ESP32-C6-DevKitC-1 with 8 MB flash**, a USB data cable, a USB charger,
one IKEA KAJPLATS E27 1055 lm RGB+white Matter-over-Thread bulb and three IKEA
BILRESA **two-button** Matter-over-Thread remotes. Keep their printed Matter
numbers. Other board sizes and IKEA models have not been tested.

## Install once on a new ESP

Open [the browser installer](https://postmanb.github.io/c6-ikea-matter-hub/setup/install.html)
in desktop Chrome or Edge. Connect the board's **UART / USB-UART** socket to the
PC. Read the erase notice, confirm that this is the new 8 MB C6, and install.
Installation erases all previous firmware and pairings on that board.

If no port appears, use a data cable and install the official
[Silicon Labs CP210x Windows driver](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers).
If it cannot connect, hold BOOT, tap RESET, release BOOT, and retry.
After installation, close the installer and move the cable to **native USB**.
Press RESET if the board remains in download mode.

If the ESP is already running this hub, skip installation. Its saved pairings
are readable through setup; you do not need to erase or pair those devices again.

## Pair and test

1. Open [USB device setup](https://postmanb.github.io/c6-ikea-matter-hub/setup/)
   in desktop Chrome or Edge. Connect the ESP's native USB socket and choose its
   Espressif USB serial port when the browser asks. Close other serial monitors.
2. Keep all devices close to the ESP. Reset the bulb using six quick power-off/on
   cycles, ending on; it should blink. Enter **its** printed Matter number and
   click **Pair bulb**. Wait for its pairing to finish.
3. For each remote, hold the small system button inside its battery compartment
   for 10 seconds, until the red LED stops blinking. Enter its printed Matter
   number into the matching door, desk or bedside card and pair one at a time.
   Keep a remote awake with occasional main-button presses if pairing stalls.
4. Test both buttons, hold/release and double-press on **each** remote. Also check
   that previously paired remotes still work. The checkboxes record your tests;
   they do not automatically measure physical light behaviour.

| Gesture | Upper button | Lower button |
| --- | --- | --- |
| Tap | On | Off |
| Hold, then release | Brighter | Dimmer |
| Double-press | Next colour / white tone | Previous colour / white tone |

The page reads status from the firmware and sends commands over USB. Matter
numbers are kept only in page memory, hidden in page diagnostics, and cleared
after sending/disconnecting. This setup page has no analytics, cloud API or
browser storage. The separate installer loads ESP Web Tools from unpkg.com;
installing from the hosted page requires Internet access to download the files.

## Move to charger power

Disconnect the page and unplug the board from the PC. Use the board's
**UART / USB-UART socket** for charger power. Native-socket charger startup was
intermittent on the tested board; UART-socket operation passed a power-cycle
test with all functions. See [validation](../VALIDATION.md).

Keep the bulb powered. Put the remotes in their final places, with the ESP in
Thread radio range. Allow up to 90 seconds for reconnection. Two bulb flashes
mean all three remote subscriptions and the bulb connection are ready. A remote
press during startup cancels the flashes so your requested action takes priority.
Wake each remote with a button press if necessary, and repeat the control tests.

The ESP saves its own randomly generated fabric identity, Thread network and
pairings. After setup, the ESP and devices work without a PC, router, Wi-Fi,
Internet, Home Assistant or IKEA hub. Each friend's board creates **its own**
identity; no owner's network credentials are included in the downloadable image.

## Console fallback on Windows

Open native USB in a serial terminal at **115200 baud**. Use Windows Device
Manager to find its COM number; it differs between PCs. Reset the board after
connecting if the console has not started. Close the browser connection first.

Replace each placeholder below with the matching device's number, after resetting
that device. Wait for commissioning and endpoint discovery to finish before the
next command. Do not type the angle brackets.

```text
matter esp hub status
matter esp hub pair bulb <BULB_MATTER_NUMBER>
matter esp hub pair door <DOOR_MATTER_NUMBER>
matter esp hub pair desk <DESK_MATTER_NUMBER>
matter esp hub pair bedside <BEDSIDE_MATTER_NUMBER>
matter esp hub on
matter esp hub off
matter esp hub level 50
matter esp hub color next
matter esp hub status
```

A healthy status shows `storage=ok`, `pairing=idle`, Thread `role=4` (leader)
or another attached role (`2` or `3`), `SRP=1`, paired endpoints on all four slots,
all three remotes `listening=1`, and bulb `State subscribed=1`. `history` prints
the saved startup trace. The raw console may echo Matter numbers; do not share
unredacted console captures or flash backups.

Removing a device is an advanced console action: `matter esp hub remove SLOT`.
Wait for successful removal before resetting/re-pairing it. Endpoint overrides
and recovery notes are in [the development guide](DEVELOPMENT.md).

## Flash using Python instead of the browser

Download and extract the repository ZIP. Install Python 3 for Windows, then open
PowerShell in the extracted folder:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install esptool==4.10.0
powershell -ExecutionPolicy Bypass -File .\scripts\flash-windows.ps1 -Port COM9 -Python .\.venv\Scripts\python.exe -FirstInstall
```

Replace `COM9` with your board's **UART/USB-UART** COM number. `-FirstInstall`
explicitly erases old firmware and pairings, after taking a private full backup.
For an update to an existing hub, **omit `-FirstInstall` and
`-InitializeMatterStorage`** to preserve identity and pairings. The script verifies
image checksums before writing. If the backup fails, it cancels the flash.

## Use the setup page offline

Download the repository ZIP while online and extract it. With Python 3 installed:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\open-setup.ps1
```

Open `http://localhost:8765/setup/` in desktop Chrome or Edge. Keep that PowerShell
window open during setup. The pairing wizard uses only local files and USB. The
browser installer uses an external module; for fully offline first installation,
download Python/esptool ahead of time and use the flashing script instead.

## If something stops working

Check bulb power, remote batteries, charger/cable and the UART power socket first.
Wait for startup, then wake remotes. If the issue remains, connect native USB to a
PC, open setup diagnostics or run `status` and `history`. Preserve the flash
backup and saved identity. Erasing or reinstalling the board requires resetting
and commissioning its devices again; do not use the installer as a routine fix.
