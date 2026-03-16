# GitHub RP2350 Badge Port Notes

`gh.io/badger` currently points at the GitHub badge documentation for the Universe 2025 Tufty badge. That hardware is
an RP2350 badge made by Pimoroni, but it is not compatible with this repository's existing Pico target by changing only
`PICO_BOARD`.

## What the `badger/home` repo clarifies

The official badge repo is useful, but it also confirms that it is a different software stack from this project:

- it ships custom MonaOS MicroPython firmware rather than a Pico SDK C/C++ application model
- badge apps are loaded as Python code under `/system/apps`
- the visible app framebuffer is `160x120`, which is then pixel-doubled to the physical `320x240` display
- input is centered around five front-facing buttons plus a separate Home button
- firmware is distributed as badge-specific `.uf2` images

That means `badger/home` is primarily a reference for:

- display size and app framebuffer expectations
- button layout and UX model
- flashing flow and filesystem layout

It is not, by itself, a low-level C/C++ board support package for this Doom port.

## Why the current build does not map directly

The shipped Pico backend in this repository assumes external VGA and I2S hardware:

- [`src/pico/i_video.c`](../src/pico/i_video.c) is built on
  `pico/scanvideo` and a custom PIO VGA pipeline.
- [`src/pico/i_picosound.c`](../src/pico/i_picosound.c) uses
  `pico/audio_i2s.h` for stereo output.
- [`src/pico/CMakeLists.txt`](../src/pico/CMakeLists.txt) links
  `pico_scanvideo_dpi`, which is specific to the VGA-style video path used here.
- [`README.md`](../README.md) documents the reference `vgaboard`
  pinout: RGB on GPIO 0-15, sync on 16/17, I2S on 26/27/28.

The GitHub badge instead exposes an onboard LCD, onboard buttons, and badge-specific power and peripheral wiring. That
means this is a porting task, not just a board-selection task.

There is a second mismatch as well: the badge repo assumes application code running inside its existing MicroPython
environment, while this repository builds native code with Pico SDK and CMake.

## What a real badge port needs

At minimum:

1. Replace the VGA scanvideo backend with a badge display backend that can push Doom frames to the onboard LCD.
2. Add a badge input layer that maps the badge buttons to Doom actions.
3. Decide what to do about audio:
   - disable it for the first bootable build, or
   - replace the current I2S output path with badge-compatible audio hardware if the badge exposes one.
4. Review flash and asset placement for RP2350 badge flash size, because the current WHD/WHX addresses were chosen for
   the VGA builds in this repository.
5. Add a board-specific CMake target only after the display, input, and memory layout are known.

In practice, there are two possible implementation routes:

1. Native Pico SDK port
   - keep Doom in C/C++
   - add native LCD and button support for the badge hardware
   - produce a standalone UF2 that replaces the stock badge firmware
2. Badge firmware integration
   - target the badge's existing firmware environment
   - reuse its display/input framework
   - this would require a very different integration strategy and is not a small adaptation of the current codebase

For this repository, the realistic path is the first one.

## Practical first milestone

The lowest-risk path is:

1. Keep the Doom game logic and data pipeline as-is.
2. Introduce a new `src/pico` display backend for the badge LCD.
3. Build a first badge target with:
   - onboard button input
   - no USB host keyboard
   - audio disabled
   - shareware data only
4. Once that runs, reintroduce audio and any badge-specific UX.

This repository now includes an experimental CMake scaffold target named `doom_tufty_badger_scaffold`. It exists to
separate the badge build plumbing from the existing VGA build plumbing, but it does not yet contain a real badge LCD
implementation or real badge button handling.

The scaffold now includes:

- a software palette path for Doom's indexed framebuffer
- a software conversion path from Doom's `320x200` output to a badge-style `160x120` RGB565 framebuffer
- a provisional button-to-Doom key mapping layer
- Tufty-specific GPIO/button wiring based on Pimoroni's `tufty2350` board definitions
- a first-cut native parallel LCD flush path for the onboard ST7789-compatible display
- a silent audio backend for the badge scaffold, so it no longer tries to bring up the VGA build's I2S audio path
- a local Pico SDK board header for `pimoroni_tufty2350`, so the scaffold can be configured against a Tufty-specific
  board name instead of borrowing a different RP2350 board definition

What is still missing is the actual hardware bridge:

- an optimized LCD update path comparable to Pimoroni's PIO/DMA display driver
- validation on real hardware that the chosen Doom button mapping is playable

Current native Tufty wiring used by this scaffold:

- buttons: `A=GPIO7`, `B=GPIO9`, `C=GPIO10`, `UP=GPIO11`, `DOWN=GPIO6`, `HOME=GPIO22`
- LCD control: `BACKLIGHT=GPIO26`, `CS=GPIO27`, `DC/RS=GPIO28`, `WR=GPIO30`, `RD=GPIO31`
- LCD data bus: `GPIO32` through `GPIO39`

## Suggested code split

To keep the port contained, split the hardware-specific work like this:

- keep common Doom code under [`src`](../src)
- leave the current VGA implementation in
  [`src/pico/i_video.c`](../src/pico/i_video.c)
- add a second video implementation for the badge LCD
- add a second target in [`src/CMakeLists.txt`](../src/CMakeLists.txt)
  instead of overloading `doom_tiny`

## Non-goals for the first pass

Do not try to preserve all current RP2040 Doom hardware features in the first badge build:

- VGA output
- I2C multiplayer
- USB host keyboard
- external I2S audio

Those are all secondary to getting one stable on-badge framebuffer build running.
