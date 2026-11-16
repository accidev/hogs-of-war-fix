# Hidden and cut content of Hogs of War

**English** · [Русский](hidden-content.ru.md)

Hogs of War shipped with more inside than a player can reach: two pig classes above the Hero, a test pig that carries every big toy, five maps that are in no list, a boat without a model, a demo that never plays and a sheep the game refuses to load. This page lists what we found in the Windows game (version 1.2, the Steam release) while working on the [fix](../README.md).

Everything here was read from the game's program and data files. Things a player cannot reach were not started in the game, so we often know that something exists and not what it looks like. Where a statement comes from the code only and nobody tried it, the text says so.

- [Cheat names](#cheat-names)
- [Ace and Legend](#ace-and-legend)
- [The developers' test pig](#the-developers-test-pig)
- [Weapons](#weapons)
- [Vehicles and objects](#vehicles-and-objects)
- [Maps nobody sees](#maps-nobody-sees)
- [The demo that never plays](#the-demo-that-never-plays)
- [Debug leftovers](#debug-leftovers)
- [PlayStation and golf leftovers](#playstation-and-golf-leftovers)
- [Videos, music, sounds, pictures](#videos-music-sounds-pictures)
- [The developers in their own game](#the-developers-in-their-own-game)
- [What we did not check](#what-we-did-not-check)

## Cheat names

The game checks the name of your **team**, not the name of a pig. Start One Player → New Game, choose a nation and type one of these names when the game asks you to name your team.

| Team name | What it does |
|---|---|
| `NAUGHTY PIGS` | The team has 99 promotion points every time its team screen opens. Start Mission opens a menu named "Cheat Level Select" with all 26 levels: the training and the 25 missions. Its help text reads "Due to some naughtiness you can choose which level to play" |
| `MARDY PIGS` | The team becomes the purple Team Lard with eight new pigs. It does not count as a team that has finished the campaign |
| `PRYING PIGS` | A cinema. Start Mission plays every story video in a row and then returns to the menu; no battle starts. We read this from the code and did not try it |

- The game gives these names itself: a team that has finished the campaign once is renamed MARDY PIGS, and after the second time PRYING PIGS.
- A translated game compares with the translated line. In the Russian translation the three names are `ПАРШИВЦЫ`, `МАРДИ` and `ЗАНУДЫ`.
- Five more names are in the same list and do nothing in the PC game, because no code reads them: `LAZY PIGS`, `GREEDY PIGS`, `SPEEDY`, `WATTA PORK` and `DEMO PIGS`. Cheat lists for the PlayStation version give WATTA PORK as a bonus video.

## Ace and Legend

The game has 17 pig classes, and the team screen shows 15 of them. Above the Hero there are the **Ace** and the **Legend**.

- The Legend appears once in the original game: the five enemy commanders of the last mission.
- The Ace is on no map. Its name is in the battle text, its row is in the class table, and nothing ever uses it.
- The promotion list already has the entries Hero → Ace and Ace → Legend. Their price is 0 and their counters are switched off.
- In the game's own table the Ace is not an upgrade. It has the 150 health of a Hero, gets Jetpack ×3, F-Rain Airstrike and Tranquiliser, and loses the Sniper Rifle, Hide and the Medicine Dart. The Legend is an Ace with 200 health and an unlimited Shockwave.

The fix opens this promotion and makes both classes real upgrades; see "Ace and Legend" in the [README](../README.md).

Multiplayer has a leftover of its own: the list of multiplayer class names ends with HERO, and the class selector never offers it.

## The developers' test pig

After the campaign and multiplayer classes the class table has one more row. It describes a pig with 50 health and fifteen weapons, all with unlimited ammunition:

Bayonet, Cluster Grenade, Super Shotgun, Super TNT, Guided Missile, Shrapnel Grenade, Rifle Burst, F-Rain Airstrike, Special Ops, Airstrike, Heavy M-Gun, Flame Thrower, Airburst, Poison Gas, TNT.

A flag makes the game use this row instead of a pig's own class. The game only ever writes 0 to that flag, so nobody has played with this pig since the testers went home.

## Weapons

The weapon list has 67 entries: weapons, skills and commands such as Skip Turn.

| Weapon | State |
|---|---|
| Confusion Gas | Cut. It has a name and a place in the list, and the game has no code to throw it. No class, crate or enemy gives it |
| Map View, Surrender | Names in the list. We found no code that gives or uses them |
| Binoculars | The same, although its entry even points to a telescope model for the hand |
| Freeze Gas, Madness Gas, Roller Grenade | They work, and the campaign never gives them. Multiplayer crates have them: Freeze Gas on Death Valley and Chill Hill, Madness Gas on Death Bowl and Cratermass, Roller Grenade on Bridge the Gap and Frost Fight. Crates with random contents can hold them too |
| Grenade Launcher | Only the multiplayer classes carry it. No crate has it |
| Rocket Launcher, Guided Missile, Homing Missile, Super Airburst, Anti-P Mine, Super TNT | No class has them: they come from crates and from what enemies and buildings drop. The campaign has a single Super Airburst crate (Under Siege) and a single Anti-P Mine crate (Glacier Guns) |
| Shockwave | Among the classes only the Legend has it, and the original game has Legends only as the enemy of the last mission. The list of random crate contents has it too |
| Long Range, Mine, Gas, Fire Rain, 1000 lbs and Shock Shell | The six shells of the Artillery gun |

Four models for a pig's hand belong to no weapon: a spade (`WE_SPADE`), a radio (`WE_RADIO`), `WE_AUTP` (by its name an automatic pistol) and `WE_HEG` (a high-explosive grenade; the H-Expl. Grenade of the game uses the ordinary grenade model).

## Vehicles and objects

The battle text names seven vehicles. Not all of them made it.

| Name in the game | Model | State |
|---|---|---|
| Tank | `TANK` | Just Deserts (mission 12) and random levels |
| Aqua-Tank | `AM_TANK` | Random generated levels only |
| Duck | `AMLAUNCH` | One, in Fortified Swine (mission 15) |
| Truck | `CARRY` | Two, in Chemical Compound (mission 19) |
| Airship | `AIRSHIP1` | The aircraft of the airstrikes. No map has one parked |
| Boat | `BOAT` | Cut: the name is in the model list, and no map has the model |
| Plane | `PP1` | Cut in the same way |

Other objects that stayed in the files:

- **Bunker.** `BUNKER` and its guns `BUNKGUN` and `B_GUN` are names without models.
- **Sandbags.** The text has the name SANDBAGS and five map packs have a model `SAND_B`. The game does not know that model name and skips it.
- **A third crate.** `CRATE3` is in the same five packs and is skipped in the same way. The game uses `CRATE1`, `CRATE2` (health) and `CRATE4`.
- **A sheep.** `SHEEP1` stands twice on the map EASY, and its model is in that map's pack. The game does not know the name, so the sheep would not appear even if the map could be started.
- **Birds.** `BYRD` and `BYRD3` are models in ten map packs. No map places them.

## Maps nobody sees

The game folder holds 61 maps. 26 belong to the campaign (the training and 25 missions), 24 to multiplayer and 6 are the themes of the random level generator. Five are in no list:

| File | What is on it | Why you never see it |
|---|---|---|
| `DEMO` | Desert, 7 Grunts, 10 crates, palm trees | It is entry 26 of the campaign table, right after the last mission. The mission counter stops before it |
| `DEMO2` | Snow, 10 Grunts, pines, huts, oil drums | The battle text has a name for it, "DEMO LEVEL", and no list has the map |
| `DVAL` | 10 Grunts and 38 crates | The first Death Valley. Multiplayer shows `DVAL2` under the name "Death Valley"; the text "Death Valley 2" is never used |
| `EASY` | 7 Grunts, bridges, iron gates, two sheep | The program does not name this map at all |
| `HILLBASE` | 5 Grunts, 5 heavy weapons pigs, two pillboxes, tents and shelters, no crates | The same |

<details>
<summary>The file names of all levels</summary>

Campaign, in the order of play:

| # | Mission | File |
|---|---|---|
| – | Boot Camp (training) | `CAMP` |
| 1 | The War Foundation | `ESTU` |
| 2 | Routine Patrol | `ROAD` |
| 3 | Trench Warfare | `TRENCH` |
| 4 | Morning Glory! | `DEVI` |
| 5 | Island Invasion | `RUMBLE` |
| 6 | Under Siege | `ZULUS` |
| 7 | Communication Breakdown | `TWIN` |
| 8 | The Spying Game | `SNIPER` |
| 9 | The Village People | `MASHED` |
| 10 | Bangers 'n' Mash | `GUNS` |
| 11 | Saving Private Rind | `LIBERATE` |
| 12 | Just Deserts | `OASIS` |
| 13 | Glacier Guns | `FJORDS` |
| 14 | Battle Stations | `EYRIE` |
| 15 | Fortified Swine | `BAY` |
| 16 | Over the Top | `MEDIX` |
| 17 | Geneva Convention | `BRIDGE` |
| 18 | I Spy... | `DESVAL` |
| 19 | Chemical Compound | `SNAKE` |
| 20 | Achilles Heal | `EMPLACE` |
| 21 | High and Dry | `SUPLINE` |
| 22 | Assassination | `KEEP` |
| 23 | Hero Warship | `TESTER` |
| 24 | Hamburger Hill | `FOOT` |
| 25 | Well, Well, Well! | `FINAL` |

Multiplayer:

| List | Levels |
|---|---|
| Survival-Novice | Play Pen `PLAY1`, Duvet Fun `PLAY2`, Pigs in Space `LUNAR1`, Skulduggery `HELL3`, Pigin' Hell `HELL2`, Graveyard Shift `CREEPY2` |
| Survival-Expert and Death Match | Ice-Flow `ICEFLOW`, You Hillock `ARCHI`, Death Bowl `DBOWL`, Frost Fight `MLAKE`, The Lake `LAKE`, Chill Hill `ICE`, Square Off `SEPIA1`, One Way System `ONEWAY`, Ridge Back `RIDGE`, Island Hopper `ISLAND` |
| Survival-Strategy | Friendly Fire `BOOM`, Bridge the Gap `LECPROD`, Dam Busters `BHILL`, Moon Buttes `BUTE`, Hedge Maze `MAZE`, Cratermass `CMASS`, Doomed `ARTGUN`, Death Valley `DVAL2` |

</details>

The random level generator (Multi-Player → Generate) is not hidden; the manual describes it. Two details are easy to miss. Its six themes are plain text files, `Maps\GEN*.gen`, each signed "by Paul Tapper". And it is the only place where an Aqua-Tank can appear.

## The demo that never plays

The game still contains a demo that plays by itself: two computer teams fight on One Way System, Ice-Flow, Doomed, Graveyard Shift or Play Pen, with the words "DEMO MODE" and "PRESS ANY KEY" on the screen. The switch for it is a variable that the game only ever sets to 0, and the title screen has no idle timer, so it never starts.

Two more leftovers of a demo version: the team name DEMO PIGS and a help text that says "In the full version of the game you will be able to play with up to 4 human or computer players".

## Debug leftovers

- **A debug camera.** The turn has about twenty states, and the program holds a name for each. State 9 is "MANUAL CAM MODE - debug camera", described elsewhere as "MANUAL CAMERA MODE. FOR DEBUGGING PURPOSES ONLY". Its code is there. We found nothing that enters this state.
- **The other state names** are worth a look too: "CAN'T SELECT ANY PIGS THIS TURN...SORRY...", "PIG SELECTION MODE - R1 changes, X selects", "DOING SOFT RESET PSX".
- **Menu overlays** "DEBUG STATE: %d" and "DEBUG MOVE : %d, %d" wait for flags that nothing sets.
- **Debug messages** are all sent to a function that does nothing. Among them: "Playing speech clip %s", "AI Pick skill speech" and "The Network class is fucked".
- **A studio address.** If the network address field is empty, the game connects to `10.11.11.116`.
- **Menus we found no way into:** Screen Pos (the "Adjust Screen" of the console version), "Network: Hosting New Game" and "Network: Choose Army".
- **Animation names.** A list of 59 names that no code reads: "Run cycle (wounded)", "Swimming like Rick", "Lord Flash-Heart Pose", "Looking gormless", "Bouncing on B-Hind", "Hari Kiri".
- The game reads no command-line switches, and we found no debug keys.

## PlayStation and golf leftovers

- The PC text still says "CONTROLLER REMOVED", "PLEASE RE-INSERT", "VIBRATION ON" and names the multitap ports 1A to 2D.
- The game has 16 control actions, one for each button of a PlayStation pad. Its Controls screen shows 12 of them.
- The program's list of renderer functions includes `CreateGolfer`, `WaterGolfer` and `InitialiseDisplayGolfers`, and its error messages include "Illegal Golfer". The renderer was evidently shared with a golf game.

## Videos, music, sounds, pictures

- **Two missing videos.** The game plays `FMV 02` right after the intro and `FMV 09` after the ending. In the Steam release both files are 1 byte long, so nothing is shown. We do not know what was in them.
- **Music.** `Track32` is a copy of `Track31`, and the game never asks for it.
- **Skies.** Nothing reads the folder `Skys` with its 39 sky sets. The skies of the game come from the domes in `Chars`.
- **Night sounds.** The night ambience files `AMB_1N` and `AMB_2N` are never loaded: the night sound list is a byte-for-byte copy of the day list.
- **Menu sounds.** 37 files `Indu…` in `FESounds` are named nowhere.
- **Speech.** Every pig voice has 30 lines in 6 accents. The game also has a code for a seventh, Team Lard's own, and no files for it.
- **Pictures** that nothing loads: `Hgren.BMP` and `Hpara.BMP` (by their names, icons of the Grenadier and the Paratrooper) and `deathm2.bmp`, a second layout of the deathmatch result table.

## The developers in their own game

- The pigs of Team Lard are named after the team that made the game: Simon, Tapper, John, Andy, Mark, Jake, Martyn, Izzy, Ricki, Ade, Ben, Jim, Paul and others.
- In the last mission the enemy pigs get names of their own. One is IZZY. The others come from a list in the style of the six nations, and the game skips the name of your nation: SAINT NICK, LE RENARD, BIG-MAT, YAKOBSKI, TAPAZUKI, HERR GUERIN. "Le renard" is French for "the fox", and the lead programmer of the game is Andrew Fox.

## What we did not check

- How the five unlisted maps play, and whether they start at all.
- What exactly Freeze Gas and Madness Gas do to a pig: we did not read their effect from the code.
- What the videos `FMV 02` and `FMV 09` showed, and whether older releases of the PC game had them.
- Which voice the pigs of Team Lard use.
- How the debug camera is entered.
- How the PlayStation version differs. Everything here is about the PC game.
