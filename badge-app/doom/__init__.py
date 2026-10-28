# Doom launcher for the GitHub Universe 2026 badge (MonaOS).
#
# Doom is a native RP2350 program, not a MicroPython app, so this app does not
# run it inside MonaOS. Instead it finds doom.bin (the Doom executable with the
# shareware WAD packed at offset 0x40000) where the /system FAT volume stores
# it in flash, and asks the RP2350 bootrom to chain into it. The bootrom's
# address translation maps the file to the start of the XIP window, so the
# image runs exactly as if it had been flashed at 0x10000000. Holding HOME in
# Doom (or choosing Quit Game) reboots back to MonaOS.

import binascii
import os
import sys
import time

import machine
import micropython
import uctypes
from array import array

APP_DIR = "/system/apps/doom"
os.chdir(APP_DIR)
sys.path.insert(0, APP_DIR)

IMAGE_FILE = "doom.bin"

# /system is a FAT volume on 4 KiB sectors at a fixed flash offset (see
# pimoroni/tufty2350 board/filesystem.cmake and modules/python/_msc.py).
FAT_XIP_BASE = 0x10300000
FAT_BYTES = 12 * 1024 * 1024
SECTOR = 4096

# RP2350 registers used to quiesce MonaOS before handing over the chip.
SYST_CSR = 0xE000E010
NVIC_ICER0 = 0xE000E180
NVIC_ICPR0 = 0xE000E280
SCB_ICSR = 0xE000ED04
ICSR_PENDSTCLR = 1 << 25
ICSR_PENDSVCLR = 1 << 27
PSM_FRCE_OFF = 0x40018004
PSM_FRCE_OFF_PROC1 = 0x01000000
RESETS_RESET = 0x40020000
RESETS_USBCTRL = 0x10000000
WATCHDOG_CTRL = 0x400D8000
WATCHDOG_CTRL_ENABLE = 0x40000000
BOOTRAM_BOOTLOCK7 = 0x400E0828  # BOOTROM_LOCK_ENABLE
REG_ALIAS_SET = 0x2000
REG_ALIAS_CLR = 0x3000

BOOTROM_TABLE_LOOKUP = 0x16
RT_FLAG_FUNC_ARM_SEC = 0x0004
ROM_FUNC_CHAIN_IMAGE = ord("C") | (ord("I") << 8)


@micropython.asm_thumb
def _call2(r0, r1, r2):
    # return r0(r1, r2)
    mov(r4, r0)
    mov(r0, r1)
    mov(r1, r2)
    data(2, 0x47A0)  # blx r4


@micropython.asm_thumb
def _chain(r0):
    # r0 points at uint32 [chain_image, window_base, window_size].
    # Calls chain_image(workarea, 4096, window_base, window_size), which only
    # returns if the image cannot be launched; then reboots back to MonaOS.
    mov(r5, r0)

    # MonaOS keeps its heap in PSRAM behind the XIP cache, and chain_image
    # invalidates that cache. Write back every dirty line first so nothing
    # this code still needs (including itself) is lost. (Clean by set/way;
    # as in the SDK's xip_cache_clean_all().)
    movw(r1, 0xC001)
    movt(r1, 0x1BFF)  # XIP_MAINTENANCE_BASE + 0x03FFC000 + CLEAN_BY_SET_WAY
    movw(r2, 0x4000)
    add(r2, r2, r1)
    mov(r0, 0)
    label(clean)
    strb(r0, [r1, 0])
    add(r1, 8)
    cmp(r1, r2)
    blt(clean)
    data(2, 0xF3BF, 0x8F4F)  # dsb sy
    data(2, 0xF3BF, 0x8F6F)  # isb sy

    # The workarea must be SRAM for the same reason; use 8 KiB below our
    # stack, which MonaOS will never need again.
    data(2, 0x4668)  # mov r0, sp
    movw(r1, 0x2000)
    sub(r0, r0, r1)
    movw(r1, 0x1000)
    ldr(r4, [r5, 0])
    ldr(r2, [r5, 4])
    ldr(r3, [r5, 8])
    data(2, 0x47A0)  # blx r4

    # Launch failed: watchdog reboot to the launcher, as machine.reset() does.
    movw(r1, 0x801C)
    movt(r1, 0x400D)  # WATCHDOG_SCRATCH4
    mov(r0, 0)
    str(r0, [r1, 0])
    movw(r1, 0xA000)
    movt(r1, 0x400D)  # WATCHDOG_CTRL set alias
    movw(r0, 0)
    movt(r0, 0x8000)  # WATCHDOG_CTRL_TRIGGER
    str(r0, [r1, 0])
    label(hang)
    b(hang)


