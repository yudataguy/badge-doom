#!/usr/bin/env python3
"""Package Doom as a GitHub Universe 2026 badge (MonaOS) launcher app.

Writes <out_dir>/doom.bin, the Doom executable padded to the WAD address (the
target's TINY_WAD_ADDR) with the WHX data appended, alongside the launcher
files from badge-app/doom/. Copy <out_dir> to /system/apps/doom on the badge.
"""

import os
import shutil
import struct
import sys

XIP_BASE = 0x10000000
SECTOR = 4096
PICOBIN_BLOCK_MARKER_START = 0xffffded3
# Every file in this folder ships with the app (CMake globs the same folder).
LAUNCHER_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'badge-app', 'doom')


def has_image_def(image):
    # The bootrom only chains into an image whose block loop starts in the first 4 KiB.
    head = image[:SECTOR]
    return any(struct.unpack_from('<I', head, i)[0] == PICOBIN_BLOCK_MARKER_START
               for i in range(0, len(head) - 3, 4))


def main():
    if len(sys.argv) != 5:
        print(f"Usage: {sys.argv[0]} <program.bin> <wad.whx> <wad_addr> <out_dir>")
        sys.exit(1)
    program_bin, wad_file, wad_addr, out_dir = sys.argv[1:]
    wad_offset = int(wad_addr, 0) - XIP_BASE
    if wad_offset <= 0 or wad_offset % SECTOR:
        sys.exit(f"error: WAD address {wad_addr} is not a 4 KiB aligned address in flash")

    with open(program_bin, 'rb') as f:
        program = f.read()
    with open(wad_file, 'rb') as f:
        wad = f.read()

    if len(program) > wad_offset:
        sys.exit(f"error: program is {len(program)} bytes, overlapping the WAD at 0x{wad_offset:x}")
    if not has_image_def(program):
        sys.exit("error: no IMAGE_DEF block in the first 4 KiB; the bootrom cannot chain into this image")

    image = program.ljust(wad_offset, b'\xff') + wad
    image = image.ljust((len(image) + SECTOR - 1) // SECTOR * SECTOR, b'\xff')

    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, 'doom.bin'), 'wb') as f:
        f.write(image)
    for name in sorted(os.listdir(LAUNCHER_DIR)):
        src = os.path.join(LAUNCHER_DIR, name)
        if os.path.isfile(src):
            shutil.copyfile(src, os.path.join(out_dir, name))

    print(f"Program:    {len(program)} bytes")
    print(f"WAD:        {len(wad)} bytes at 0x{wad_offset:x}")
    print(f"doom.bin:   {len(image)} bytes")
    print(f"Written to: {out_dir}")


if __name__ == '__main__':
    main()
