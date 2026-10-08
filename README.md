<p align="center">
  <img src="sce_sys/icon0.png" width="128" alt="ProsperoEden icon">
</p>

<h1 align="center">ProsperoEden</h1>

<p align="center">
  <strong>An unofficial Eden emulator port for PlayStation 5 homebrew</strong>
</p>

**ProsperoEden is an unofficial PlayStation 5 port of [Eden](https://github.com/eden-emulator/mirror)** - an accurate, high-performance emulator. All credit for the emulator core belongs to the Eden project and its contributors. ProsperoEden is not affiliated with or endorsed by the Eden team or Sony.

This is an early alpha. Video, audio, controller input, and saves have been confirmed working. Compatibility and performance will vary between games. The current release is **v1.000.080**.

## Source code

The complete ProsperoEden source is in this repository: the PS5 frontend and launcher in `headless/`, and the build and packaging tools in `tools/`. To build it yourself, run `make` on Linux (Ubuntu 26.04; WSL works). It fetches every dependency at its pinned revision and writes the release files to `dist/`; `make help` lists the other targets. See [docs/BUILDING.md](docs/BUILDING.md).

## Project foundation

> [!IMPORTANT]
> **Built on the [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate), the same native foundation used by ProsperoLight.**
> It provides the native PS5 application structure, runtime, packaging, and homebrew deployment foundation.

> [!IMPORTANT]
> **Graphics are powered by [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl).**
> This OpenGL implementation provides the native PS5 rendering layer used by the Eden graphics backend.

> [!IMPORTANT]
> **Vulkan is powered by Mihawk's [PS5 Mesa](https://github.com/mihawk-99/PS5_Mesa) and [PS5 Vulkan](https://github.com/mihawk-99/PS5_Vulkan).**
> Mihawk's Mesa/RADV driver for the PS5 runs ProsperoEden's Vulkan renderer. Many thanks to Mihawk for this work and for [all of the PS5 projects](https://github.com/mihawk-99) behind it.

> [!IMPORTANT]
> **Thanks to [ps5-vulkan](https://github.com/mpereiraesaa/ps5-vulkan) by mpereiraesaa**, an experimental Vulkan graphics and compute API for native PS5 homebrew.

## Features

- **Vulkan renderer (recommended)** - the default backend, running on Mihawk's PS5 Mesa (RADV) driver.
- **OpenGL renderer** - still available through ps5-opengl. Switch between them in **Settings > Video**.
- **Resolution and upscaling** - render at 0.5x to 4x of the game's resolution and choose the filter that scales it to your TV (Bilinear, AMD FSR, Bicubic or Nearest) in **Settings > Video**.
- **Output resolution** - the picture is made at 1080p, 1440p or 2160p: **Output resolution** in **Settings > Video**. The menu is drawn at that size too, and the PS5 scales it to your TV.
- **120 Hz output** - on a display that shows 120 Hz, games can run on a 120 Hz output: **Refresh rate** in **Settings > Video**, or in one game's settings. A frame that is a little late is then shown 8 ms later instead of 17 ms, and patches for more than 60 FPS need it. The menu stays at 60 Hz.
- **Game files anywhere** - keys, firmware, and games can live in any folder the PS5 can read: internal storage, an M.2 or external drive, or a USB device.
- **Folder browser** - pick the game files folder in **Settings > Game files**. It shows how many keys, firmware files, and games each folder holds. Hold L1/R1 to page quickly.
- **Library** - game covers, **Continue Playing**, and **Recently Played**, which keep working after you move your files.
- **Launcher** - an animated interface drawn with OpenGL, with sound effects (their level is in **Settings > Audio**) and a loading screen while a game starts. The home screen shows which controllers are connected.
- **Profiles** - everyone who plays has their own save data, settings and recently played games: **Settings > Profiles**; see [Profiles](#profiles).
- **Settings per game** - a game can differ from Settings in its video, performance, audio, controls and language, and has its own Handheld / Docked mode (Triangle in the Library); see [Settings per game](#settings-per-game).
- **Button mapping** - choose which DualSense button presses each of the game's buttons, for every controller, in **Settings > Controls**, or for one game in its settings; see [Button mapping](#button-mapping).
- **Your language** - the launcher follows the language the PS5 is set to (29 languages; English otherwise).
- **Accessibility** - larger text, high contrast and reduced motion, in **Settings > Accessibility**.
- **Save data in and out** - import a game's save from a folder or from a Ryujinx data folder, and export a copy (Triangle in the Library, then **Save data**).
- **Game updates and DLC** - put update and DLC files (NSP or XCI) in the `updates` folder next to `roms`. They apply when the game starts, and each game's details show the update version and DLC count.
- **Mods** - patches, replacement game files and cheats for a game, from a `mods` folder next to `roms`, each switched on or off in the game's settings (Triangle in the Library, then **Mods**).
- **Performance switches** - seven switches that trade accuracy for speed (compiling a game's code ahead, asynchronous shaders, faster GPU, CPU and DMA emulation, and more) in **Settings > Performance**; see [Performance settings](#performance-settings).
- **Shader cache** - shaders compiled in earlier sessions are loaded when a game starts, so an effect stutters only the first time it appears.
- **In-game shortcuts** - a performance overlay (Touchpad + R1), and Touchpad + L1 to end the game and return to the library. Leaving takes ten seconds at most, also while a game is still loading.
- **Settings in one place** - a single JSON file under `/data/prosperoeden`, with game volume, mute, and detailed logging options. Logs keep the previous session.
- **Updates from the menu** - when a newer release is listed on [homebrew.page](https://homebrew.page/ps5), ProsperoEden offers to install it each time it opens; **Skip** keeps the current version; see [Updating](#updating).
- **Crash reports** - if ProsperoEden stops because of an error, it saves a report with that session's logs, starts again and shows where the report is.
- **Controllers, audio, and saves** - up to four DualSense controllers (one per signed-in PS5 user) with rumble and motion controls, game audio, and save data work out of the box. Games that have a single Joy-Con held sideways are played with the DualSense held as usual: the stick, the buttons and the motion sensors are turned to match.

## Install

> [!TIP]
> **Use [ShadowMountPlus 1.7beta4](https://github.com/drakmor/ShadowMountPlus/releases/tag/1.7beta4) or newer.** It mounts ProsperoEden from the folder (or drive) you copy it to, and it also mounts `/data` and USB and extended storage drives into the app's sandbox, so ProsperoEden reaches its data and your game files with nothing else to load. With an older ShadowMountPlus, ProsperoEden falls back to its bundled [Lapy](https://github.com/blackbearreloaded/PS5-Lapy-JB-Daemon) helper (see [Elevation credits](#elevation-credits)).

1. Download and extract the release ZIP.
2. Copy the included `PPSA99008` folder to `/data/homebrew/PPSA99008` on the PS5 (or to another folder ShadowMountPlus scans, such as `/mnt/usb0/homebrew/PPSA99008`).
3. Make sure your jailbreak environment's local ELF loader is listening on TCP port 9021 (most setups have it). [Updates from the menu](#updating) use it, and with a ShadowMountPlus older than 1.7beta4 ProsperoEden sends its bundled Lapy helper through it.
4. Put your own legally dumped keys, firmware, and games in a **game files folder** (layout below). It can be anywhere the PS5 can read: internal storage, an M.2 or external drive, or a USB device.
5. Launch **ProsperoEden**, open **Settings > Game files**, browse to that folder and select it. The default is `/data/prosperoeden`.
6. Close and reopen ProsperoEden, then open **Library**. Setup is checked when the app opens, so reopen it after changing the folder or adding keys or firmware.

### Game files folder

Only these subfolders matter; the folder itself can have any name and location.

```text
<game files folder>/                    # e.g. /data/prosperoeden, /mnt/ext1/eden, /mnt/usb0/eden
├── keys/
│   ├── prod.keys
│   └── title.keys                      # optional
├── firmware/
│   └── *.nca                           # extracted firmware NCAs
├── roms/
│   ├── Game.nsp
│   └── Game.xci
├── updates/                            # optional: update and DLC files
│   └── Game update.nsp
├── mods/                               # optional: mods, one folder per title ID
│   └── <title ID>/                     # the 16-character code in the game's file name
│       ├── <mod name>/
│       │   ├── exefs/                  # code patches: *.pchtxt, *.ips
│       │   ├── romfs/                  # replacement game files
│       │   └── cheats/
│       │       └── <build ID>.txt      # cheats for one version of the game
│       └── cheat_<name>.txt            # optional: cheats without a mod folder
├── save-import/                        # optional: saves to import, one folder per title ID
├── ryujinx/                            # optional: a Ryujinx data folder to import saves from
└── save-export/                        # written by "Export a copy"
```

The folder browser shows how many keys, firmware files, and games each folder holds, so you can check a folder before selecting it. Moving your files later only needs a new selection in Settings; saved settings, covers, and recently played games carry over.

### App data

ProsperoEden keeps its own data in `/data/prosperoeden`, separately from the game files folder:

```text
/data/prosperoeden/
├── config/prosperoeden.json            # every profile's settings, and the game files folder
├── covers/                             # cached game covers
├── logs/                               # current and previous session logs, crash reports
└── user/                               # every profile's saves, and emulator user data
```

The app itself stays in `/data/homebrew/PPSA99008`; see [Updating](#updating).

ProsperoEden does not include keys, firmware, games, or other copyrighted console data. Dump these files from hardware and software you own. Do not download or redistribute them.

### Updating

Close ProsperoEden first. Your settings, saves, covers and logs are in `/data/prosperoeden`, outside the app, so an update keeps them. Afterwards the About screen shows the version that is running.

Once each time it opens, ProsperoEden asks [homebrew.page](https://homebrew.page/ps5) which release of it is listed there. The request carries the app's title ID and nothing else. If the listed release is newer than the one running, a dialog offers it, with its version and download size:

- **Update now** downloads the release ZIP from its GitHub release, checks it against the SHA-256 in the catalog's signed list, and unpacks it beside the app. A ring shows how far it is and the time left; Circle cancels, and nothing has changed until the end. Then ProsperoEden closes, the update helper (`self-updater.elf`, sent to the console's payload loader on port 9021) replaces the app's files, and the console shows a notification. Open ProsperoEden again to use the new version.
- **What's new** (when the release has notes) shows what changes in it before you decide: the release notes as homebrew.page lists them. Up/Down scroll, L1/R1 jump a page, Cross updates and Circle goes back to the offer. Triangle opens them from the offer too.
- **Skip** keeps the version you have. The dialog shows again the next time ProsperoEden opens, not when you come back from a game.

If the download or the unpacking fails, the dialog says why and offers **Try again**; the app stays as it was. Without a network, or without an answer, nothing is shown. When ProsperoEden cannot install the release itself (no payload loader, or an install it cannot find), the menu says **Update available** at the top right for ten seconds instead, and the steps below still work:

- **Folder install.** Copy the `PPSA99008` folder from the new release ZIP over `/data/homebrew/PPSA99008`, replacing the files it has, then start ProsperoEden. Files you put there yourself, such as `language.txt`, stay.
- **Package image (`.ffpfsc`).** Delete the old image, copy the new one to the same place, then restart ShadowMountPlus (send its payload again, or restart the console) and start ProsperoEden. ShadowMountPlus 1.6 keeps the old image mounted until it restarts; 1.7 finds a replaced image by itself at its next scan, and a restart does no harm there. Keep one image only, and no `PPSA99008` folder next to it: two copies of the app are reported as a duplicate. The image install is still untested (see the [Roadmap](#roadmap)), so these steps follow how ShadowMountPlus handles images.

### Moving save data

Emulators like Eden keep a save as the files the game wrote, so nothing is converted: folders are copied. In the Library, press Triangle on a game and pick **Save data**. The folders below are next to `roms/` in the game files folder.

- **Import from a folder.** Copy the contents of the game's save folder (what an emulator opens as the game's save directory) into `save-import/<title ID>/`. The title ID is the 16-character code in the game's file name; Save data also shows it when there is nothing to import.
- **Import from Ryujinx.** Copy Ryujinx's data folder (the one that holds `bis/`, or a portable folder around it) to `ryujinx/`. ProsperoEden finds the game's save in it: the first user's, and the device save when there is one.
- **Export.** Square writes `save-export/<title ID>-<date>-<time>/`, with `account/` and `device/` inside. An exported folder can be imported again: put it in `save-import/` under the title ID.

Cross imports, and asks before it replaces a save. The save it replaces is first moved to `/data/prosperoeden/backup/save-import`, so nothing is lost.

### Mods

A mod changes a game: a patch to its code (`.pchtxt` or `.ips` files in an `exefs` folder), replacement game files (a `romfs` folder), or cheats (a `cheats` folder). Mods made for other emulators of the same console come in this layout.

- **Add a mod.** Each mod is a folder. Copy it to `mods/<title ID>/`, next to `roms/` in the game files folder, so that a patch ends up at `mods/<title ID>/<mod name>/exefs/<file>.pchtxt`. The title ID is the 16-character code in the game's file name. The Mods screen names the exact folder, and Square creates it. The About screen shows where the `mods` folder is.
- **Add cheats.** Cheats are text files in the layout that cheat collections for other emulators of the same console use: each cheat starts with its name in square brackets (`[Infinite health]`), followed by its code lines. Put the file at `mods/<title ID>/<mod name>/cheats/<build ID>.txt`, the same place as a mod (any mod folder name; a collection's own folder works as it is). A single cheats file can also go straight into the title's folder as `mods/<title ID>/cheat_<name>.txt`; it is listed as its own mod. The build ID is the code the file is named after in cheat collections: each version of a game has its own, so pick the file for the version you run (with its update in `updates/`). Folder names may be in any letter case.
- **Switch it on or off.** In the Library, press Triangle on the game and pick **Mods**. Every mod found is listed with a switch. A mod is on unless you switch it off, and a change applies the next time the game starts.
- **Switch all of a game's mods off or on.** In the Library, a game that has mods shows a **Mods** switch under its console mode; Square flips it. Off, the game starts without any of its mods, and each mod keeps its own switch for when you turn it back on.
- **Choose cheats one by one.** A cheats file can hold many cheats, as the collections made for a game do. When a mod lists more than one, the Mods screen shows each cheat under the mod with its own switch, and none of them runs until you switch it on. Cheats that replace each other, such as two frame rates or two resolutions, take each other's place: switching one on switches the other off. A mod with a single cheat runs it whenever the mod is on.
- **See what a game has.** The home screen and the Library count a game's mods next to its update and DLC (`Update 1.2.0, 2 DLC, 1 mod`), and say so when some are switched off (`1 of 2 mods on`).
- **Match the game's version.** A patch is made for one version of a game. One made for another version is ignored without a message, so check that the mod matches the update you have in `updates/`.
- **Frame rate.** A patch that makes a 30 FPS game run at 60 FPS works on the 60 Hz output ProsperoEden uses. A patch for more than 60 FPS needs the 120 Hz output: set **Refresh rate** to 120 Hz in the game's settings (Triangle in the Library) or in **Settings > Video**. That takes a display that shows 120 Hz and the PS5's own 120 Hz output setting; without them the game runs at 60 Hz. A 60 FPS patch can gain from it too: a game that misses some frames at 60 Hz has twice as many chances to show them. A patch for more frames than the output shows (240 FPS on the 120 Hz output, 120 FPS on the 60 Hz one) still runs at its own pace: the frames the display has no refresh for are left out.

ProsperoEden does not include or download mods.

### Performance settings

**Settings > Performance** trades accuracy for speed, for every game. A change applies when a game starts.

| Setting | In the file | Default | What it does |
|---|---|---|---|
| Compile ahead | `block_list` | off | The code a 64-bit game compiled in earlier sessions is compiled again on a spare CPU as it starts, so the game stutters less when it reaches that code. In the one large game it was measured in, the game loaded sooner and its first seconds of play held their frame rate, while the emulated processor worked about a tenth harder afterwards. It has run in one game on the console so far; switch it off if a game misbehaves. |
| Asynchronous shaders | `async_shaders` | off | A new effect is drawn once its shader is ready instead of pausing the game for it. Things can be missing for a moment. With the Vulkan renderer only: the OpenGL renderer ignores it. |
| Faster GPU emulation | `fast_gpu` | off | The emulator's lowest GPU accuracy. Faster in demanding games; graphics can be wrong. |
| Faster CPU emulation | `unsafe_cpu` | off | Less exact floating-point math. A few games misbehave. |
| Faster DMA | `unsafe_dma` | off | Less exact memory transfers to the GPU. A few games show wrong graphics. |
| Reactive flushing | `reactive_flushing` | on | Keeps what a game reads back from the GPU exact. Off is faster; some effects break. |
| Skip CPU invalidation | `skip_invalidation` | off | Skips some checks when a game changes memory the GPU uses. Textures can be stale. |

A game can have its own values: in its settings, **Performance** (see [Settings per game](#settings-per-game)). In the settings file, `"performance"` at the top level is what the switches set, and one inside a game's entry under `"games"` is that game's, whose values go first:

```json
{
  "performance": { "block_list": false, "async_shaders": false, "fast_gpu": false, "unsafe_cpu": false,
                   "unsafe_dma": false, "reactive_flushing": true, "skip_invalidation": false },
  "games": { "0100000000010000": { "performance": { "fast_gpu": true } } }
}
```

The accuracy switches help only where a game is held back by what they relax. In the scene they were measured in on the console, a large game standing at its frame rate limit, switching all of them on changed neither the frame rate nor how busy the emulated processor and the graphics thread were. Leave them off unless a game runs under its frame rate, and switch them back off if its graphics go wrong.

Resolution, the upscaling filter, the renderer and the refresh rate, which change speed too, are in **Settings > Video** and in a game's own settings.

### Profiles

**Settings > Profiles** lists the people who play on this console, up to eight. Each profile keeps its own save data, its own settings (everything under Settings, and each game's own settings) and its own Continue Playing and Recently Played. The game files folder is the console's and is the same for everyone.

- **Cross** plays as the highlighted profile. The home screen names who is playing once there is more than one profile.
- **New profile** adds one. It starts with the settings of the profile that made it, and with no save data.
- **Left and right** change a profile's name: the names of the PS5 users signed in, then "Player 1" to "Player 8".
- **Square**, pressed twice, takes a profile off the list. Its save data stays on the console.

The menu opens with the profile that the PS5 user in front chose last. If you used an earlier version, your saves, settings and recently played games are the first profile's, where they always were.

### Settings per game

**Settings** holds what every game uses. A game can differ from it: in the Library, press Triangle on the game. Its settings list **Video**, **Performance**, **Audio**, **Controls** and **Language**, and each one says how many of its settings the game changes. Inside, a setting shows **Default** with the value Settings has until you choose another one for this game; going past the last choice comes back to **Default**. A change applies the next time the game starts.

| Kind | What a game can have of its own |
|---|---|
| Video | Renderer, resolution, upscaling filter, refresh rate, FPS overlay |
| Performance | Each of the seven switches |
| Audio | Game volume, mute |
| Controls | Vibration, and a button mapping of its own |
| Language | The language the game is given |

The output resolution, the menu's sounds and the accessibility settings are the same for every game.

### Button mapping

**Settings > Controls > Button mapping** lists the game's buttons (A, B, X, Y, L, R, ZL, ZR, Plus, Minus and the two stick presses) with the DualSense button that presses each one. Left and right choose another DualSense button; when another game button had it, the two swap, so one DualSense button never presses two game buttons. Square puts every button back. The mapping applies to every controller.

A game can have its own: in its settings, **Controls**, set **Button mapping** to **This game**, then press Cross to change it. It starts as a copy of the one in Settings.

The D-pad, the sticks and the [in-game shortcuts](#in-game-shortcuts) do not change. With the usual mapping the touchpad is the game's Minus; the Create button presses whatever the touchpad presses, unless it has a game button of its own.

### Language and accessibility

The launcher follows the language the PS5 is set to: Arabic, Chinese (simplified and traditional), Czech, Danish, Dutch, Finnish, French, German, Greek, Hungarian, Indonesian, Italian, Japanese, Korean, Norwegian, Polish, Portuguese, Romanian, Russian, Spanish, Swedish, Thai, Turkish, Ukrainian and Vietnamese (with the regional variants the PS5 has for French, Portuguese and Spanish), and English otherwise. Arabic, Chinese, Greek, Japanese, Korean and Thai are drawn with the PS5's own system fonts. Arabic text runs right to left; the screens themselves are not mirrored. To use another one, put a file named `language.txt` holding its tag (for example `en-US` or `pt-BR`) in `/data/homebrew/PPSA99008`. The language *games* use is a separate setting, **Settings > Language**.

**Settings > Accessibility** has three switches. **Larger text** draws the menu's small text about a third larger. **High contrast** uses solid panels, brighter text and an outlined highlight. **Reduce motion** stops the background drifting and the screens sliding, in the menu and on the loading screen. There is no screen reader.

### Upgrading from an earlier alpha

Earlier versions read everything from `/data/homebrew/PPSA99008/assets/`. That folder keeps working until you choose a game files folder, and settings are migrated automatically on first launch. To move to the new layout, move `assets/keys`, `assets/firmware` and `assets/roms` into any folder, then select it in **Settings > Game files**. The release ZIP contains no user files, so copy its app files over your installation without deleting your own data.

## Changes in v1.000.080

- **Single Joy-Con games with the DualSense held as usual.** A game that has a single Joy-Con held sideways no longer needs the DualSense turned sideways: the stick, the four buttons and the motion sensors are turned to match, for each player. L1 and R1 stay SL and SR.
- **What's new in the update dialog.** When an update is offered, **What's new** shows that release's notes before you decide.
- **Start a game directly.** ProsperoEden takes `--rom <file>` and `--exit-after-game` as launch arguments, so a home screen forwarder can open one game; see [docs/FORWARDER.md](docs/FORWARDER.md).
- **Filesystem access on firmware 13.60.** The bundled helper is updated with a fix for that firmware.
- **Release ZIP opens with the right permissions.** Its files are stored open to all, as the console wants an app's files; unpacked with their permissions kept, they could give "Can't start the game or app" (CE-107750-0).

## Changes in v1.000.070

- **ProsperoEden starts wherever it is installed.** On a USB drive, extended storage, an etaHEN games folder or any other place ShadowMountPlus mounts it from, it could not find its own files and closed after a moment. It now reads its files from where the PS5 mounts the running app.
- **Updates from the menu work on those installs too.** The update helper updates the folder ShadowMountPlus mounted the app from; an app installed as an image is not changed, and the dialog says to replace the image.
- **Clearer error when the menu cannot start**, naming the file and the reason.
- **Start-up trace for bug reports.** Each start-up step is written to the kernel log (`[ProsperoEden diag]`) and to `/data/prosperoeden/logs/boot-trace.txt`.

## Changes in v1.000.060

- **Updates from the menu.** When a newer release is listed on homebrew.page, ProsperoEden offers it each time it opens. **Update now** downloads and checks it, unpacks it beside the app, then closes so the update helper can replace its files; **Skip** keeps the current version until the next time it opens. Your saves, settings and own files in the app folder are kept. See [Updating](#updating).
- **Lapy elevation included.** ProsperoEden carries its own one-shot helper built from upstream Lapy and sends it over the local ELF loader on port 9021; loading Lapy separately is no longer needed.
- **The elevation helper no longer crashes when ProsperoEden starts.** Its start-up runs on one thread, and it is built from a corrected upstream release.

## Changes in v1.000.050

- **Profiles.** **Settings > Profiles** lists the people who play on the console, up to eight. Each profile keeps its own save data, its own settings (everything under Settings, and each game's own) and its own recently played games. See [Profiles](#profiles). If you update from an earlier version, your saves, settings and games played become the first profile's, with nothing to do.
- **Button mapping.** Choose which DualSense button presses each of the game's buttons, for every controller, in **Settings > Controls**, or for one game in its settings. See [Button mapping](#button-mapping).
- **Settings per game.** A game's settings now have Video, Performance, Audio, Controls and Language; each value follows Settings until you choose another. See [Settings per game](#settings-per-game).
- **Performance settings.** **Settings > Performance** has seven switches that trade accuracy for speed, for every game or for one. See [Performance settings](#performance-settings).
- **Cheats chosen one by one.** A mod that lists several cheats shows each under it with its own switch, and only the chosen ones run. See [Mods](#mods).
- **Update notice.** When a newer release is listed on homebrew.page, the menu says so at the top right for ten seconds. Nothing is downloaded or installed. See [Updating](#updating).
- **Leaving a game takes ten seconds at most.** If a game has not stopped ten seconds after Touchpad + L1, or after it ended by itself, ProsperoEden starts again and opens at the menu. Some games took minutes to close.
- **Touchpad + L1 works while a game loads.** A load that hangs can be left; before, the only way out was closing ProsperoEden.
- **A game's controller screen always gets an answer.** A game that takes only the handheld controller gets it in Docked mode too, and a game that names no controller gets a Pro Controller. Both used to leave the game waiting.
- **Removed games leave the menu.** A game whose file leaves the game files folder leaves Continue Playing, Recently Played and the Library within two seconds, and is not started.
- **Touchpad by its name.** The menu and this document say Touchpad + L1 and Touchpad + R1.
- **The graphics driver's shader cache works again**, so shaders compiled in earlier sessions load when a game starts.
- **A crash when a game with cheats stops is fixed.**
- **Games start a little sooner**: the instruction decoder's tables are built from one reading of each pattern.
- The graphics driver is built from Mihawk's current PS5 Mesa and Vulkan.

## Changes in v1.000.040

- **The launcher in your language.** It follows the language the PS5 is set to, in 29 languages. Arabic, Chinese, Greek, Japanese, Korean and Thai are drawn with the console's own fonts. Text that runs longer in another language makes its own room instead of being cut.
- **Accessibility.** **Settings > Accessibility** adds **Larger text**, **High contrast** and **Reduce motion**. Warnings carry a mark as well as a colour.
- **Save data in and out.** Import a game's save from a folder or from a Ryujinx data folder, and export a copy. See [Moving save data](#moving-save-data).
- **A ten-second slowdown with the AMD FSR filter is fixed.** In a large open-world game, gameplay could start at 3-9 FPS for about ten seconds, and frames of about a tenth of a second kept coming afterwards. The texture cache was throwing away images the GPU had drawn, with a wait for the GPU each time, as soon as memory use passed a mark that the FSR filter's own images pushed it over. It now keeps them until graphics memory is really short.
- **Smoother heavy scenes.** The renderer hands its work to the Vulkan worker in larger batches, and the emulated cores wait less on the GPU caches' locks. In the heaviest area of a test walk this removed drops to 26-28 FPS and cut the GPU thread's work by about a quarter.
- **Touchpad as Minus in games.** A tap of the touchpad presses the game's Minus button, and a longer press holds it. The Touchpad + L1 and Touchpad + R1 shortcuts never reach the game as a press.
- **A crash a few seconds into some games is fixed.** The motion sensors' updates could reach a part of the controller service that was not set up yet.
- **Games' own system screens.** A game's error dialog, profile picker and similar screens use Eden's built-in versions, and its error dialog now answers the game instead of leaving it waiting. The firmware's versions could end a session with an out-of-memory error.
- **More graphics memory.** The PS5 gives an app one pool of memory that the CPU and the GPU share, and ProsperoEden held about 3 GiB of it without using it: its heap took 3 GiB at start and the emulated console's page table 1 GiB. Both now take memory as they need it, which gives graphics about 2.6 GiB more in the largest game tested. That game ran out of graphics memory while loading at 2x; it now runs at 2x with the AMD FSR filter at a steady 30 FPS, with about 2 GiB to spare. The texture cache also measures its memory use from what is really left of the pool, instead of a driver figure that counted every allocation twice.
- **A clearer message when a game runs out of graphics memory.** If a game still needs more than there is at a high resolution, give it its own lower one: Triangle on it in the Library, then **Resolution**.
- **Games that accept only single Joy-Cons** now get one, and L1 and R1 are its SL and SR buttons, which those games ask for on their controller screen.
- **Mods.** A game's patches, replacement files and cheats load from `mods/<title ID>/` next to `roms/`, and the game's settings list them with a switch each. `.ips` and text `.pchtxt` patches, which the emulator core could not apply, now work. The Library has one switch per game for all of its mods. See [Mods](#mods).
- **120 Hz output.** **Refresh rate** in **Settings > Video**, and in each game's settings, asks the display for 120 Hz while a game runs. A display that cannot show it stays at 60 Hz, and the menu always runs at 60 Hz. After a game at 120 Hz the menu takes about five seconds to come back while the display changes rate.
- **Output resolution.** **Settings > Video** has a new **Output resolution** row: 1080p (as before), 1440p or 2160p. It is the size of the picture ProsperoEden puts out, for the menu and for games. Until now a game's picture was made at 1080p and enlarged, whatever its internal resolution; at 2160p the upscaling filter scales the game straight to a 4K picture. A larger picture needs more graphics memory.
- **3x and 4x resolution.** The internal resolution now goes up to 4x. These need far more graphics memory than 2x; a game that runs out says so, and can be given its own lower resolution.
- **Patches for more frames than the display shows.** A game patched to run faster than the output refreshes keeps its pace: the frames the display has no refresh for are left out.
- **Crash reports.** If ProsperoEden stops because of an error, it writes `crash-<date>-<time>.txt` to `/data/prosperoeden/logs`, starts again and says on the home screen where the report is. That session's logs are kept beside it, and the five newest reports stay. A report holds what failed, where in the app's code, and what was running; it never holds the contents of memory.
- The launcher and the OpenGL renderer use the PS5 OpenGL 4.6 SDK 1.0.0.
- The graphics driver's shader cache moved to `/data/prosperoeden/cache`, so a read-only package install keeps it.
- **Experimental: block list.** With an empty file named `block-list.txt` in `/data/homebrew/PPSA99008`, ProsperoEden saves which code a 64-bit game compiled and compiles it again on a spare CPU when the game next starts. In a repeat session of a large open-world game, the emulated cores then compiled 426 blocks during play instead of 138,262, and gameplay started at 30 FPS instead of 22. It is off by default until more games have run with it.

## Changes in v1.000.030

- **New launcher.** Every screen is redrawn with OpenGL: smooth transitions between screens, text that stays sharp, panels that blur the artwork behind them, and sound effects for moving, selecting and going back. **Settings > Audio > Menu sounds** sets their level.
- **Controllers on the home screen.** Four controller icons show which controllers are connected, and change as one joins or leaves.
- **Loading screen.** An animated scene shows while a game starts, on both renderers.
- **The Library opens at once.** The game list is read in the background, and the home screen shows each game's own name instead of its file name.
- **New music** on the PS5 home screen.
- **Smoother first-time gameplay.** The emulated CPU cores now share the code they compile, so each part of a game is compiled once instead of once per core. In large open-world games this halves the compile work and removes most of the stutter when gameplay starts or a new area loads.
- **Faster compiling.** Compiling a game's code now takes about 40% less CPU time, which shortens the remaining stutter when gameplay starts or a new area loads.
- **Startup hang fixed.** A game could stop for good right after starting, because the emulator's memory allocator could leave high-priority threads waiting on each other forever. Development builds also report where a slow start is stuck.
- **Games close in about a second.** Touchpad + L1 used to take 10-25 seconds to return to the library; logging no longer waits on the console's storage, and the emulator skips needless teardown work.
- **Up to four controllers.** Each signed-in PS5 user's controller becomes the next player (player 1 is whoever launched the game); controllers can join or leave during a game, and games that ask for controllers connect every one in use.
- The Touchpad + L1 and Touchpad + R1 shortcuts work from any controller.
- **Vibration and motion.** DualSense rumble for games that use it (turn it off in **Settings > Controls**), and the controller's gyro and accelerometer for motion controls.
- **Resolution and upscaling.** **Settings > Video** now sets the internal rendering resolution (0.5x to 2x) and the filter that scales it to the TV: Bilinear, AMD FSR, Bicubic or Nearest.
- **Per-game settings.** Press Triangle on a game in the Library to give it its own renderer, resolution and upscaling filter.
- **Language setting.** **Settings > Language** picks the language games use (18 languages), with the console region that goes with it. It applies when a game starts.
- **Game updates and DLC.** Put update and DLC files (NSP or XCI) in the `updates` folder next to `roms`; the newest update and all DLC apply when the game starts, and the game details show them.
- **Build it yourself.** `make` fetches every dependency at its pinned revision and builds the release. The OpenGL renderer now uses the published PS5 OpenGL 4.6 SDK 0.6.0.

## Changes in v1.000.020

- **Vulkan is now the recommended and default renderer.** OpenGL remains available in Settings.
- **Choose where your game files live.** The new **Settings > Game files** browser selects any folder, including external drives and USB devices. The old `assets/` folder keeps working until you choose one.
- **App data moved to `/data/prosperoeden`.** Config, logs, covers, and saves now live outside the app folder, so updating the app never touches them.
- **Settings are now stored in one JSON file.** Earlier text settings are migrated automatically on first launch.
- **Faster Library navigation**, and covers and recent games that survive folder moves.
- **New icons** for controller actions, pages, and the Handheld / Docked mode.

**Known issues:** Ending a game with Touchpad + L1 can take several seconds. Some games can still hang on the loading screen, and some demanding games remain slow. This is a testing pre-release, not a compatibility guarantee.

## In-game shortcuts

The touchpad is pressed as a button. On its own, a tap of the touchpad presses the game's Minus button (with the usual [button mapping](#button-mapping)), and a longer press holds it; a shortcut never reaches the game.

| Shortcut | Action |
|---|---|
| Touchpad + R1 | Toggle the performance HUD |
| Touchpad + L1 | End the running game and return to the library |

## Roadmap

- **FPKG support** - install ProsperoEden as a fake package, alongside the current homebrew folder install. Each release includes a ShadowMountPlus package image (`.ffpfsc`); installing it still needs testing.
- **More performance** - CPU and GPU work to keep demanding games at their target frame rate: the short stutter when a game reaches new areas (the block list, **Compile ahead** in **Settings > Performance**, once it has run in more games and can be on by default), heavy cutscenes, and games that run slower in Docked mode than in Handheld.
- **More reliable game loading** - a game that hangs while it loads can now be left with Touchpad + L1; the hangs themselves still need a log from a game that does it.
- **Game names that show as "?"** - a game can be listed with a question mark for its name while its cover and file are fine. The Library shows the first name the game carries, in whatever language and whatever it holds. Use the name in the menu's language, skip a name that is empty or has nothing the fonts can draw, take the name from an installed update when it has one, and fall back to the file name.
- **Controller type per game** - some games only work with a Handheld controller or Joy-Cons, not the Pro Controller every player gets today. Add a **Controller type** choice to each game's Controls: Pro Controller (the default), Handheld, Dual Joy-Cons, Left Joy-Con or Right Joy-Con.
- **Real Joy-Cons and Pro Controllers (investigation)** - play with the original controllers connected to the PS5. The PS5 does not pair them itself, so this first needs to find out whether a homebrew app can read them: a wired Pro Controller over USB looks more likely than Joy-Cons, which only connect over Bluetooth.
- **Typing text in games** - when a game asks for text (a character's name, an answer during an event), show a keyboard instead of answering with the game's default text as now: the PS5's own on-screen keyboard if a homebrew app can open it (it also takes a USB or Bluetooth keyboard the PS5 supports), otherwise one of our own. A USB keyboard's keys could also be mapped to the game's buttons, like the DualSense's.
- **Multiplayer with Eden on other systems (investigation)** - join Eden's multiplayer rooms from the PS5 and play with Eden players on PC, Linux and Android, entering a room's address by hand, including a room hosted on your local network (no PSN needed). Rooms only accept the same Eden version on every side, so each release would name the matching PC version.
- **DualShock 4 controllers** - use DualShock 4 controllers as players too, next to the DualSense, so multiplayer games do not need four DualSense controllers.
- **Better OpenGL performance** - make the OpenGL renderer faster, and add tuning options for it.
- **More game compatibility** - validate more games on the PS5, and fix what keeps them from running well, such as games that crash at launch.

## Issues are disabled

GitHub issues are turned off for this repository on purpose. ProsperoEden is a general-purpose emulator port, and the project does not host discussion of console makers, specific commercial games, compatibility reports, or where to find game files. Issue threads tend to fill up with exactly that, so there are none.

Please do not use pull requests or other channels to post that kind of content either.

## Elevation credits

Filesystem elevation uses [PS5-Lapy-JB-Daemon](https://github.com/ArkSama/PS5-Lapy-JB-Daemon),
created by ArkSama. ProsperoEden follows the cooperative owned-root design and implementation
from [ProsperoEden's Lapy fork](https://github.com/blackbearreloaded/PS5-Lapy-JB-Daemon), based on
[mpereiraesaa's cooperative helper](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon). The build
pins that source, builds its exact-title one-shot helper without local payload changes,
verifies the upstream-generated manifest, and includes the resulting ELF in the release.
The donor-release and firmware 13.60 fixes pinned here were merged upstream in
[PS5-Lapy-JB-Daemon PR #48](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon/pull/48) and
[PR #49](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon/pull/49).
The previous `54a095c` helper passed five automated launch/elevate/close cycles on firmware 6.02
and five on 12.70 with root access, balanced donor references, clean helper exits, and no fatal
signal, app crash, coredump, nonsleeping-lock warning, or kernel panic in the captured kernel-log
windows. The new `c3bdfe3` helper still requires attended qualification runs.

## Thanks

Thank you to the people who test ProsperoEden on their own consoles and report what they find:

- **Jones** ([X](https://x.com/Jonesskulls), [GitHub](https://github.com/AgentJonesy)), for helping with testing.
- **Phronesis**, for helping with testing.
- **szampan** ([GitHub](https://github.com/heni0xyz)), for helping with testing.
- **BunkinBacon**, for helping with testing.
- **Cold** ([GitHub](https://github.com/JMUtechnologies)), for helping with testing.

<!-- bbr-footer:start -->
<!-- Generated by ps5-homebrew-dev-protocol/scripts/readme-footer. Edit the template there, not here. -->

## Credits

Built with the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) by John Törnblom (ps5-payload-dev).
Third-party components, authors and licenses are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

Copyright © 2026 BlackBearReloaded. Licensed under GPL-3.0-or-later; see [LICENSE](LICENSE). Third-party components keep their own licenses. Binary releases are built from the tagged source in this repository.

## Disclaimer

- **No affiliation.** This is an independent homebrew project. It is not
  affiliated with, endorsed by, or sponsored by Sony Interactive Entertainment.
  "PlayStation", "PS5" and related marks are trademarks of Sony Interactive
  Entertainment Inc. This project is not affiliated with or endorsed by the Eden project.
- **No proprietary material.** No Sony SDK, firmware, encryption keys or
  decrypted system modules are included.
- **No warranty.** This project is provided "as is", without warranty of any
  kind, to the extent permitted by law. See sections 15 and 16 of the GPL.
- **Use at your own risk.** Running homebrew requires a modified console, which
  may void its warranty, breach the platform's terms of service, or cause data
  loss.
- **Legal use only.** Use it only with hardware, accounts and content you own.
  This project does not support or enable piracy.

## AI assistance

This project was developed with AI assistance from OpenAI and/or Anthropic tools.
<!-- bbr-footer:end -->
