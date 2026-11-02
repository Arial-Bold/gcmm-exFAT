# GCMM exFAT

GCMM exFAT backs up, restores and deletes the saves on your GameCube memory cards, using an SD card. It's an unofficial fork of [suloku's GCMM](https://github.com/suloku/gcmm) for the GameCube, with:

- **exFAT SD cards**, as well as FAT and FAT32
- a new look in the style of the GameCube's own menus, built around the three jobs: **Back up**, **Restore** and **Card tools**
- tick boxes, so you can back up or restore several saves at once, and labels that say which saves are new, changed or already backed up
- **display settings**: resolution, 4:3 or 16:9, picture width and overscan
- **Update from GitHub**, right from Settings, with a network adapter
- both memory cards shown at the top when there's one in each slot
- the controls along the bottom of every screen
- a new logo and splash screen

It runs on the GameCube only.

## What it does

The main screen has three big choices:

- **Back up**: the saves on the memory card, each marked **New** (no backup yet), **Changed** (newer than its backup) or **Backed up**. The new and changed ones start out ticked; X backs up the ticked saves.
- **Restore**: the saves (.gci, .gcs and .sav) in `MCBACKUP` on the SD card, and the folders in it, each marked whether it's on the memory card already. Tick the ones you want and press X. If some are on the card already, you're asked once whether to replace them.
- **Card tools**: a page of its own to back up a whole memory card as a .raw image, restore a whole card (.raw, .gcp and .mci), delete saves, format the card, choose the SD card, and open Settings.

Every save shows its own icon and comment. The bar along the bottom says how many saves you've ticked and where they'll go, and under it are the buttons you can press.

Backups go in the `MCBACKUP` folder on the SD card; folders inside it are fine. SD cards can be in an SD2SP2, an SD Gecko (Slot A or B) or a GC Loader. With an SD2SP2 or GC Loader, GCMM exFAT uses whichever slot has a memory card in it.

## Installing

Download `gcmm.zip` from the Releases page and copy the `gcmm` folder in it to your SD card, where your Launcher looks for apps. It holds `default.dol`, `opening.bnr` (the banner your Launcher shows) and `cacert.pem` (the certificates Update from GitHub checks GitHub's secure connection with).

## Controls

| Button | |
| --- | --- |
| Control Stick, + Control Pad | Move |
| A | Choose; in a list, tick a save or open a folder |
| Y | Tick every save (or none) |
| X | Back up, restore or delete the ticked saves |
| B | Back; on the main screen, Exit to Launcher |
| Left / Right | A page at a time in a list |
| START | Settings (on the main screen) |

The buttons that do something on each screen are shown along the bottom.

## Settings

Press START on the main screen, or choose Settings in Card tools.

- **Resolution**: Auto (480p with a component cable), 480i or 480p. A new resolution goes back to the old one after 10 seconds unless you press A, in case your TV can't show it.
- **Screen**: 4:3, or 16:9 if your TV stretches the picture to fill a widescreen.
- **Picture width**: Full uses the whole width of the TV picture; Standard leaves a border.
- **Overscan**: pulls everything in from the edges if your TV cuts them off.
- **Update from GitHub**: see below.
- **Exit to Launcher**: leaves GCMM exFAT.

They're kept in `MCBACKUP/gcmm.ini` on the SD card, and read before the splash screen shows, so it's in 16:9 too when that's set.

## Update from GitHub

With a Broadband Adapter or a WIZnet-based adapter (such as the ETH2GC) connected to your router, Settings > Update from GitHub checks this repository's latest release. If it's newer, you see what's new in it, and A downloads and installs it: the new `default.dol` replaces the one GCMM exFAT was started from (with its banner and `cacert.pem`), and it starts straight away. Your settings and backups are left alone. If anything goes wrong, nothing is changed.

## Leaving GCMM exFAT

B on the main screen (or Exit to Launcher in Settings) goes back to the loader that started it, if there is one; otherwise it starts `autoexec.dol` from the SD card (your Launcher, if that's where it is), and failing that it restarts the GameCube.

## Choosing the device when starting

GCMM exFAT can be started with one of these arguments (for example from a .cli file next to the .dol): `ask` (always show the device choice), `sdgecko`, `sdgeckoA`, `sdgeckoB`, `sd2sp2` or `gcloader`.

## Building

GCMM exFAT builds with devkitPPC and [libogc2](https://github.com/extremscorner/libogc2), which includes libdvm for FAT and exFAT and the network library. Update from GitHub also needs mbedTLS, which `get-deps.sh` downloads into `third_party`. The easiest way is the libogc2 container:

```
sh get-deps.sh
docker run --rm -v "$PWD:/src" -w /src ghcr.io/extremscorner/libogc2:latest make
```

Every push to GitHub builds it too (see the Actions tab), and the result is a zip with the `gcmm` folder ready for the SD card.

The font textures, logo and card pictures are made by `tools/make_assets.py` (needs Python 3 with Pillow and NumPy), which writes `source/ui_assets.c` and `.h`.

## Credits

- GCMM: dsbomb and justb, based on askot's libOGC Memory Card Backup; banners and icons by Dronesplitter; folders by dragonbane0; updates, raw mode and devices by suloku
- libogc2 and libdvm: Extrems and the devkitPro team
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls) (Apache 2.0) for the secure connection to GitHub; `cacert.pem` holds Mozilla's trusted root certificates
- Font: M PLUS Rounded 1c (SIL Open Font License, see `tools/fonts/OFL.txt`)

GCMM is under the GPL (see `COPYING`).
