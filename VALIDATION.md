# Validation record — 2026-10-01

## Optional Wi-Fi/HomeKit branch — 2026-10-03

- ESP-IDF 5.5.5, original Matter/CHIP pins, ESP-HomeKit SDK
  `676fabac4a4a05184be020611cb069faa0016411`: combined build passed.
- Latest binary: 2,474,384 bytes, 61% of the existing 6 MB app partition free.
  Static memory report before the final 16-byte code adjustment: DIRAM 277,838
  used / 174,274 remaining; runtime heap with active Wi-Fi/HAP still unmeasured.
- Coexistence enabled; Matter Wi-Fi commissioning remains disabled; ESP Insights
  disabled. New HomeKit/config storage at 0x6b0000; old storage offsets unchanged.
- Host schedule tests passed: clock validity, disabled state, two-minute grace,
  reboot/day deduplication, clock rewind, repeated DST hour and CET/CEST offsets.
- Eight original protocol tests, six network/privacy tests, three firmware
  checksum/layout tests and browser JS syntax check passed.
- Local setup page loads, new forms render, and browser reports no JS errors.
  USB credential provisioning passed on the physical board; iPhone pairing
  remains unverified.
- Pre-update physical status: all three existing remotes subscribed, bulb state
  live, Thread role 3, SRP running, storage healthy, 197,248 bytes internal heap.
- Full 8 MB pre-update backup passed stub MD5 verification at 115200; a faster
  attempt was rejected on an incomplete packet. All four update images flashed
  successfully and passed esptool verification, without any credential erase.
- Updated physical hub booted: all three remote subscriptions recovered, live
  bulb state, Thread role 3/SRP running, healthy old/new storage, two-flash ready
  sequence acknowledged, 116,024 bytes free heap / 94,208 largest block before
  optional Wi-Fi/HAP activation. Headless UART-powered boot also saved a trace
  with all pairings restored and four startup command acknowledgments.
- User provisioned Wi-Fi and a HomeKit verifier through the local USB page,
  restarted and disconnected. Both settings persisted; the schedule remains
  disabled by choice. No credentials were entered in chat or public files.
- A diagnostic app-only update passed flash hash verification through native
  USB. Fresh private backups of home_nvs and Thread/Matter NVS each passed the
  stub checksum; all pairings recovered after the update.
- Initially Wi-Fi initialized with ESP_OK but association failed with reason
  201 and a scan returned zero networks. Added the explicit
  esp_coex_wifi_i154_enable call used by the pinned ESP-IDF Thread example;
  Matter's Thread-only platform did not call it. The original saved credentials
  then connected successfully, without any Wi-Fi configuration change.
- Final scanner/coexistence app-only update passed flash hash verification.
  Physical asynchronous scan completed in five seconds and found four access
  points. Wi-Fi connected, NTP synchronized to CEST, and HomeKit reported running.
  All three remote subscriptions, bulb state, readiness signal, Thread role 3
  and SRP recovered concurrently. Both storage regions healthy; schedule off.
  Heap: 45,884 bytes free / 25,600 largest block / 33,972 minimum observed.
- Setup-page discovery includes UTF-8 SSIDs, signal/security labels, strongest
  duplicate selection, manual hidden-name entry, timeouts and firmware feature
  gating. SSIDs are plain DOM text and redacted from displayed diagnostics.
  Host tests cover duplicates, malformed names, fresh scans and error recovery.
- Browser USB scan interaction, iPhone pairing/Siri, scheduled actions and
  unplugged combined-radio recovery remain pending physical user tests.

The hardware results below describe the previously tested offline release.

Verified on the physical C6 using the separate Thread diagnostic:
- 8 MB flash, C6 rev 0.2, native USB COM8.
- Native Thread FTD became leader, with local SRP server running.
- DNS-SD resolved a locally registered diagnostic service.
- Random active Thread dataset survived hardware reset unchanged.

