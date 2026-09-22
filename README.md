# ProsperoEden

**ProsperoEden is an unofficial PlayStation 5 port of [Eden](https://github.com/eden-emulator/mirror).** All credit for the emulator core belongs to the Eden project and its contributors. ProsperoEden is not affiliated with or endorsed by the Eden team, Nintendo, or Sony.

This is an early alpha. Hollow Knight has been confirmed working with video, audio, controller input, and saves. Compatibility and performance will vary between games.

## Install

1. Download and extract the release ZIP.
2. Copy the included `PPSA99008` folder to `/data/homebrew/PPSA99008` on the PS5.
3. Supply your own legally dumped keys, firmware, and games using the paths below.
4. Launch **ProsperoEden** and choose **Load ROM**.

The final layout should look like this:

```text
/data/homebrew/PPSA99008/
├── assets/
│   ├── keys/
│   │   ├── prod.keys
│   │   └── title.keys                 # optional
│   ├── nand/system/Contents/registered/
│   │   └── *.nca                      # your dumped Switch firmware
│   └── roms/
│       ├── Game.nsp
│       └── Game.xci
├── eboot.bin
└── ...
```

ProsperoEden does not include Nintendo Switch keys, firmware, games, or other copyrighted console data. Dump these files from hardware and software you own. Do not download or redistribute them.

## In-game shortcuts

“Select” means pressing the DualSense touchpad itself, as in ProsperoLight.

| Shortcut | Action |
|---|---|
| Select + R1 | Toggle the performance HUD |
| Select + L1 | End the running game and return to the ROM menu |

## License

ProsperoEden is distributed under the GNU General Public License v3.0. See the [repository license](https://github.com/blackbearreloaded/ProsperoEden/blob/main/LICENSE).
