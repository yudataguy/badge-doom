# DOOM

Play the full shareware episode of DOOM natively on the badge.

## Usage

Select DOOM in the launcher. The app hands the badge over to a native Doom
build stored alongside it in `doom.bin`; MonaOS comes back when you leave.

| Button | In game | In menus |
| --- | --- | --- |
| Up / Down | Move forward / back | Move selection |
| Left / Right | Turn left / right | Change option |
| Select | Fire, use, and open doors | Choose, and answer "yes" |
| HOME (tap) | Open the Doom menu | Close the menu, or answer "no" |
| HOME (hold 1 s) | Return to the badge launcher | Return to the badge launcher |

Choosing **Quit Game** in the Doom menu also returns to the launcher. The
capacitive touch pads are not used, and save games are disabled because the
badge's flash belongs to MonaOS.

## Installing

1. Build `doom.bin` (see the
   [badge-doom README](https://github.com/yudataguy/badge-doom#universe-2026-badge-app)),
   or download it from a release.
2. Launch **USB Disk** on the badge and copy this whole `doom` folder,
   including `doom.bin`, into `apps/` on the drive.
3. Eject the drive and reset the badge.

`doom.bin` (about 2 MB) must be stored in one contiguous run of flash. If the
app reports that it is fragmented, delete it, free some space, and copy it
again.
