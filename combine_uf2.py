#!/usr/bin/env python3
"""Combine a program UF2 with a binary data file at a given flash offset."""

import struct
import sys

UF2_MAGIC0 = 0x0A324655
UF2_MAGIC1 = 0x9E5D5157
UF2_MAGICEND = 0x0AB16F30
UF2_FLAG_FAMILY = 0x00002000

def make_uf2_blocks(data, base_addr, family_id, block_size=256):
    blocks = []
    num_blocks = (len(data) + block_size - 1) // block_size
    for i in range(num_blocks):
        chunk = data[i * block_size:(i + 1) * block_size]
        chunk = chunk.ljust(476, b'\x00')  # pad data area to 476
        header = struct.pack('<IIIIIIII',
                             UF2_MAGIC0, UF2_MAGIC1,
                             UF2_FLAG_FAMILY,
                             base_addr + i * block_size,
                             block_size,
                             i, num_blocks,
                             family_id)
        blocks.append(header + chunk + struct.pack('<I', UF2_MAGICEND))
    return blocks

def main():
    program_uf2 = sys.argv[1]
    wad_bin = sys.argv[2]
    wad_addr = int(sys.argv[3], 0)
    output_uf2 = sys.argv[4]

    with open(program_uf2, 'rb') as f:
        program_data = f.read()

    with open(wad_bin, 'rb') as f:
        wad_data = f.read()

    # Detect family ID from the program UF2 (use the data family, not partition)
    # Find first block with flag 0x2000 (family present) that isn't the partition block
    family_id = 0xe48bff59  # default RP2350 ARM-S data family
    for i in range(len(program_data) // 512):
        block = program_data[i * 512:(i + 1) * 512]
        _, _, flags, addr, _, _, _, fid = struct.unpack('<IIIIIIII', block[:32])
        if flags & UF2_FLAG_FAMILY and addr < 0x10ff0000:
            family_id = fid
            break

    print(f"Program UF2: {len(program_data)} bytes ({len(program_data)//512} blocks)")
    print(f"WAD binary:  {len(wad_data)} bytes")
    print(f"WAD address: 0x{wad_addr:08x}")
    print(f"Family ID:   0x{family_id:08x}")

    wad_blocks = make_uf2_blocks(wad_data, wad_addr, family_id)

    with open(output_uf2, 'wb') as f:
        f.write(program_data)
        for block in wad_blocks:
            f.write(block)

    total_size = len(program_data) + len(wad_blocks) * 512
    print(f"Combined:    {total_size} bytes ({total_size//512} blocks)")
    print(f"Written to:  {output_uf2}")

if __name__ == '__main__':
    if len(sys.argv) != 5:
        print(f"Usage: {sys.argv[0]} <program.uf2> <wad.bin> <wad_addr> <output.uf2>")
        sys.exit(1)
    main()
