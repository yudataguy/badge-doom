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
import time

import machine
import micropython
import uctypes
from array import array

# The launcher imports apps as packages and chdirs into them first, so either
# source gives the app's own folder; doom.bin is opened by absolute path.
if __file__.startswith("/") and __file__.endswith("/__init__.py"):
    APP_DIR = __file__[: -len("/__init__.py")]
else:
    APP_DIR = os.getcwd()
IMAGE_PATH = APP_DIR + "/doom.bin"

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
PSM_WDSEL = 0x40018008
PSM_WDSEL_ALL_BUT_OSCS = 0x01FFFFF3  # as the SDK's _watchdog_enable()
RESETS_RESET = 0x40020000
RESETS_DMA = 0x00000004
RESETS_PIO0_1_2 = 0x00003800
RESETS_USBCTRL = 0x10000000
WATCHDOG_CTRL = 0x400D8000
WATCHDOG_CTRL_ENABLE = 0x40000000
WATCHDOG_LOAD = 0x400D8004
WATCHDOG_SCRATCH4 = 0x400D801C
LAUNCH_TIMEOUT_US = 4000000  # Doom disarms the watchdog first thing in main()
BOOTRAM_BOOTLOCK7 = 0x400E0828  # BOOTROM_LOCK_ENABLE
REG_ALIAS_SET = 0x2000
REG_ALIAS_CLR = 0x3000

BOOTROM_TABLE_LOOKUP = 0x16
RT_FLAG_FUNC_ARM_SEC = 0x0004
ROM_FUNC_CHAIN_IMAGE = ord("C") | (ord("I") << 8)

# chain_image() needs a 3264 byte workarea in SRAM (see _chain). SCRATCH_X
# only holds core 1's stack in the SDK memory map, and core 1 is stopped.
# Core 0's stack (SCRATCH_Y, just above) must leave chain_image() room to run.
WORKAREA = 0x20080000
WORKAREA_BYTES = 0xD00
ROM_STACK_HEADROOM = 0x800


@micropython.asm_thumb
def _call2(r0, r1, r2):
    # return r0(r1, r2)
    mov(r4, r0)
    mov(r0, r1)
    mov(r1, r2)
    data(2, 0x47A0)  # blx r4


@micropython.asm_thumb
def _chain(r0):
    # r0 points at uint32 [chain_image, workarea, workarea_size, window_base,
    # window_size]. Calls chain_image() with the last four, which only returns
    # if the image cannot be launched; then reboots back to MonaOS.
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

    # The workarea must be SRAM for the same reason.
    ldr(r4, [r5, 0])
    ldr(r0, [r5, 4])
    ldr(r1, [r5, 8])
    ldr(r2, [r5, 12])
    ldr(r3, [r5, 16])
    data(2, 0x47A0)  # blx r4

    # Launch failed: watchdog reboot to the launcher, as machine.reset() does
    # (quiesce() already selected what the watchdog resets).
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


@micropython.asm_thumb
def _sp():
    data(2, 0x4668)  # mov r0, sp


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
    # within the FAT volume, or None and the reason it cannot be used.
    size = os.stat(path)[6]
    if size == 0 or size % SECTOR:
        return None, "incomplete"
    with open(path, "rb") as f:
        head = f.read(SECTOR)
    head8 = head[:8]
    head_crc = binascii.crc32(head)
    crc = None  # whole-file CRC, only read once a candidate turns up

    for addr in range(FAT_XIP_BASE, FAT_XIP_BASE + FAT_BYTES - size + 1, SECTOR):
        if uctypes.bytearray_at(addr, 8) != head8:
            continue
        if binascii.crc32(uctypes.bytearray_at(addr, SECTOR)) != head_crc:
            continue
        if crc is None:
            crc = file_crc(path)
        if binascii.crc32(uctypes.bytearray_at(addr, size)) == crc:
            return addr, size
    return None, "fragmented"


def quiesce():
    mem32 = machine.mem32
    # No MonaOS interrupt may fire once Doom's vector table is live.
    mem32[SYST_CSR] = 0
    for i in range(2):
        mem32[NVIC_ICER0 + 4 * i] = 0xFFFFFFFF
        mem32[NVIC_ICPR0 + 4 * i] = 0xFFFFFFFF
    mem32[SCB_ICSR] = ICSR_PENDSTCLR | ICSR_PENDSVCLR
    mem32[WATCHDOG_CTRL + REG_ALIAS_CLR] = WATCHDOG_CTRL_ENABLE
    # Let the fallback watchdog trigger in _chain() actually reset the chip.
    mem32[PSM_WDSEL + REG_ALIAS_SET] = PSM_WDSEL_ALL_BUT_OSCS
    # Stop core 1 (it may be executing from flash, which is about to be
    # remapped) and let it restart into the bootrom's launch wait loop.
    mem32[PSM_FRCE_OFF + REG_ALIAS_SET] = PSM_FRCE_OFF_PROC1
    while not mem32[PSM_FRCE_OFF] & PSM_FRCE_OFF_PROC1:
        pass
    mem32[PSM_FRCE_OFF + REG_ALIAS_CLR] = PSM_FRCE_OFF_PROC1
    # Stop MonaOS's DMA and PIO (display, wireless, LEDs) so nothing keeps
    # writing SRAM while chain_image() and Doom's crt0 use it, and drop off
    # USB so the host sees a clean disconnect. Doom's runtime takes them out
    # of reset again.
    mem32[RESETS_RESET + REG_ALIAS_SET] = RESETS_DMA | RESETS_PIO0_1_2 | RESETS_USBCTRL
    # Release the bootrom lock-checking lock, as rom_chain_image() does.
    mem32[BOOTRAM_BOOTLOCK7] = 0
    # Arm the watchdog last: if the launch fails in a way the fallback in
    # _chain() cannot handle (a fault, a hang), the badge still reboots to
    # the launcher. Scratch 4 = 0 makes that a normal boot.
    mem32[WATCHDOG_SCRATCH4] = 0
    mem32[WATCHDOG_LOAD] = LAUNCH_TIMEOUT_US
    mem32[WATCHDOG_CTRL + REG_ALIAS_SET] = WATCHDOG_CTRL_ENABLE


def chain(addr, size):
    lookup = machine.mem16[BOOTROM_TABLE_LOOKUP]
    chain_image = _call2(lookup, ROM_FUNC_CHAIN_IMAGE, RT_FLAG_FUNC_ARM_SEC)
    if not chain_image:
        return "no chain_image in ROM"
    sp = _sp()
    if WORKAREA <= sp < WORKAREA + WORKAREA_BYTES + ROM_STACK_HEADROOM:
        return "stack too close to workarea"
    args = array("I", [chain_image, WORKAREA, WORKAREA_BYTES, addr, size])
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
    if not file_exists(IMAGE_PATH):
        fail(["doom.bin is missing", "Copy it into", "apps/" + APP_DIR.rsplit("/", 1)[-1]])
        return

    addr, size_or_reason = find_image(IMAGE_PATH)
    if size_or_reason == "incomplete":
        fail(["doom.bin is incomplete", "Rebuild or download", "it, then copy again"])
        return
    if addr is None:
        fail(["doom.bin is fragmented", "Delete it, then copy", "it to the badge again"])
        return

    show(["DOOM", "Hold HOME to exit"])
    time.sleep(0.8)

    # HOME doubles as the boot switch; do not hand over while it is held.
    while not machine.Pin.board.BUTTON_HOME.value():
        pass

    error = chain(addr, size_or_reason)
    fail(["Could not start DOOM", error])


main()