Full hub project:
- Source implementation and pinned SDK APIs inspected locally.
- Build helper Python syntax and Windows flashing script syntax checked.
- Flash layout checked against the 8 MB device and existing diagnostic NVS.
- Full compilation PASSED after Windows restart and Ubuntu setup, using IDF5.5.5
  and the pinned ESP-Matter/CHIP commits with documented helper corrections.
- Verified generated configuration: C6, BLE central/commissioner, Thread FTD,
  Matter-over-Thread, SPIFFS attestation, custom issuer, Matter NVS partition.
- Latest application binary size: 0x1b3f70 (1,785,712 bytes); 72% of app partition free.
- Four flash images exported with SHA256 manifest; production PAA snapshot contains 72 roots.
- First USB backup read failed with corrupt data; slower retry stalled and was
  stopped before any firmware writes. User asked to reconnect USB.
- Full hub flashed through USB-UART COM9 after an 8 MB backup passed the stub's
  MD5 checksum. Every firmware image passed esptool's flash checksum verification.
- First boot asserted on a missing OpenThread platform config. Added explicit
  native radio/no host connection/NVS queue configuration before starting Matter,
  rebuilt, and flashed the application update without erasing storage.
- Updated hub booted successfully: Thread role 4 (leader), SRP state 1 (running).
- Matter registered its own operational service through its local SRP server.
- Internal heap free 202,440 bytes; largest block 176,128; minimum observed 192,900.
- Initial serial status console worked with all four device slots unpaired;
  later commissioning results below supersede that initial state.
- Real KAJPLATS BLE/PASE pairing, production certificate attestation, operational
  credentials installation, Thread dataset transfer and Thread join succeeded.
- Initial pairing exposed pinned SDK bugs: a normal NimBLE EALREADY from MTU
  negotiation was treated as fatal, and operational DNS did not retry after an
  initial NXDOMAIN before the bulb registered its SRP service. Added reproducible
  corrections and rebuilt/flashed successfully.
- First secure CASE attempt exposed a project credential issuer mismatch: the
  local controller used its persisted random IPK but the upstream issuer returned
  the public test IPK to the bulb. Corrected the custom issuer completion callback
  to send the same persisted key to every commissioned node.
- Real KAJPLATS full commissioning PASSED, including production attestation,
  SRP registration, DNS resolution, CASE and CommissioningComplete. Bulb record
  persisted, and Descriptor discovery selected its sole OnOff endpoint 1.
- Bulb acknowledged Off and On over Thread. After pairing: heap free 200,948,
  largest block 180,224, minimum observed 180,828; storage healthy.
- User observed the bulb turn off and on. A subsequent ESP hardware reset kept
  the bulb's saved pairing and endpoint; a new CASE session succeeded and the
  bulb acknowledged Off and On again, without recommissioning.
- First door-remote search timed out while the remote was unavailable. It
  exposed an upstream missing discovery callback and unimplemented BLE
  cancellation. Added an application discovery deadline and a pinned SDK
  cancellation implementation; rebuilt and flashed without erasing pairings.
- Real door BILRESA full commissioning PASSED, including persisted ICD
  registration, Thread join, operational DNS, CASE and CommissioningComplete.
  Actual descriptor has Generic Switch servers on endpoints 1 and 2, and no
  Binding/OnOff client path was used. Mapping 1=On, 2=Off saved automatically.
  Its event subscription was established successfully. Six physical presses
  alternated Off/On and all six commands were acknowledged. The user confirmed
  both remote buttons control the bulb correctly.
- Real desk BILRESA full commissioning, ICD registration and event subscription
  PASSED. It also exposes Generic Switch EP1/EP2 and was mapped automatically.
  Door subscription remained active while the desk remote was commissioned.
- User confirmed door and desk both control the bulb. Additional desk and door
  events during/after bedside pairing produced acknowledged bulb commands.
- Real bedside BILRESA full commissioning, persisted ICD registration, automatic
  Generic Switch EP1/EP2 mapping and event subscription PASSED. All four slots
  are now paired and all three remote subscriptions are active.
