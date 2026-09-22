<p align="center">
  <img src="sce_sys/icon0.png" width="128" alt="ProsperoEden icon">
</p>

<h1 align="center">ProsperoEden</h1>

<p align="center">
  <strong>An unofficial Eden emulator port for PlayStation 5 homebrew</strong>
</p>

**ProsperoEden is an unofficial PlayStation 5 port of [Eden](https://github.com/eden-emulator/mirror).** All credit for the emulator core belongs to the Eden project and its contributors. ProsperoEden is not affiliated with or endorsed by the Eden team, Nintendo, or Sony.

This is an early alpha. Hollow Knight has been confirmed working with video, audio, controller input, and saves. Compatibility and performance will vary between games.

![ProsperoEden launcher](docs/images/prosperoeden.png)

## Project foundation

> [!IMPORTANT]
> **Built on the [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate), the same native foundation used by ProsperoLight.**
> It provides the native PS5 application structure, runtime, packaging, RmlUi shell, and homebrew deployment foundation.

> [!IMPORTANT]
> **Graphics are powered by [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl).**
> This OpenGL implementation provides the native PS5 rendering layer used by the Eden graphics backend.

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

## Credits and license

ProsperoEden exists thanks to the Eden maintainers and contributors, the PS5 Native App Boilerplate, ps5-opengl, and the wider PS5 homebrew community.

ProsperoEden is distributed under [GPL-3.0](LICENSE). PlayStation and PS5 are trademarks of Sony Interactive Entertainment. Nintendo Switch is a trademark of Nintendo. ProsperoEden is an independent homebrew project and is not affiliated with or endorsed by Sony Interactive Entertainment, Nintendo, or the Eden project.

This project was developed with assistance from OpenAI Codex, including some original interface artwork. Project maintainers reviewed and validated the resulting code, tests, documentation, dependencies, and generated assets.
