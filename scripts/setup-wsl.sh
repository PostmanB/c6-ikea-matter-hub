#!/usr/bin/env bash
set -eo pipefail
# Run inside Ubuntu after Windows has finished enabling WSL.
project_source=$(cd "$(dirname "$0")/.." && pwd)
task_root="${C6_HUB_BUILD_ROOT:-$HOME/c6-local-hub}"
mkdir -p "$task_root"
sudo apt-get update
sudo apt-get install -y git gcc g++ pkg-config libssl-dev libdbus-1-dev libglib2.0-dev \
    ninja-build python3 python3-venv python3-pip cmake ccache unzip wget flex bison gperf libusb-1.0-0
if [[ ! -d "$task_root/esp-idf/.git" ]]; then
    git clone --branch v5.5.5 --depth 1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git "$task_root/esp-idf"
fi
[[ $(git -C "$task_root/esp-idf" describe --tags --exact-match) == v5.5.5 ]]
cd "$task_root/esp-idf"
./install.sh esp32c6
source ./export.sh
if [[ ! -d "$task_root/esp-matter/.git" ]]; then
    git clone --branch release/v1.5 https://github.com/espressif/esp-matter.git "$task_root/esp-matter"
fi
cd "$task_root/esp-matter"
git checkout ae9001236dddd3f5fd953bed1c4f483c1b9beba3
git submodule update --init --depth 1
cd connectedhomeip/connectedhomeip
[[ $(git rev-parse HEAD) == 392b307067a10514615ea4fbd3edae5da5b25133 ]]
python3 scripts/checkout_submodules.py --platform esp32 linux --shallow
cd ../..
./install.sh --no-host-tool
source ./export.sh
mkdir -p "$task_root/project"
if [[ ! -d "$task_root/esp-homekit-sdk/.git" ]]; then
    git clone --recursive https://github.com/espressif/esp-homekit-sdk.git "$task_root/esp-homekit-sdk"
fi
git -C "$task_root/esp-homekit-sdk" checkout 676fabac4a4a05184be020611cb069faa0016411
export ESP_HOMEKIT_PATH="$task_root/esp-homekit-sdk"
# Copy sources to the Linux filesystem: Windows checkout paths are unsuitable
# for the CHIP build scripts. Never overwrite a previous build directory.
cp -r "$project_source/main" "$project_source/scripts" "$task_root/project/"
cp "$project_source/CMakeLists.txt" "$project_source/sdkconfig.defaults" "$project_source/partitions.csv" "$task_root/project/"
if [[ -f "$project_source/paa_cert/manifest.json" ]]; then
    cp -r "$project_source/paa_cert" "$task_root/project/"
elif [[ ! -f "$task_root/project/paa_cert/manifest.json" ]]; then
    python "$task_root/project/scripts/prepare-paa.py" --output "$task_root/project/paa_cert"
fi
cd "$task_root/project"
python scripts/patch-sdk.py "$task_root/esp-matter"
python scripts/patch-homekit.py "$ESP_HOMEKIT_PATH"
idf.py -DIDF_TARGET=esp32c6 -DSDKCONFIG="$task_root/project/sdkconfig.home" build
python scripts/export-build.py build "$project_source/firmware"
echo "Compiled firmware exported to the Windows project. Use flash-windows.ps1 there."
