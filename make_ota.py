#!/usr/bin/env python3
"""
Генератор OTA-образа для 7ch-zigbee-relay

Использование:
  python make_ota.py build/7ch-zigbee-relay.bin

Создаёт: 7ch-zigbee-relay-v1.0.0.ota
Копировать в ZHA OTA-папку HA для обновления по воздуху.
"""
import sys
import os
import struct
import hashlib

OTA_HEADER_MAGIC = 0x5A5A
OTA_FILE_VERSION = 0x01000000  # v1.0.0
OTA_SW_BUILD_ID = b"v1.0.0"


def make_ota(fw_path):
    with open(fw_path, 'rb') as f:
        fw_data = f.read()

    total_size = len(fw_data)
    sw_hash = hashlib.sha256(fw_data).digest()

    # Заголовок OTA: минимальный Espressif-совместимый
    header = struct.pack('<HHI32s',
                         OTA_HEADER_MAGIC,
                         OTA_FILE_VERSION,
                         total_size,
                         sw_hash)

    output_path = f'7ch-zigbee-relay-{OTA_SW_BUILD_ID.decode()}.ota'
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(fw_data)

    print(f"OTA image created: {output_path}")
    print(f"  Firmware size: {total_size} bytes")
    print(f"  SHA256: {sw_hash.hex()}")
    return output_path


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <firmware.bin>")
        sys.exit(1)
    make_ota(sys.argv[1])