- Joint live control test PASSED: button events from door, desk and bedside
  were captured and the corresponding Off/On commands were acknowledged.
  The user explicitly confirmed that all three remotes work together.
- All-three status: heap free 197,300, largest block 172,032, minimum 175,752;
  all records paired, all subscriptions listening, storage healthy, Thread leader
  and SRP running. No secondary processor, Wi-Fi or upstream border router used.
- All-three ESP hardware reset test PASSED: all four saved records and endpoint
  mappings restored, all three subscriptions re-established automatically at
  approximately 22/26/28 seconds, without another commissioning operation.
  Status at 41 seconds: all three listening, storage healthy, heap free 198,340,
  largest block 172,032, minimum 189,820. Charger results are recorded below.

- ESP reset test passed: controller restored the same operational service ID
  [device-specific service identifier withheld] and resumed Thread leader/SRP operation.

- First standalone USB-charger test FAILED: user reported no remote control with
  the red power LED on after moving the C6 approximately 2 metres. Reconnecting
  to the PC restored normal leader/SRP state and all three live subscriptions.
  The cause is not yet established; restart-on-PC results do not prove charger
  operation. Added saved startup diagnostics, host-aware logging and independent
  console initialization. Rebuild and application-only flash PASSED.
- Diagnostic firmware PC boot PASSED: all three subscriptions recovered at
  approximately 20/26/27 seconds, records/mappings retained, startup stage 6,
  Thread leader and SRP running. Startup trace persisted and was read successfully.
- Diagnostic charger test FAILED again. Saved previous-boot trace proved the
  application reached control-loop creation (stage 5, host 0, power-on reset),
  but remained at the initial 1-second snapshot: no 5-second or later sample,
  and console initialization did not return. The zero network fields were never
  sampled and do not themselves prove a Thread attach failure.
- Added USB-host gating before optional console initialization and a pinned
  SDK LogV guard covering its direct printf prefixes, which bypass the ESP log
  callback. Rebuild and application-only flash PASSED.
- Updated standalone charger test PASSED: user confirmed remote control worked.
  Saved boot 4 trace: power-on reset, stage 5, uptime 240 seconds, host 0,
  Thread leader, SRP running, paired mask 0xf, subscriptions mask 0xe,
  button counts door/desk/bedside 4/6/5, 15 acknowledged commands, zero failures.
  Stage 5 is normal here because the optional console waits for a USB host.
  Reconnecting to the PC restored all records and subscriptions without re-pairing.
- Added one readiness indication per ESP startup: once all three subscriptions
  are established, send acknowledged Off/On/Off/On with visible pauses. Real
  button or console control takes priority and skips/cancels the indication.
  It leaves the bulb on and uses normal bounded command retries.
- Final readiness build and application-only flash PASSED, checksum verified.
  PC startup restored bedside/door/desk subscriptions at roughly 20/20/23 seconds.
  Off/On/Off/On acknowledged at 24.177/25.377/27.387/29.377 seconds, followed
  by READY. Both buttons on each of all three remotes then produced acknowledged
  Off/On commands (six physical presses). Status: all three subscriptions live,
  storage healthy, Thread leader, SRP running, heap free 198,156 bytes, largest
  block 180,224, minimum 188,120. All pairings and mappings retained.

- Brightness/colour extension full build and application-only flash PASSED.
  Actual KAJPLATS ColourControl FeatureMap and ColorCapabilities both read 0x1f:
  Hue/Saturation, enhanced hue, colour loop, XY and colour temperature supported.
  LevelControl FeatureMap=3, MinLevel=1, MaxLevel=254; physical white-temperature
  limits 153..555 mired. Actual BILRESA Switch FeatureMap=0x1e on both endpoints
  of all three remotes: momentary press, release, long press and multi-press.