def show(lines):
    screen.pen = color.black
    screen.shape(shape.rectangle(0, 0, screen.width, screen.height))
    try:
        screen.font = font.absolute
    except OSError:
        pass
    screen.pen = color.white
    y = (screen.height - 16 * len(lines)) / 2
    for line in lines:
        w, _ = screen.measure_text(line)
        screen.text(line, (screen.width - w) / 2, y)
        y += 16
    display.update()


def file_crc(path):
    crc = 0
    buf = bytearray(SECTOR)
    with open(path, "rb") as f:
        while n := f.readinto(buf):
            crc = binascii.crc32(memoryview(buf)[:n], crc)
    return crc


def find_image(path):
    # Return the XIP address of a contiguous, 4 KiB aligned copy of the file
    # within the FAT volume, or None if the file is fragmented.
    size = os.stat(path)[6]
    if size == 0 or size % SECTOR:
        return None, size
    with open(path, "rb") as f:
        head = f.read(SECTOR)
    head8 = head[:8]
    head_crc = binascii.crc32(head)
    crc = file_crc(path)

    for addr in range(FAT_XIP_BASE, FAT_XIP_BASE + FAT_BYTES - size + 1, SECTOR):
        if uctypes.bytearray_at(addr, 8) != head8:
            continue
        if binascii.crc32(uctypes.bytearray_at(addr, SECTOR)) != head_crc:
            continue
        if binascii.crc32(uctypes.bytearray_at(addr, size)) == crc:
            return addr, size
    return None, size


def quiesce():
    mem32 = machine.mem32
    # No MonaOS interrupt may fire once Doom's vector table is live.
    mem32[SYST_CSR] = 0
    for i in range(2):
        mem32[NVIC_ICER0 + 4 * i] = 0xFFFFFFFF
        mem32[NVIC_ICPR0 + 4 * i] = 0xFFFFFFFF
    mem32[SCB_ICSR] = ICSR_PENDSTCLR | ICSR_PENDSVCLR
    mem32[WATCHDOG_CTRL + REG_ALIAS_CLR] = WATCHDOG_CTRL_ENABLE
    # Stop core 1 (it may be executing from flash, which is about to be
    # remapped) and let it restart into the bootrom's launch wait loop.
    mem32[PSM_FRCE_OFF + REG_ALIAS_SET] = PSM_FRCE_OFF_PROC1
    while not mem32[PSM_FRCE_OFF] & PSM_FRCE_OFF_PROC1:
        pass
    mem32[PSM_FRCE_OFF + REG_ALIAS_CLR] = PSM_FRCE_OFF_PROC1
    # Drop off USB so the host sees a clean disconnect.
    mem32[RESETS_RESET + REG_ALIAS_SET] = RESETS_USBCTRL
    # Release the bootrom lock-checking lock, as rom_chain_image() does.
    mem32[BOOTRAM_BOOTLOCK7] = 0


def chain(addr, size):
    lookup = machine.mem16[BOOTROM_TABLE_LOOKUP]
    chain_image = _call2(lookup, ROM_FUNC_CHAIN_IMAGE, RT_FLAG_FUNC_ARM_SEC)
    if not chain_image:
        return "no chain_image in ROM"
    args = array("I", [chain_image, addr, size])
    quiesce()
    _chain(uctypes.addressof(args))  # does not return


def fail(lines):
    def update():
        show(lines + ["", "Press any button"])
        if badge.pressed():
            return True

    run(update)


def main():
    show(["DOOM", "Loading..."])
    if not file_exists(IMAGE_FILE):
        fail(["doom.bin is missing", "Copy it into", "/apps/doom"])
        return

    addr, size = find_image(IMAGE_FILE)
    if addr is None:
        fail(["doom.bin is fragmented", "Delete it, then copy", "it to the badge again"])
        return

    show(["DOOM", "Hold HOME to exit"])
    time.sleep(0.8)

    # HOME doubles as the boot switch; do not hand over while it is held.
    while not machine.Pin.board.BUTTON_HOME.value():
        pass

    error = chain(addr, size)
    fail(["Could not start DOOM", error])


main()
