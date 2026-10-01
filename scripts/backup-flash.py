"""Esptool 4.x backup with one packet in flight for native USB reliability."""
import hashlib
import struct
import sys
import esptool
from esptool.loader import ESPLoader

original = ESPLoader.read_flash

def read_flash(self, offset, length, progress_fn=None):
    if not self.IS_STUB:
        return original(self, offset, length, progress_fn)
    self.check_command("read flash", self.ESP_READ_FLASH,
                       struct.pack("<IIII", offset, length, self.FLASH_SECTOR_SIZE, 1))
    data = bytearray()
    while len(data) < length:
        packet = self.read()
        if len(packet) != min(self.FLASH_SECTOR_SIZE, length - len(data)):
            raise esptool.FatalError("Incomplete backup packet; flash must remain unchanged")
        data.extend(packet)
        self.write(struct.pack("<I", len(data)))
        if len(data) % 1048576 == 0:
            print(f"Backup received: {len(data) // 1048576} / {length // 1048576} MiB", flush=True)
    digest = self.read()
    if digest != hashlib.md5(data).digest():
        raise esptool.FatalError("Backup checksum mismatch; flash must remain unchanged")
    return bytes(data)

if __name__ == "__main__":
    sys.stdout.reconfigure(line_buffering=True)
    ESPLoader.read_flash = read_flash
    esptool.main(sys.argv[1:])
