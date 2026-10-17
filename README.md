# Remix Mode for the Silent Hill Native PC Port

A randomizer-style add-on for the [Silent Hill Native PC Port](https://github.com/SlickAmogus/silent-hill-decomp). Every seed gives you a new way through the game: rebuilt building layouts, moved items and keys, a different starting gun, and new monsters in each area. The story stays intact, so a full playthrough always works.

> \*\*Unofficial.\*\* Remix is a fan-made add-on for the PC port. It is not part of that project and not affiliated with its developers or with Konami. Report Remix problems here, not to the PC port's issue tracker.

**Original source:** this add-on is built on, and needs, the source of the Silent Hill Native PC Port:
**https://github.com/SlickAmogus/silent-hill-decomp**
That port is itself built on the [Silent Hill decompilation](https://github.com/shdecompilations/silent-hill-decomp) and uses [PsyCross](https://github.com/SlickAmogus/PsyCross). All credit for the game port goes to those projects.

\---

## Features

Every mode has its own on/off switch, and they all share one **seed** (1 to 99,999,999). The same seed always gives the same game, so you can share a seed with friends.

* **Remix rooms:** the interiors (both schools, the hospital in both worlds, and Nowhere) get a new layout. Each door leads somewhere new, and every room stays reachable, with no dead ends. In the schools and the Otherworld hospital, a door can even lead to another floor.
* **Remix items:** health drinks, first aid kits, ampoules and ammo move to new spots and change type within their kind. This covers the streets, the resort and the sewers too.
* **Remix keys:** key items move to other spots in their own part of the game, so each one is always reachable when the original was. A few keys can even turn up in another area.
* **Remix starter weapons:** you start with a random gun (Handgun, Shotgun or Hunting Rifle) with its ammo, instead of Cybil's Handgun.
* **Remix weapons:** every weapon found in the world turns up at a random spot.
* **Remix unlockables:** the bonus items (Katana, Channeling Stone, Chainsaw, Rock Drill) are hidden at random spots, still only once you've unlocked them.
* **Remix enemies:** each regular monster spawn becomes a random monster that fits the area. Monsters keep their own sounds, and the bites and grabs work wherever they show up. Bosses and scripted encounters stay as they are.
* **Kept items:** pick any items that should always stay in their normal spots.

Remix settings are in **PC Settings → Remix** in-game, and in the **Remix launcher** (`SilentHillPC\_Remix.exe`), which has the full set of options. Settings are saved in `remix.cfg`, next to the game's `config.cfg`.

With every Remix switch off, the game plays exactly like the normal PC port.

\---

## Requirements For Release Files

* **A legally obtained copy of Silent Hill for PlayStation.** No game data is included here or in the PC port.
* Download this version of the Silent Hill Decomp Files [SHPC\_beta-2026.10.12.1.zip](https://github.com/SlickAmogus/silent-hill-decomp/releases/download/crossplatform-beta/SHPC_beta-2026.10.12.1.zip)



## Installation

#### 1\. Get the Files

* Download from here [SHPC\_beta-2026.10.12.1.zip](https://github.com/SlickAmogus/silent-hill-decomp/releases/download/crossplatform-beta/SHPC_beta-2026.10.12.1.zip)
* Download the latest files from the Release page



#### 2\. Extract Files

* Extract the SHPC\_beta-2026.10.12.1.zip anywhere you please.
* Extract the Releases\*.zip file into the game's folder.
* Overwrite or rename any files it says to.
* Add your image.bin file into the gamedata folder.



#### 3\. Launch it

* Launch SilentHillPC\_Remix.exe
* Choose your settings
* Press Save \& Play
* Enjoy



## Requirements For Building Source Code

* **A legally obtained copy of Silent Hill for PlayStation.** No game data is included here or in the PC port.
* **Windows** with **MSYS2** (https://www.msys2.org/). In an "MSYS2 MinGW x64" shell, install:

```
  pacman -S --needed mingw-w64-x86\_64-gcc mingw-w64-x86\_64-cmake mingw-w64-x86\_64-ninja mingw-w64-x86\_64-SDL2 mingw-w64-x86\_64-openal
  ```

* **For the launchers (optional):** Visual Studio 2022 or newer with ".NET desktop development", or the free Build Tools for Visual Studio with ".NET desktop build tools". The game and Remix build without them; only the two launcher windows need them.
* **The PC port's source code,** the version this release is made for (see the release notes). Remix replaces some of the port's files, so it has to match that source.

\---

## Installation

#### 1\. Get the PC port's source

Clone it with its submodules:

```
git clone --recursive https://github.com/SlickAmogus/silent-hill-decomp.git
```

If you download the source as a zip instead, two folders inside it come out empty, and you need to fill them yourself:

* `pc\_port\\PsyCross`: extract https://github.com/SlickAmogus/PsyCross/archive/refs/heads/master.zip into it.
* `pc\_port\\third\_party\\rcheevos`: optional, for RetroAchievements. The game builds without it.

#### 2\. Download Remix

Download the latest `Remix\*.zip` from this page's **Releases**.

**Before extracting**, right-click the zip, choose **Properties**, tick **Unblock**, then click OK. Otherwise Windows marks every extracted file as "from the internet", and Visual Studio refuses to build the launchers (`error MSB3821 ... mark of the web`). If that has already happened, run this once in PowerShell on your source folder:

```powershell
Get-ChildItem -Path "C:\\path\\to\\silent-hill-decomp" -Recurse | Unblock-File
```

#### 3\. Remove an older Remix (only if you installed one before)

Copy `Remove\_Old\_Remix\_Files.bat` into the source folder (the one that holds `pc\_port\\` and `src\\`) and run it there. It deletes files from older Remix versions that would otherwise get built in twice.

#### 4\. Copy the files in

Copy the **contents** of these folders from the zip onto the source folder, and overwrite when asked:

|Folder|Needed?|What it is|
|-|-|-|
|`1\_required`|**Yes**|The Remix add-on and the game's hooks for it|
|`4\_required\_build\_files`|**Yes**|Build files: builds Remix with the game, plus fixes for current MSYS2|
|`2\_optional\_remix\_launcher`|Recommended|The Remix launcher (settings window)|
|`3\_optional\_docs\_tools`|Optional|Full documentation, the data generator and the layout test|
|`5\_psycross\_sound\_ram`|Optional|A PsyCross patch (see [Monster sounds](#monster-sounds))|

Prefer git? `remix\_mode.patch` contains the same changes as folders 1–5. From the source folder, run `git apply remix\_mode.patch`.

#### 5\. Build

From the source folder:

```
pc\_port\\build.bat configure
```

This builds `SilentHillPC.exe` and `SilentHillPC\_RemixAddon.dll` into `pc\_port\\build`. Keep the DLL next to the exe.

Then build the Remix launcher (optional):

```
pc\_port\\build\_remix\_launcher.bat
```

#### 6\. Add your disc and play

Put your disc image at `pc\_port\\build\\gamedata\\Silent Hill (USA).bin`, as the PC port's own instructions describe. Then start the game or the Remix launcher from `pc\_port\\build`.

To check that Remix loaded, look in `pc\_port\\build\\SilentHill.log` for:

```
\[REMIX] add-on loaded
```

\---

## Using it

* **In game:** turn modes on and off and roll a new seed under **PC Settings → Remix**. Room, item and enemy changes apply the next time an area loads. The starting gun applies to a new game.
* **Remix launcher:** has every option, including item-type weights, which monsters can appear in each area, kept items and the seed. **Save \& Play** saves and starts the game.
* **Turning it off:** switch the modes off, or delete `SilentHillPC\_RemixAddon.dll`, and the game is the normal PC port again. Deleting `remix.cfg` resets all Remix settings.

### Monster sounds

A monster outside its home area needs room in sound memory for its own sounds. The PC port's default sound renderer has only the PlayStation's 512 KiB, which rarely leaves room. Without it, the monster falls back to the area's sounds.

The **Remix launcher handles this automatically.** Whenever it opens, it switches the game to the legacy sound renderer (`spu\_renderer = legacy` in `config.cfg`), which has 2 MiB of sound memory. This affects the game's audio as a whole, not only Remix: the legacy renderer doesn't reproduce the PlayStation's reverb exactly.

If you'd rather keep the default renderer, apply the patch in `5\_psycross\_sound\_ram` to PsyCross and rebuild. It gives the default renderer the extra memory without changing how the base game sounds.

```
cd pc\_port\\PsyCross
git apply ..\\patches\\psycross\_spu\_ext\_ram.patch
```

`SilentHill.log` tells you which you have: `monster samples go above 512 KiB` means there's room for every monster's sounds.

\---

## Troubleshooting

When a build fails, `build.bat` repeats the failing file and its error at the end. The full output is in `pc\_port\\build\\build\_output.log`.

|Error|Fix|
|-|-|
|`unknown type name 'PsyX\_FarMeshRange'`|Your PsyCross is older than the game source. Update `pc\_port\\PsyCross` (see step 1). `build.bat` checks for this before building.|
|`conflicting types for 'open'` (PsyCross `libapi.c`)|Newer MSYS2 headers. Already fixed in `4\_required\_build\_files`; make sure you copied that folder.|
|`undefined reference to Pc\_Ra\_Why` / `WinHttpOpen`|Comes from an empty `third\_party\\rcheevos`. Fixed in `4\_required\_build\_files`. Or fill the folder: `git submodule update --init pc\_port/third\_party/rcheevos`|
|`error MSB3821 ... mark of the web`|Unblock the files (see step 2).|
|`\[REMIX] add-on not installed` in the log|`SilentHillPC\_RemixAddon.dll` isn't next to `SilentHillPC.exe`.|
|Monsters use the area's sounds|Open the Remix launcher once (see [Monster sounds](#monster-sounds)).|

\---

## License and credits

* **Remix** is released under the same license as the PC port it builds on: **GPL-3.0-or-later**.
* **The PC port:** [SlickAmogus/silent-hill-decomp](https://github.com/SlickAmogus/silent-hill-decomp) (GPL-3.0). See its `COPYRIGHT.md` for the full breakdown.
* **The decompilation:** [shdecompilations/silent-hill-decomp](https://github.com/shdecompilations/silent-hill-decomp) and its contributors. Neither this project nor the PC port claims any rights to it.
* **PsyCross:** [SlickAmogus/PsyCross](https://github.com/SlickAmogus/PsyCross).
* **Silent Hill** is © Konami. No game data is included or distributed. A legally obtained copy of the PlayStation disc is required to play.

