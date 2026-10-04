#!/usr/bin/env python3
# ps5-native-app-boilerplate - Check that a program sent to the payload loader ends at its section table.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

from pathlib import Path
import struct
import sys


def validate(data):
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
        raise ValueError("helper must be a little-endian ELF64")
    section_offset = struct.unpack_from("<Q", data, 40)[0]
    entry_size, count = struct.unpack_from("<HH", data, 58)
    if entry_size != 64 or count == 0 or section_offset < 64:
        raise ValueError("helper must have ordinary ELF64 section headers")
    if section_offset + count * entry_size != len(data):
        raise ValueError("helper must end at its section table; trailing bytes corrupt the protocol")
    for index in range(count):
        section = struct.unpack_from("<IIQQQQIIQQ", data, section_offset + index * entry_size)
        if section[1] != 8 and section[4] + section[5] > len(data):
            raise ValueError("helper section extends beyond the ELF stream")


if __name__ == "__main__":
    validate(Path(sys.argv[1]).read_bytes())
    print("Loader ELF framing validated.")
