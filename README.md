# GCMM exFAT

GCMM exFAT backs up, restores and deletes the saves on your GameCube memory cards, using an SD card. It's an unofficial fork of [suloku's GCMM](https://github.com/suloku/gcmm) for the GameCube, with:

- **exFAT SD cards**, as well as FAT and FAT32
- a new look in the style of the GameCube's own menus, built around the three jobs: **Back up**, **Restore** and **Card tools**
- tick boxes, so you can back up or restore several saves at once, and labels that say which saves are new, changed or already backed up
- **display settings**: resolution, 4:3 or 16:9, picture width and overscan
- a new logo and splash screen

It runs on the GameCube only.

## What it does

The main screen has three big choices:

- **Back up**: the saves on the memory card, each marked **New** (no backup yet), **Changed** (newer than its backup) or **Backed up**. The new and changed ones start out ticked; X backs up the ticked saves.
- **Restore**: the saves (.gci, .gcs and .sav) in `MCBACKUP` on the SD card, and the folders in it, each marked whether it's on the memory card already. Tick the ones you want and press X. If some are on the card already, you're asked once whether to replace them.
- **Card tools**: back up a whole memory card as a .raw image, restore a whole card (.raw, .gcp and .mci), delete saves, format the card, choose the SD card, and the settings.

Every save shows its own icon and comment. The bar along the bottom says how many saves you've ticked and where they'll go.

Backups go in the `MCBACKUP` folder on the SD card; folders inside it are fine. SD cards can be in an SD2SP2, an SD Gecko (Slot A or B) or a GC Loader. With an SD2SP2 or GC Loader, GCMM exFAT uses whichever slot has a memory card in it.

## Installing

Download `gcmm.zip` from the Releases page and copy the `gcmm` folder in it to your SD card, where your Launcher looks for apps. It holds `default.dol` and `opening.bnr` (the banner your Launcher shows).

## Controls

| Button | |
| --- | --- |
| Control Stick, + Control Pad | Move |
| A | Choose; in a list, tick a save or open a folder |
| Y | Tick every save (or none) |
| X | Back up, restore or delete the ticked saves |
| B | Back |
| Left / Right | A page at a time in a list |
| START | Settings (on the main screen) |
| Hold START | Exit to Launcher |

## Settings

Press START on the main screen.

- **Resolution**: Auto (480p with a component cable), 480i or 480p. A new resolution goes back to the old one after 10 seconds unless you press A, in case your TV can't show it.
- **Screen**: 4:3, or 16:9 if your TV stretches the picture to fill a widescreen.
- **Picture width**: Full uses the whole width of the TV picture; Standard leaves a border.
- **Overscan**: pulls everything in from the edges if your TV cuts them off.

They're kept in `settings.ini` beside GCMM exFAT's `default.dol` (or in `MCBACKUP/gcmm.ini` if it can't find its own folder on the SD card). They're also in Card tools.

## Leaving GCMM exFAT

Holding START on the main screen goes back to the loader that started it, if there is one; otherwise it starts `autoexec.dol` from the SD card (your Launcher, if that's where it is), and failing that it restarts the GameCube.

## Choosing the device when starting

GCMM exFAT can be started with one of these arguments (for example from a .cli file next to the .dol): `ask` (always show the device choice), `sdgecko`, `sdgeckoA`, `sdgeckoB`, `sd2sp2` or `gcloader`.

## Building

GCMM exFAT builds with devkitPPC and [libogc2](https://github.com/extremscorner/libogc2), which includes libdvm for FAT and exFAT. The easiest way is the libogc2 container:

```
docker run --rm -v "$PWD:/src" -w /src ghcr.io/extremscorner/libogc2:latest make
```

Every push to GitHub builds it too (see the Actions tab), and the result is a zip with the `gcmm` folder ready for the SD card.

The font textures, logo and card pictures are made by `tools/make_assets.py` (needs Python 3 with Pillow and NumPy), which writes `source/ui_assets.c` and `.h`.

## Credits

- GCMM: dsbomb and justb, based on askot's libOGC Memory Card Backup; banners and icons by Dronesplitter; folders by dragonbane0; updates, raw mode and devices by suloku
- libogc2 and libdvm: Extrems and the devkitPro team
- Font: M PLUS Rounded 1c (SIL Open Font License, see `tools/fonts/OFL.txt`)

GCMM is under the GPL (see `COPYING`).
