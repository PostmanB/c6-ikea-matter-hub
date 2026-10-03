#!/usr/bin/env bash
set -eo pipefail
project_source=$(cd "$(dirname "$0")/.." && pwd)
task_root="${C6_HUB_BUILD_ROOT:-$HOME/c6-local-hub}"
source "$task_root/esp-idf/export.sh"
source "$task_root/esp-matter/export.sh"
export ESP_HOMEKIT_PATH="$task_root/esp-homekit-sdk"
[[ $(git -C "$ESP_HOMEKIT_PATH" rev-parse HEAD) == 676fabac4a4a05184be020611cb069faa0016411 ]]
cp -rp "$project_source/main" "$project_source/scripts" "$task_root/project/"
cp -p "$project_source/sdkconfig.defaults" "$project_source/CMakeLists.txt" "$project_source/partitions.csv" "$task_root/project/"
cd "$task_root/project"
python scripts/patch-sdk.py "$task_root/esp-matter"
python scripts/patch-homekit.py "$ESP_HOMEKIT_PATH"
idf.py -DIDF_TARGET=esp32c6 -DSDKCONFIG="$task_root/project/sdkconfig.home" build
python scripts/export-build.py build "$project_source/firmware"
