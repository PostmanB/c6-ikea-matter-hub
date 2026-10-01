#!/usr/bin/env bash
set -eo pipefail
project_source=$(cd "$(dirname "$0")/.." && pwd)
task_root="${C6_HUB_BUILD_ROOT:-$HOME/c6-local-hub}"
source "$task_root/esp-idf/export.sh"
source "$task_root/esp-matter/export.sh"
cp -rp "$project_source/main" "$project_source/scripts" "$task_root/project/"
cp -p "$project_source/sdkconfig.defaults" "$project_source/CMakeLists.txt" "$project_source/partitions.csv" "$task_root/project/"
cd "$task_root/project"
python scripts/patch-sdk.py "$task_root/esp-matter"
idf.py -DIDF_TARGET=esp32c6 build
python scripts/export-build.py build "$project_source/firmware"
