"""Fail closed on HomeKit NVS errors instead of silently erasing pairings."""
from pathlib import Path
import sys

path = Path(sys.argv[1]) / "components/homekit/esp_hap_platform/src/hap_platform_keystore.c"
source = path.read_text()
# Literal strings avoid interpreting C braces as Python format placeholders.
for init in ["nvs_flash_secure_init_partition(part_name, cfg)", "nvs_flash_init_partition(part_name)"]:
    block = "        if (err == ESP_ERR_NVS_NO_FREE_PAGES) {\n            ESP_ERROR_CHECK(nvs_flash_erase_partition(part_name));\n            err = " + init + ";\n        }"
    replacement = "        /* Hub: propagate NVS errors; never erase an accessory identity. */"
    if block in source:
        source = source.replace(block, replacement)
    elif replacement not in source:
        raise SystemExit("Unexpected pinned HomeKit NVS implementation")
path.write_text(source)
print("Checked HomeKit no-auto-erase patch")

# Native USB must not block headless startup when no computer drains output.
# Route the SDK's direct printf diagnostics through the hub's guarded ESP log.
debug = Path(sys.argv[1]) / "components/homekit/esp_hap_core/src/priv_includes/esp_mfi_debug.h"
source = debug.read_text()
replacement = '#define ESP_MFI_DEBUG(l, fmt, ...) { ESP_LOGI("homekit", fmt, ##__VA_ARGS__); }\n'
legacy = '#define ESP_MFI_DEBUG(l, fmt, ...) ESP_LOGI("homekit", fmt, ##__VA_ARGS__)\n'
source = source.replace(legacy, replacement)
if replacement not in source:
    start = source.index("#define ESP_MFI_DEBUG(l, fmt, ...)")
    end = source.index("#define ESP_MFI_DEBUG_INTR", start)
    source = source[:start] + replacement + source[end:]
    source = source.replace('#include <stdio.h>', '#include <stdio.h>\n#include <esp_log.h>')
debug.write_text(source)
print("Checked HomeKit charger-safe logging patch")
