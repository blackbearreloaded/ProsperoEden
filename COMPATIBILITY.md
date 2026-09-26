# Game compatibility

Community-maintained results for **ProsperoEden on PS5**, not desktop or Android Eden.
Last updated: **September 25, 2026**.

> [!IMPORTANT]
> These are early, limited tests. Recent development results informed v1.000.010, but the final release ZIP has not been separately console-tested. Older observations are labeled below. Reaching a menu does **not** establish playable gameplay or a completed game.

## Results

| Game | Grade | FPS |
| --- | --- | --- |
| Hollow Knight | B — Playable in tested sections; first-use stutter observed | Up to 60 observed in earlier builds; not a sustained benchmark |
| Horizon Chase Turbo | Intro/menu only — gameplay unverified | 33–36 in main menu |
| Mario Kart 8 Deluxe | C — Runs in-game, below target speed | 39.50 average in a short docked gameplay window; not sustained 60 |
| Mario vs. Donkey Kong | Intro/menu only — gameplay unverified | 30 at title screen |
| Metroid Dread | Intro/menu only — gameplay unverified | 59–60 at title/file selection |
| Pokémon Legends Z-A | D — Not playable in tested state; shutdown crash | About 5.5 at language selection |
| Summerhouse | Limited historical result — current build needs retesting | About 30 previously reported after startup; gameplay scope unverified |
| Super Mario 3D World | Intro/title only — gameplay unverified | 35–53 at animated title |

## How to read the grades

- **A — Excellent:** extended gameplay tested with no significant known issues; report the tested scope. No current entry qualifies for this grade.
- **B — Playable:** gameplay works in the tested sections, with some limitations. This does not imply a full playthrough.
- **C — Runs with major issues:** reaches gameplay, but speed, rendering, or stability significantly affects play.
- **D — Not playable:** fails to reach usable gameplay or has a blocking problem. This can include a game that boots.
- **Intro/menu only:** reaches an intro, title, or menu; gameplay has not been qualified.
- **Limited historical result:** an older observation without enough current-build evidence for a gameplay grade.

FPS alone does not determine the grade. A fast menu can coexist with slow or broken gameplay. Reported ranges are observed values, not guaranteed minimums or maximums unless explicitly measured that way.

## Test context and known limits

The recent development checks used **PS5 firmware 6.02 and OpenGL**. These results do not establish compatibility with other firmware or a Vulkan backend.

- **Hollow Knight:** the owner confirmed gameplay, audio, controller input, and saving in earlier builds. First-use spikes and variable speed were also reported. No full-game completion or controlled sustained 60 FPS result is recorded here.
- **Mario Kart 8 Deluxe:** 39.50 FPS is the average over a 20-second docked gameplay window in an accepted development candidate. It is a single, unreplicated result, not an all-course average or a minimum. Stable 60 FPS remains unresolved.
- **Horizon Chase Turbo, Mario vs. Donkey Kong, Metroid Dread, and Super Mario 3D World:** recent title/menu checks showed successful return or shutdown. This does not establish repeated game switching or long-session stability.
- **Pokémon Legends Z-A:** slow language selection and a repeatable shutdown crash remain unresolved.
- **Summerhouse:** the earlier startup/display result needs a fresh, scene-specific check on the public release.

Mode was not recorded in this summary for titles other than Mario Kart; do not infer handheld or docked mode from their FPS. Repeated switching between games can still expose stability problems across the app.

## Contribute a result

**Pull requests are welcome!** Edit this file to add a game or update an existing result. Keep the table alphabetized with exactly **Game, Grade, FPS** columns; add longer context below it instead of adding columns.

In your PR, include:

- ProsperoEden version (or development commit), PS5 firmware, game version/update, and handheld or docked mode.
- Scene tested and duration: menu, gameplay, or an extended session. State whether FPS is an average, range, peak, or a rough HUD observation.
- Any rendering, audio, controller, saving, crash, or return-to-menu issues. Mention whether you tested switching games.
- A concise reproduction description and, when available, a screenshot or sanitized log supporting the result.

Do not attach games, keys, firmware, saves, or logs containing personal data. Do not copy results from other platforms into this PS5 table. Preserve conflicting results with their build/mode context until they can be reconciled.

A single Markdown table is enough for now. If the list becomes cumbersome, it can be split alphabetically while keeping this page as the index.