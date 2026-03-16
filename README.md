# Doom on the GitHub Universe 2025 Badge

This fork runs the full shareware Doom (DOOM1.WAD) on the **GitHub Universe 2025 conference badge** — a custom
[Pimoroni Tufty 2350](https://github.com/pimoroni/tufty2350) with an RP2350B processor, 16 MB flash, and a 320x240
ST7789 LCD. Six buttons, no keyboard, no VGA, no audio — just Doom.

Forked from [kilograham/rp2040-doom](https://github.com/kilograham/rp2040-doom), which is itself derived from
[Chocolate Doom](https://github.com/chocolate-doom/chocolate-doom). The original RP2040 Doom blog post is
[here](https://kilograham.github.io/rp2040-doom/).

Badge documentation: [gh.io/badger](https://gh.io/badger)

## Quick Start

### Prerequisites

- [pico-sdk](https://github.com/raspberrypi/pico-sdk) (develop branch recommended)
- [pico-extras](https://github.com/raspberrypi/pico-extras) (latest)
- `arm-none-eabi-gcc` **13.2.rel1** (other versions may cause binary size or stack overflow issues)
- Git submodules initialized: `git submodule update --init`

### Build

```bash
mkdir build-tufty
cd build-tufty
cmake -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DPICO_BOARD=pimoroni_tufty2350 \
      -DPICO_SDK_PATH=/path/to/pico-sdk \
      -DPICO_EXTRAS_PATH=/path/to/pico-extras \
      ..
make doom_tufty_badger_scaffold -j8
```

### Combine UF2 + WAD

The build produces a UF2 with just the executable. The WAD data (`doom1.whx`) must be appended at flash offset
`0x10040000`:

```bash
python3 combine_uf2.py \
    build-tufty/src/doom_tufty_badger_scaffold.uf2 \
    doom1.whx \
    0x10040000 \
    doom_tufty_combined.uf2
```

`doom1.whx` is included in this repository. To regenerate it from `DOOM1.WAD`, build `whd_gen` (see
[whd_gen](#whd_gen) below) and run `whd_gen DOOM1.WAD doom1.whx`.

### Flash

1. Hold **BOOT** on the badge while plugging in USB.
2. Copy the combined UF2 to the USB mass storage device:

```bash
cp doom_tufty_combined.uf2 /Volumes/RP2350/
```

The badge reboots and Doom starts.

## Hardware

### Badge Specs

| Component | Detail |
|-----------|--------|
| MCU | RP2350B (dual Cortex-M33, 150 MHz default, 270 MHz overclocked) |
| Flash | 16 MB QSPI |
| SRAM | 520 KB |
| Display | ST7789 320x240 IPS LCD, parallel 8-bit interface |
| Buttons | 6 (UP, DOWN, A, B, C, HOME) — active low with internal pull-ups |

### LCD Pinout

| Signal | GPIO |
|--------|------|
| D0–D7 (data bus) | 32–39 |
| WR | 30 |
| RD | 31 |
| CS | 27 |
| DC | 28 |
| Backlight | 26 |
| **POWER_EN** | **41** |

**POWER_EN (GPIO 41) must be driven HIGH before any LCD commands.** The badge has a peripheral power rail
controlled by this pin. The backlight LED runs on a separate USB power rail, so it lights up without POWER_EN, but
the ST7789 controller itself is unpowered and ignores all commands until POWER_EN is asserted. The display init
sequence in `badger_hw_tufty.c` handles this automatically.

### Button Mapping

| Button | GPIO | Doom Action |
|--------|------|-------------|
| UP | 10 | Move forward |
| DOWN | 6 | Move backward |
| A | 7 | Turn left |
| B | 8 | Fire + menu select |
| C | 9 | Turn right |
| HOME | 22 | Escape / menu |

Button B sends both `KEY_RCTRL` (fire) and `KEY_ENTER` (menu select) simultaneously, so it works as the action
button in both gameplay and menus.

## Architecture

### Modular Backend System

The original RP2040 Doom had a monolithic Pico backend that assumed VGA output, I2S audio, and I2C networking. This
fork refactors the backend into composable libraries:

| Library | Role |
|---------|------|
| `doom_sound_pico_i2s` | I2S audio output via PIO |
| `doom_sound_pico_null` | Silent audio stub (badge) |
| `doom_input_pico_uart_usb` | UART/USB keyboard input (VGA build) |
| `doom_input_badger_stub` | Badge GPIO button polling |
| `doom_net_pico_i2c` | I2C multiplayer networking (VGA build) |
| `doom_video_vga` | VGA scanline renderer via pico_scanvideo |
| `doom_video_badger_stub` | Badge LCD video (framebuffer composition + ST7789 output) |
| `doom_badger_hw_tufty` | Tufty hardware driver (PIO parallel 8-bit, DMA) |

These are assembled into **backend bundles**:

- **`pico_backend_vga`** = `doom_input_pico_uart_usb` + `doom_net_pico_i2c` + `doom_video_vga` + `doom_sound_pico_i2s`
- **`pico_backend_badger_scaffold`** = `doom_badger_hw_tufty` + `doom_input_badger_stub` + `doom_video_badger_stub` + `doom_sound_pico_null`

### Key Source Files

| File | Description |
|------|-------------|
| `src/pico/badger_hw_tufty.c` | LCD driver: PIO state machine, DMA transfers, ST7789 init, POWER_EN, 2x pixel doubling |
| `src/pico/i_video_badger_stub.c` | Video backend: `compose_display_buffer()` composites all video layers, `service_render_frame()` sends to LCD |
| `src/pico/i_input_badger_stub.c` | Button input: GPIO polling with edge detection, Doom key event dispatch |
| `src/pico/i_picosound_null.c` | Null sound driver (stubs out all audio interfaces) |
| `src/pico/st7789_parallel.pio` | PIO program for parallel 8-bit writes with WR strobe |
| `src/pd_render.cpp` | Renderer: includes wipe bypass for badge (`PICO_DOOM_TUFTY_BADGER`) |
| `src/pico/i_timer.c` | `I_GetTime()` with corrected tick formula |
| `src/d_loop.c` | `TryRunTics` with piconet conditional guards |
| `src/doom/m_menu.c` | Menu with `NET_MENU` guard for network option |
| `src/pico/CMakeLists.txt` | Modular library definitions and backend bundles |

## Porting Notes: Bugs Fixed

Seven bugs were encountered and fixed while porting to the badge:

### 1. Screen wipe freeze (`wipe_min` never advancing)

The VGA scanline renderer updates `volatile uint8_t wipe_min` to track melt-wipe progress. The badge LCD backend
never touches this variable, so the wipe state machine gets stuck at `WIPESTATE_SKIP1` and
`D_RunFrame()`'s `do { D_Display(); } while (wipestate);` spins forever. Fixed by bypassing the wipe entirely in
`pd_render.cpp` for `PICO_DOOM_TUFTY_BADGER` — forcing `wipestate = WIPESTATE_NONE` at the check site.

### 2. `I_GetTime()` returns values 1000x too large

The formula `TICRATE * (uint32_t)(time_us_64() / 1000)` computes `35 * milliseconds` instead of
`milliseconds * 35 / 1000`. Fixed with a multiply-shift approach: `150323855ull * ms >> 32`. This didn't cause the
wipe freeze directly (deltas cancel in singletics mode) but broke the netgame stall timeout path.

### 3. Missing frame composition for overlays and status bar

The original `service_render_frame` read directly from `frame_buffer`, missing overlays, the status bar, and
multi-buffer compositing. Fixed by adding `compose_display_buffer()` that handles all `VIDEO_TYPE_*` modes and
renders vpatch overlays into `screenbuffer` before LCD presentation.

### 4. Build system couldn't separate VGA vs LCD backends

The monolithic `common_pico` library unconditionally linked VGA scanvideo, I2C networking, and I2S audio. Refactored
into the composable library system described in [Architecture](#architecture) above.

### 5. `piconet_stop()` called without `USE_PICO_NET`

`d_loop.c` disconnect code referenced piconet unconditionally, causing a link error on badge builds. Fixed with a
`#if USE_PICO_NET` guard.

### 6. OPL music module linked without audio hardware

`i_sound.c` unconditionally referenced `music_opl_module` even when no audio hardware exists. Fixed with
`PICO_BADGER_NO_AUDIO` compile-time guards.

### 7. Network menu item shown without networking

`m_menu.c` drew the "Start a Network Game" option unconditionally. Fixed with a `NET_MENU` guard to hide it when
networking is not compiled in.

## Original VGA/RP2040 Build

The original VGA build targets still work. This section preserves the upstream build instructions.

### Prerequisites

Same as the badge build (pico-sdk, pico-extras, arm-none-eabi-gcc 13.2.rel1, submodules).

### Build

```bash
mkdir rp2040-build
cd rp2040-build
cmake -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DPICO_BOARD=vgaboard \
      -DPICO_SDK_PATH=/path/to/pico-sdk \
      -DPICO_EXTRAS_PATH=/path/to/pico-extras \
      ..
make -j8
```

### Targets

There are four VGA build targets:

| Target | Format | WAD Address | Notes |
|--------|--------|-------------|-------|
| `doom_tiny` | WHX (super-tiny) | `0x10040000` | DOOM1.WAD on 2M Pico, UART input only |
| `doom_tiny_usb` | WHX (super-tiny) | `0x10042000` | Adds USB keyboard support |
| `doom_tiny_nost` | WHD (standard) | `0x10048000` | For larger WADs (Ultimate Doom, Doom II) on 8M+ |
| `doom_tiny_nost_usb` | WHD (standard) | `0x10048000` | Larger WADs + USB keyboard |

Load the WAD data with [picotool](https://github.com/raspberrypi/picotool):

```bash
picotool load -v -t bin doom1.whx -o 0x10042000
```

### VGA Pin Assignments

```
 0-4:    Red 0-4
 6-10:   Green 0-4
 11-15:  Blue 0-4
 16:     HSync
 17:     VSync
 18:     I2C1 SDA
 19:     I2C1 SCL
 20:     UART1 TX
 21:     UART1 RX
 26:     I2S DIN
 27:     I2S BCK
 28:     I2S LRCK
```

### whd_gen

`doom1.whx` is included in this repository. To regenerate or convert other WADs:

```bash
# Build whd_gen (native build, not cross-compiled)
mkdir build
cd build
cmake ..
make whd_gen

# Generate WHX (required for DOOM1.WAD on 2M flash)
whd_gen DOOM1.WAD doom1.whx

# Generate WHD (for larger WADs on 8M+ flash)
whd_gen DOOMII.WAD doom2.whd -no-super-tiny
```

Use a release build of `whd_gen` for best sound effect fidelity (debug builds lower encoding quality for speed).

## Licenses

- Code derived from Chocolate Doom: **GPLv2**
- New RP2040 Doom code: **BSD-3-Clause**
- ADPCM-XA: **BSD-3-Clause**
- emu8950 (modified): **MIT**
