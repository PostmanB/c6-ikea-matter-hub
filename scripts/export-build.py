"""Export an IDF build and its exact flash address map for Windows esptool."""
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

build, destination = map(Path, sys.argv[1:])
data = json.loads((build / "flasher_args.json").read_text())
destination.mkdir(parents=True, exist_ok=True)
manifest = {"flash_settings": data["flash_settings"], "files": []}
for index, (address, filename) in enumerate(data["flash_files"].items()):
    source = build / filename
    name = f"{index}-{source.name}"
    shutil.copyfile(source, destination / name)
    manifest["files"].append({"address": address, "file": name, "sha256": hashlib.sha256(source.read_bytes()).hexdigest()})
(destination / "flash-manifest.json").write_text(json.dumps(manifest, indent=2))
settings = manifest["flash_settings"]
merged = destination / "install-merged.bin"
merge_args = [sys.executable, "-m", "esptool", "--chip", "esp32c6", "merge_bin",
              "-o", str(merged), "--flash_mode", settings["flash_mode"],
              "--flash_freq", settings["flash_freq"], "--flash_size", "8MB"]
for entry in manifest["files"]:
    merge_args += [entry["address"], str(destination / entry["file"])]
subprocess.run(merge_args, check=True)
(destination / "install-sha256.txt").write_text(
    hashlib.sha256(merged.read_bytes()).hexdigest() + "  install-merged.bin\n")
print(f"Exported {len(manifest['files'])} images")
