# GCMM exFAT

<img width="1280" height="960" alt="screenshot1" src="https://github.com/user-attachments/assets/46700957-6eba-42c9-bd03-6b12448abe83" />


GCMM exFAT backs up, restores and deletes the saves on your GameCube memory cards, using an SD card. It's an unofficial fork of [suloku's GCMM](https://github.com/suloku/gcmm) for the GameCube, with:

- **exFAT SD cards**, as well as FAT and FAT32
- a new look in the style of the GameCube's own menus, built around the three jobs: **Back up**, **Restore** and **Card tools**
- tick boxes, so you can back up or restore several saves at once, and labels that say which saves are new, changed or already backed up
- **display settings**: resolution, 4:3 or 16:9, picture width and overscan
- **Update from GitHub**, right from Settings, with a network adapter
- **Network transfer**: send files to the SD card from a phone or computer (zips are unzipped), and download your backups
- **Virtual memory cards**: the card images your Launcher keeps on the SD card work just like real cards: back up, restore, delete, format, and copy saves between real and virtual cards
- both memory cards shown at the top when there's one in each slot
- the controls along the bottom of every screen, and menu sounds
- a new logo, and an opening: the logo flips over and goes into a slot (with its own sound), which you can turn off

It runs on the GameCube only.

## What it does

<img width="1280" height="960" alt="screenshot2" src="https://github.com/user-attachments/assets/ba5a0414-fb77-40e0-8b92-906bf4ee5f98" />


The main screen has three big choices:

- **Back up**: the saves on the memory card, each marked **New** (no backup yet), **Changed** (newer than its backup) or **Backed up**. The new and changed ones start out ticked; X backs up the ticked saves.
- **Restore**: the saves (.gci, .gcs and .sav) in `MCBACKUP` on the SD card, and the folders in it, each marked whether it's on the memory card already. Tick the ones you want and press X. If some are on the card already, you're asked once whether to replace them.
- **Card tools**: a page of its own to back up a whole memory card as a .raw image, restore a whole card (.raw, .gcp and .mci), copy saves between cards, delete saves, format the card, make a new virtual card, use Network transfer, and open Settings.

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
| B | Back; on the main screen, twice to exit to the Launcher |
| Left / Right | A page at a time in a list |
| START | Settings (on the main screen) |

The buttons that do something on each screen are shown along the bottom.

## Settings

Press START on the main screen, or choose Settings in Card tools.

- **Resolution**: Auto (480p with a component cable), 480i or 480p. A new resolution goes back to the old one after 10 seconds unless you press A, in case your TV can't show it.
- **Screen**: 4:3, or 16:9 if your TV stretches the picture to fill a widescreen.
- **Picture width**: Full uses the whole width of the TV picture; Standard leaves a border.
- **Overscan**: pulls everything in from the edges if your TV cuts them off.
- **Menu sounds**: how loud the sounds are when you move and choose, from Off to 10 (4 to start with). The opening's sound plays at this level too.
- **Opening**: On plays the logo animation when GCMM exFAT starts (A or START skips it); Off goes straight to the logo and the main screen.
- **Launcher**: which .dol at the root of the SD card Exit to Launcher starts. Left and Right go through the ones on the card. Until you choose, it's `autoexec.dol`, `boot.dol`, `ipl.dol` or a `swiss*.dol`, whichever is there.
- **Update from GitHub**: see below.
- **Exit to Launcher**: leaves GCMM exFAT.

They're kept in `MCBACKUP/gcmm.ini` on the SD card, and read before the splash screen shows, so it's in 16:9 too when that's set.

## Virtual memory cards

Some Launchers can keep a virtual memory card on the SD card for games to use instead of a real one: a card image per slot and region, in `swiss/saves` (`MemoryCardA.USA.raw`, `MemoryCardB.JPN.raw` and so on). GCMM exFAT finds them and treats them like real cards.

- **Choosing a card:** when there's more than one card around, real or virtual, Back up, Restore, Delete saves, Format and the whole-card jobs ask which one to use.
- **Copy between cards** (Card tools): pick the card to copy from and the one to copy to, real or virtual, either way. Each save shows whether it's on the other card already; tick the ones you want and press X.
- **New virtual card** (Card tools): choose the slot, region and size (59, 251, 1019 or 2043 blocks), and it's made on the SD card, formatted and ready.
- A blank virtual card (as a Launcher first makes it) isn't formatted until a game formats it. Format the card in Card tools formats it straight away.
- Whole-card backups of a virtual card go to `MCBACKUP` like a real card's, and any whole-card image can be restored onto a virtual card.

GCMM exFAT reads and writes the card's own directory and block table inside the image, keeping its checksums right and changing the spare copy first, as a real memory card does, so games and your Launcher see a normal, healthy card.

## Network transfer

<img width="1280" height="960" alt="screenshot3" src="https://github.com/user-attachments/assets/0b435e14-2e5a-4e7f-80db-c502d95668b7" />


In Card tools, with a Broadband Adapter or a WIZnet-based adapter (such as the ETH2GC) connected to your router. Scan the QR code with your phone, or type the address it shows into a browser on a computer on the same network.

- **Sending files:** choose files on the page (or drag them onto it on a computer). Each one arrives whole, then the GameCube asks where it goes: A opens a folder or chooses "Put it here", B goes up a folder. A zip is unzipped into the folder you choose (Z keeps it as a zip instead). If a file with the same name is there already, you can replace it or keep both.
- **Downloading backups:** the page lists the backups in `MCBACKUP` (and its folders); tap one to save it.

It only works while the Network transfer page is open. Files wait in a `gcmm-incoming` folder until you choose where they go.

## Update from GitHub

With a Broadband Adapter or a WIZnet-based adapter (such as the ETH2GC) connected to your router, Settings > Update from GitHub checks this repository's latest release. If it's newer, you see what's new in it, and A downloads and installs it: the new `default.dol` replaces the one GCMM exFAT was started from (with its banner and `cacert.pem`), and it starts straight away. Your settings and backups are left alone. If anything goes wrong, nothing is changed.

## Leaving GCMM exFAT

Press B twice on the main screen (the first press asks, across the top), or choose Exit to Launcher in Settings. It starts the Launcher chosen in Settings. If there's no .dol at the root of the SD card, it goes back to the loader that started GCMM exFAT if there is one, or offers to restart the GameCube.

## Choosing the device when starting

With more than one SD card adapter, GCMM exFAT asks which to use when it starts. It can also be started with one of these arguments (for example from a .cli file next to the .dol): `ask` (always show the device choice), `sdgecko`, `sdgeckoA`, `sdgeckoB`, `sd2sp2` or `gcloader`.

## Building

GCMM exFAT builds with devkitPPC and [libogc2](https://github.com/extremscorner/libogc2), which includes libdvm for FAT and exFAT and the network library. Update from GitHub also needs mbedTLS, which `get-deps.sh` downloads into `third_party`. The easiest way is the libogc2 container:

```
sh get-deps.sh
docker run --rm -v "$PWD:/src" -w /src ghcr.io/extremscorner/libogc2:latest make
```

Every push to GitHub builds it too (see the Actions tab), and the result is a zip with the `gcmm` folder ready for the SD card.

The font textures, logo and card pictures are made by `tools/make_assets.py` (needs Python 3 with Pillow and NumPy), which writes `source/ui_assets.c` and `.h`. `tools/make_banner.py` makes `dist/opening.bnr`, the banner and name your Launcher shows. `tools/sound/make_intro_clip.py` makes `source/intro_clip.h`, the opening's sound (from `tools/sound/gcmm_haze.py`; needs NumPy and SciPy). `tools/make_webpage.py` turns `source/net/webpage.html` (the Network transfer page) into `source/net/webpage.h`.

## Credits

- GCMM: dsbomb and justb, based on askot's libOGC Memory Card Backup; banners and icons by Dronesplitter; folders by dragonbane0; updates, raw mode and devices by suloku
- libogc2 and libdvm: Extrems and the devkitPro team
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls) (Apache 2.0) for the secure connection to GitHub; `cacert.pem` holds Mozilla's trusted root certificates
- Font: M PLUS Rounded 1c (SIL Open Font License, see `tools/fonts/OFL.txt`)

GCMM exFAT is free software under the GNU General Public License, version 3 or later, like GCMM (see `COPYING`). See `NOTICE` for the copyright and the other parts it uses. It isn't affiliated with Nintendo.