- Physical extension test: MultiPressComplete count=2 events produced acknowledged
  palette commands in both directions, including all four white temperatures and
  red/green/blue/purple/pink. Desk LongPress/LongRelease events produced finite
  dimming targets 190/158/126 and stopped further targets on release; brighten
  LongPress also captured. Bedside double-press colour events captured. The user
  reported that all controls appear to work. The serial capture stopped partway
  due to a Windows file-sharing collision during a diagnostic read, without
  affecting firmware operation; the helper now retries that diagnostic save.
- Saved test boot trace: all three remote button counts 5/32/10, 58 acknowledged
  commands, zero failures, all three subscriptions active, Thread leader/SRP.
- Final cadence adjustment rebuilt/flashed, checksum verified. ESP restart
  restored brightness state, persisted palette cursor (pink), all four saved
  device records and all three remote subscriptions; no recommissioning.
  Bulb state subscribed at ~18 seconds; remotes ~29/34/38 seconds. Startup ready
  signal completed with four ACKs and no failures. At 74 seconds: state subscribed,
  level190, all three remotes live, storage healthy, heap free197332, largest176128,
  minimum186884. Saved 60-second trace: paired0xf/listening0xe, ack4/fail0.

- Final-build charger readiness check FAILED: user reported no visible flashes,
  then confirmed no remote response after roughly two minutes. On PC replug,
  the saved previous startup was still PC boot9, stage6/host1/uptime120, and
  current PC boot10 completed subscriptions and four startup ACKs successfully.
  The charger run left no new storage-ready trace, unlike the earlier failure
  that reached stage5. This points to a stop before the first application trace;
  the exact cause is not yet established. Next test uses the charger's cable
  in the other UART/USB-UART socket to isolate the native USB power/boot path. Earlier
  host-free On/Off success does not establish this extension's charger result.
- Final-build charger test through the other UART/USB-UART socket PASSED:
  after moving the same charger cable to that socket, the user reported all
  functions work. No firmware changes were made between the failed native-socket
  charger boot and this successful test. This verifies local runtime operation
  with the full extension; it does not identify the native-socket failure's cause.

- Final standalone power-cycle check through UART/USB-UART PASSED: user was
  asked to disconnect charger power for five seconds, reconnect the same socket,
  wait without pressing buttons, and verify two visible readiness flashes plus
  control from all three remotes. The user confirmed yes. This completes the
  requested startup indication and local On/Off/brightness/colour workflow.
  Use UART/USB-UART for standalone power; native-socket charger startup remains
  an unresolved limitation. No extra runtime computer, hub or ESP is required.

Requested workflow is complete. Longer-duration operation, remote battery loss
and radio-range recovery remain unverified.
Retain the private original and pre-hub flash backups.


## USB setup page and public release (2026-10-01)

The original proven application image is unchanged. A USB browser setup page,
new-board browser installer, portable Windows flashing instructions and offline
pairing-page option were added for distribution.

- Eight protocol regression checks passed: restored status, first-device order,
  concurrent-pairing prevention, unhealthy storage/detached Thread, incomplete
  endpoints, lost bulb state, failed pairing, numeric-code validation and redaction.
- Three release checks passed: individual image hashes, merged app/partition/PAA
  identity, erased padding across both credential NVS regions, merged checksum
  and C6-only browser manifest.
- The parser also consumed the saved successful physical-board status capture
  and recognized all four devices as operational with RGB/white capabilities.
- Desktop browser inspection found no JavaScript errors on setup or install
  pages. The installation button stayed disabled until erase consent was checked;
  reloading cleared consent again. No firmware was installed during UI checks.
- Browser first-install and a complete fresh-device commissioning run through
  the wizard remain unverified on a second board. Physical commissioning and
  controls were previously verified through the existing firmware console.
- The published image was built from generic app/PAA images, not a board backup.
  Both Thread and Matter NVS ranges in the merged image are all 0xff. No owner's
  setup codes, paired identity, live startup captures or private flash backups
  are included in the public release.
