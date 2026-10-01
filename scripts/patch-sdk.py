"""Minimal fixes to the pinned SDK's commissioner helper for Thread-only use."""
import sys
from pathlib import Path

root = Path(sys.argv[1]) / "components/esp_matter_controller/commands"
changes = {
    "esp_matter_controller_pairing_command.cpp": [
        ("PeerAddress peerAddress = PeerAddress::UDP(",
         "chip::Transport::PeerAddress peerAddress = chip::Transport::PeerAddress::UDP("),
        ("fabric->GetFabricIndex()", "(fabric ? fabric->GetFabricIndex() : controller_instance.get_fabric_index())"),
    ],
    "esp_matter_controller_pairing_command.h": [
        ("sizeof(m_icd_symmetric_key)", "sizeof(m_icd_symmetric_key_buf)"),
    ],
}
for filename, replacements in changes.items():
    path = root / filename
    text = path.read_text()
    original = text
    for old, new in replacements:
        # Idempotent and fail closed if the expected source has changed.
        if new in text:
            continue
        if old not in text:
            raise SystemExit(f"Unexpected SDK source: {filename}: {old}")
        text = text.replace(old, new)
    if text != original:
        path.write_text(text)
    print("Checked SDK patch:", filename)

# A failed GAP connection is a recoverable pairing result. The SDK otherwise
# tries MTU exchange on its invalid handle, disables/deinitializes NimBLE, and
# subsequently crashes when the scan timeout accesses the deinitialized host.
ble_path = Path(sys.argv[1]) / "connectedhomeip/connectedhomeip/src/platform/ESP32/nimble/BLEManagerImpl.cpp"
text = ble_path.read_text()
old = '''    rc = ble_gattc_exchange_mtu(gapEvent->connect.conn_handle, NULL, NULL);
    if (rc != 0)
    {
        return CHIP_ERROR_INTERNAL;
    }

    return HandleGAPCentralConnect(gapEvent);'''
new = '''    ChipLogProgress(DeviceLayer, "Hub BLE connect status=%d handle=%u", gapEvent->connect.status, gapEvent->connect.conn_handle);
    if (gapEvent->connect.status != 0)
    {
        HandleGAPConnectionFailed(gapEvent, CHIP_ERROR_INTERNAL);
        return CHIP_NO_ERROR;
    }
    rc = ble_gattc_exchange_mtu(gapEvent->connect.conn_handle, NULL, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY)
    {
        ChipLogError(DeviceLayer, "Hub MTU exchange failed rc=%d", rc);
        HandleGAPConnectionFailed(gapEvent, CHIP_ERROR_INTERNAL);
        ble_gap_terminate(gapEvent->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return CHIP_NO_ERROR;
    }

    HandleGAPCentralConnect(gapEvent); // reports recoverable failures itself
    return CHIP_NO_ERROR;'''
if new not in text:
    previous = new.replace("if (rc != 0 && rc != BLE_HS_EALREADY)", "if (rc != 0)")
    if previous in text:
        ble_path.write_text(text.replace(previous, new, 1))
    elif old in text:
        ble_path.write_text(text.replace(old, new, 1))
    else:
        raise SystemExit("Unexpected pinned BLE manager source")
print("Checked SDK patch: recoverable GAP connection failures")

text = ble_path.read_text()
old = '''CHIP_ERROR BLEManagerImpl::CancelConnection()
{
    return CHIP_ERROR_NOT_IMPLEMENTED;
}'''
new = '''CHIP_ERROR BLEManagerImpl::CancelConnection()
{
    SystemLayer().CancelTimer(HandleConnectTimeout, nullptr);
    const auto state = mBLEScanConfig.mBleScanState;
    mBLEScanConfig.mBleScanState = BleScanState::kNotScanning;
    if (state == BleScanState::kScanForDiscriminator || state == BleScanState::kScanForAddress)
        return mDeviceScanner.StopScan();
    if (state == BleScanState::kConnecting)
    {
        const int rc = ble_gap_conn_cancel();
        if (rc != 0 && rc != BLE_HS_EALREADY) return MapBLEError(rc);
    }
    return CHIP_NO_ERROR;
}'''
if new not in text:
    if old not in text:
        raise SystemExit("Unexpected pinned BLE cancellation source")
    ble_path.write_text(text.replace(old, new, 1))
