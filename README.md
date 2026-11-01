# GCMM exFAT

GCMM (GameCube Memory Manager) backs up, restores and deletes the saves on your GameCube memory cards, using an SD card. This is an unofficial fork of [suloku's GCMM](https://github.com/suloku/gcmm) for the GameCube, with:

- **exFAT SD cards**, as well as FAT and FAT32
- a new look, in the style of the GameCube's own menus
- **display settings**: resolution, 4:3 or 16:9, picture width and overscan
- a new logo and splash screen

It runs on the GameCube only.

## What it does

- Back up saves from the memory card to the SD card (as .gci files)
- Restore saves to the memory card (.gci, .gcs and .sav)
- Delete saves from the memory card
- Back up a whole memory card as a .raw image, and restore .raw, .gcp and .mci images
- Format a memory card
- Shows each save's banner, animated icon and details

Saves go in the `MCBACKUP` folder on the SD card. Folders inside it are fine.

SD cards can be in an SD2SP2, an SD Gecko (Slot A or B) or a GC Loader.

## Installing

Download `gcmm.zip` from the Releases page and copy the `gcmm` folder in it to your SD card, where your Launcher looks for apps. It holds `default.dol` and `opening.bnr` (the banner your Launcher shows).

## Controls

| Button | Main screen |
| --- | --- |
| + Control Pad, A | Choose *Saves on the memory card* (back up) or *Saves on the SD card* (restore) |
| Y | Back up saves |
| X | Restore saves |
| Z | Delete saves |
| L + Y | Back up the whole card |
| L + X | Restore the whole card |
| L + Z | Format the card |
| R | Choose the SD card device |
| START | Settings |
| Hold START | Exit to Launcher |

In a list, A chooses, B goes back, R does every save at once, and Left/Right move a page. The bar along the bottom always says what the buttons do.

## Settings

Press START on the main screen.

- **Resolution**: Auto (480p with a component cable), 480i or 480p. A new resolution goes back to the old one after 10 seconds unless you press A, in case your TV can't show it.
- **Screen**: 4:3, or 16:9 if your TV stretches the picture to fill a widescreen.
- **Picture width**: Full uses the whole width of the TV picture; Standard leaves a border.
- **Overscan**: pulls everything in from the edges if your TV cuts them off.

They're kept in `settings.ini` beside GCMM's `default.dol` (or in `MCBACKUP/gcmm.ini` if GCMM can't find its own folder on the SD card).

## Leaving GCMM

Holding START goes back to the loader that started GCMM, if there is one; otherwise it starts `autoexec.dol` from the SD card (your Launcher, if that's where it is), and failing that it restarts the GameCube.

## Choosing the device when starting

GCMM can be started with one of these arguments (for example from a .cli file next to the .dol): `ask` (always show the device choice), `sdgecko`, `sdgeckoA`, `sdgeckoB`, `sd2sp2` or `gcloader`.

## Building

GCMM builds with devkitPPC and [libogc2](https://github.com/extremscorner/libogc2), which includes libdvm for FAT and exFAT. The easiest way is the libogc2 container:

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
