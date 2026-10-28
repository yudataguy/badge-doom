#!/usr/bin/env python3
"""Package Doom as a GitHub Universe 2026 badge (MonaOS) launcher app.

Writes <out_dir>/doom.bin, the Doom executable padded to the WAD offset with
the WHX data appended, alongside the launcher files from badge-app/doom/. Copy
<out_dir> to /system/apps/doom on the badge.
"""

import os
import shutil
import struct
import sys

WAD_OFFSET = 0x40000   # TINY_WAD_ADDR - XIP_BASE
SECTOR = 4096
PICOBIN_BLOCK_MARKER_START = 0xffffded3
LAUNCHER_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'badge-app', 'doom')
LAUNCHER_FILES = ('__init__.py', 'icon.png', 'README.md')


def has_image_def(image):
    # The bootrom only chains into an image whose block loop starts in the first 4 KiB.
    head = image[:SECTOR]
    return any(struct.unpack_from('<I', head, i)[0] == PICOBIN_BLOCK_MARKER_START
               for i in range(0, len(head) - 3, 4))


def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <doom.bin from build> <wad.whx> <out_dir>")
        sys.exit(1)
    program_bin, wad_file, out_dir = sys.argv[1:]

    with open(program_bin, 'rb') as f:
        program = f.read()
    with open(wad_file, 'rb') as f:
        wad = f.read()

    if len(program) > WAD_OFFSET:
        sys.exit(f"error: program is {len(program)} bytes, overlapping the WAD at 0x{WAD_OFFSET:x}")
    if not has_image_def(program):
        sys.exit("error: no IMAGE_DEF block in the first 4 KiB; the bootrom cannot chain into this image")

    image = program.ljust(WAD_OFFSET, b'\xff') + wad
    image = image.ljust((len(image) + SECTOR - 1) // SECTOR * SECTOR, b'\xff')

    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, 'doom.bin'), 'wb') as f:
        f.write(image)
    for name in LAUNCHER_FILES:
        shutil.copyfile(os.path.join(LAUNCHER_DIR, name), os.path.join(out_dir, name))

    print(f"Program:    {len(program)} bytes")
    print(f"WAD:        {len(wad)} bytes at 0x{WAD_OFFSET:x}")
    print(f"doom.bin:   {len(image)} bytes")
    print(f"Written to: {out_dir}")


if __name__ == '__main__':
    main()