print("Checked SDK patch: stop BLE discovery on cancellation")

chip_root = Path(sys.argv[1]) / "connectedhomeip/connectedhomeip/src"
logging_path = chip_root / "platform/ESP32/Logging.cpp"
text = logging_path.read_text()
original = text
if '#include "driver/usb_serial_jtag.h"' not in text:
    marker = '#include "esp_log.h"'
    if marker not in text:
        raise SystemExit("Unexpected pinned logging includes")
    text = text.replace(marker, marker + '\n#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG\n#include "driver/usb_serial_jtag.h"\n#endif', 1)
marker = 'void LogV(const char * module, uint8_t category, const char * msg, va_list v)\n{'
guard = '\n#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG\n    // Direct printf prefixes also need the headless guard.\n    if (!usb_serial_jtag_is_connected()) return;\n#endif'
if marker + guard not in text:
    if marker not in text:
        raise SystemExit("Unexpected pinned logging function")
    text = text.replace(marker, marker + guard, 1)
if text != original:
    logging_path.write_text(text)
print("Checked SDK patch: suppress direct USB logs without a host")

dns_path = chip_root / "platform/OpenThread/GenericThreadStackManagerImpl_OpenThread.hpp"
text = dns_path.read_text()
original = text
text = text.replace("otDnsServiceInfo serviceInfo{};", "otDnsServiceInfo serviceInfo;")
text = text.replace("otDnsServiceInfo serviceInfo;\n", "otDnsServiceInfo serviceInfo;\n    memset(&serviceInfo, 0, sizeof(serviceInfo));\n") if "memset(&serviceInfo, 0, sizeof(serviceInfo));" not in text else text
text = text.replace("if (otIp6IsAddressUnspecified(&serviceInfo.mHostAddress))",
                    "if (error == CHIP_NO_ERROR && otIp6IsAddressUnspecified(&serviceInfo.mHostAddress))")
if text != original:
    dns_path.write_text(text)

dns_path = chip_root / "lib/dnssd/Discovery_ImplPlatform.cpp"
text = dns_path.read_text()
original = text
if "struct HubDnsQuery" not in text:
    marker = "namespace {\n"
    if marker not in text:
        raise SystemExit("Unexpected DNS implementation namespace")
    text = text.replace(marker, marker + "\n" + Path(__file__).with_name("dns-retry.inc").read_text() + "\n", 1)
text = text.replace("return ChipDnssdResolve(&service, Inet::InterfaceId::Null(), HandleNodeIdResolve, this);",
                    "return HubDnsStart(peerId, service, HandleNodeIdResolve, this);")
marker = "void DiscoveryImplPlatform::NodeIdResolutionNoLongerNeeded(const PeerId & peerId)\n{"
if marker + "\n    HubDnsCancel(peerId);" not in text:
    if marker not in text:
        raise SystemExit("Unexpected DNS cancellation method")
    text = text.replace(marker, marker + "\n    HubDnsCancel(peerId);", 1)
old = "impl->mOperationalDelegate->OnOperationalNodeResolutionFailed(PeerId(), error);"
new = "PeerId failedPeer;\n        if (result) ExtractIdFromInstanceName(result->mName, &failedPeer);\n        impl->mOperationalDelegate->OnOperationalNodeResolutionFailed(failedPeer, error);"
text = text.replace(old, new)
if text != original:
    dns_path.write_text(text)
print("Checked SDK patch: bounded operational DNS retries and initialized response")
