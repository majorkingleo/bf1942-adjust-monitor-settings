# Standalone mod layout, multiplayer requirements, launch

Target: a game root that starts **only** `Battle_Of_Pavlov-1942` and supports
multiplayer (LAN / dedicated server), with all content self-contained.

## Folder layout

```
bf_1942_kwg_mod/                        <- new game root
├─ BF1942.exe
├─ BF1942_w32ded.exe                    <- dedicated server (multiplayer)
├─ DedicatedServer.exe
├─ binkw32.dll, mfc70.dll, msvcr70.dll, GDFBinary_de_DE.dll
├─ Core/                                (optional; movie codecs)
└─ Mods/
   ├─ BF1942/                           <- base game, trimmed
   │  ├─ init.con
   │  ├─ Mod.dll, DrvMgt.dll
   │  ├─ lexiconAll.dat
   │  ├─ contentCrc32.con, bfdist.vlu, 00000000.016, 00000000.256
   │  ├─ Settings/                      (default profile + server templates)
   │  └─ Archives/                      (all root .rfa + bf1942/Game.rfa)
   │                                    (NO bf1942/levels — no base maps)
   └─ bf_1942_kwg_mod/                  <- custom mod (name = folder name)
      ├─ init.con
      ├─ lexiconall.dat                 (FH's; HUD/chat/objectives)
      ├─ music/                         (FH music .bik, optional)
      ├─ movies/Background.bik          (optional)
      └─ Archives/
         ├─ objects.rfa                 (pruned)
         ├─ standardMesh.rfa            (pruned)
         ├─ texture.rfa                 (pruned)
         ├─ sound.rfa                   (pruned)
         ├─ animations.rfa              (full)
         ├─ treemesh.rfa                (pruned or full)
         ├─ aimeshes.rfa                (full)
         ├─ menu.rfa                    (full or pruned)
         └─ bf1942/
            ├─ game.rfa                 (full, 0.3 MB — mandatory)
            └─ levels/
               └─ Battle_Of_Pavlov-1942.rfa
```

The custom mod's `Archives/` override the base game's archives by internal path,
exactly how Forgotten Hope overlays the vanilla game.

## `init.con` for the custom mod

```
game.setCustomGameName bf_1942_kwg_mod
game.addModPath Mods/bf_1942_kwg_mod/
game.addModPath Mods/BF1942/
game.setCustomGameVersion 1.0
game.setCustomGameUrl ""
Game.setMenuMusicFilename "music/slaughter4.bik"
Game.setLoadMusicFilename "music/vehicle4.bik"
Game.setWinMusicFilename "music/vehicle3.bik"
Game.setLoseMusicFilename "music/menu.bik"
Game.setCampaignLoseMusicFilename "music/theme2.bik"
Game.setDebriefingMusicFilename "music/briefing.bik"
```

## Multiplayer checklist (mandatory)

1. **Identical installs** — every player and the server must use the same
   `bf_1942_kwg_mod` tree. Any file difference can cause content-mismatch errors.
2. `lexiconAll.dat` present in **both** mod roots (base + custom) — required for
   HUD, chat and objective text.
3. `Mod.dll` / `DrvMgt.dll` present in the base mod — required to host/join.
4. Dedicated server binaries copied (`BF1942_w32ded.exe`, `DedicatedServer.exe`).
5. `Settings/` templates present (the game writes `ServerSettings.con`,
   `MapList.con`, `PBClient.con` there on first run).
6. `contentCrc32.con`: keep the base game's copy **verbatim** in
   `Mods/BF1942/`. The custom mod needs none (FH ships none) — content checks
   then pass between identical trimmed installs.
7. The map is a Conquest map, so it already has control points, spawns and
   tickets — no extra work for multiplayer modes.

## Launch

```
rem client
BF1942.exe +game bf_1942_kwg_mod

rem dedicated server
BF1942_w32ded.exe +game bf_1942_kwg_mod

rem direct map start (level folder name inside the .rfa)
BF1942.exe +game bf_1942_kwg_mod +restart 1 +map Battle_Of_Pavlov-1942
```

## Things that silently break a rebuilt install

Learned the hard way on a real rebuild; each of these produced a client that
showed a black screen or exited with status 0 and no error text at all.

1. **dgVoodoo wrapper.** BF1942 renders through D3D8, which modern Windows/drivers
   no longer ship. The client opens a black fullscreen window and dies unless
   `D3D8.dll` + `dgVoodoo.conf` sit next to `BF1942.exe`. These are custom to the
   host machine (e.g. `OutputAPI = d3d12_fl12_0`) - copy them, do not regenerate.
2. **Missing profile folder.** `Mods\BF1942\Settings\Profile.con` names a profile
   (`game.setProfile "MajorLeo"`), but a source install may only ship
   `Settings\Profiles\Default`. The **first launch creates the profile directory and
   then exits silently**; the second launch boots normally. Expect one throwaway
   run per fresh copy.
3. **Music referenced by a verbatim init.con.** The vanilla `Mods\BF1942\init.con`
   sets menu/loading/win/lose/debrief music; ship `Mods\BF1942\Music` or the menu
   cannot start.
4. **Base UI/support archives.** Mod `menu.rfa` files are overlays: dropping the
   vanilla `Font.rfa`, `shaders.rfa`, `menu.rfa`, `ai.rfa`, `aiMeshes.rfa` leaves
   an empty map list and missing text.
5. **`+game` must be the mod FOLDER NAME** (`bf_1942_kwg_mod`), not some other
   mod's name: pointing at an unrelated or empty folder mounts nothing and gives
   an empty map list.
6. **A PARTIAL level inside `bf1942\game.rfa`.** `game.rfa` is enumerated by the
   engine at startup, so a level folder there holding only a few of its files
   (no `Init.con`, no `Heightmap.raw`) makes it exit cleanly (`status 0`). This
   happens when the archive is packed from a folder that also contains
   `levels\<Map>\` - e.g. the FH staging tree holds the `bf1942\game\` files
   *and* a stray `bf1942\levels\<Map>\` subtree. Pack `game.rfa` from
   `game\**` only. `repack_standalone.ps1` enforces this with `Include = 'game/*'`
   plus a `Forbidden = 'bf1942/levels/*'` assertion against the finished archive.
   Note `rfaPack` has **no exclude switch** - it packs a whole folder - so the
   script packs a hardlink mirror of the wanted files instead.
7. **A repack that silently merged another archive's content.** Filtering by
   folder alone is not a guarantee: verify the finished archive's **entry list**
   (count *and* paths), not just a success message. Use `list_rfa.ps1 -Names` -
   `Format-Table` truncates long paths and can hide exactly this.
8. **A later source overwriting an earlier one in the staging tree.** Keep-lists
   from several archives are extracted into ONE staging root and merge *by
   internal path*, so when two sources provide the same path the LAST extraction
   wins. Ordering vanilla after FH silently replaced FH's
   `objects/Effects/Common/effects.con` (49,810 bytes vs FH's 52,747) and dozens
   more. Every single-file audit still looked fine - the file was present, just
   the wrong one - and the level aborted during load. Two rules follow:
   - extract the MOD-SPECIFIC source **last**, so it wins every collision;
   - a keep-list that omits a path the other source also has is a collision
     waiting to happen. When a class is meant to ship "complete", list **all**
     of its source's entries, not a handy subset.
9. **A staging tree that cannot be rebuilt from the manifest.** Archives whose
   content came from a leftover full extraction instead of a keep-list look
   perfect until the tree is wiped - then they shrink or vanish.
   `aimeshes.rfa`, `treemesh.rfa` and `bf1942/game.rfa` had **no keep-list at
   all** and survived only as stale leftovers. Check that every expected archive
   appears in the pack output and that its entry count matches its keep-list;
   keep the lists under version control.
10. **A level archive whose name ends in `_<digits>`.** `X_001.rfa` is the engine's
    **overlay** convention - extra content for level `X` (FH ships
    `Battle_Of_Stalingrad_001.rfa` and friends). A level named `..._11` is therefore
    read as an overlay of a level `...` that does not exist, and its internal folder
    can never match. The client then exits `0x00000000` with **no window at all**,
    no matter what the archive contains: deleting it restores start-up, keeping it
    present but unlisted still fails, and even a renamed **byte copy of a known-good
    level** kills a healthy install. Use a hyphen instead (`Battle_of_KWG-11`) -
    it also renders as a space, so the menu still shows “Battle of KWG 11”.

Diagnosis tips for these failures:

- The **client writes no `BfLog_*.log`** and no stdout - it is a GUI app. Only the
  dedicated server writes a log (`Mods\<mod>\Logs\BfLog_<user>.log`).
- Compare the rebuilt root against a known-good install file by file (names *and*
  content hashes) and look at which files a run creates (`Settings\Profiles\...`,
  `cache\block.dat`, `BF1942.pid`) to infer how far it got.
- `strace -f` / gdb show an exit **status 0**: the engine chose to quit, so look
  for a missing dependency rather than a crash.
- **Bisect, don't guess.** Launch the known-good install with `BF1942.exe +game <mod>`
  and remove/add one archive at a time. Two exit signatures are distinguishable and
  worth using as a sanity check: a real content failure exits `0x00000000`, whereas
  a spurious harness failure (e.g. a leftover process) exits `0xFFFFFFFF`.
  Always bracket a batch with a control run of a mod that is known to boot, before
  *and* after - if a control fails, the whole batch is void. Build all test mods up
  front: deleting a mod folder while the game runs fails on locked `.rfa` files.
- **Missing assets vs assets being overridden** - the two look identical from the
  outside. Discriminate with an overlay: junction the WHOLE known-good mod into
  the rebuilt root (`mklink /J Mods\<known> <install>\Mods\<known>`) and add it to
  the mod path. If the symptom persists with every good asset available, the
  problem is your content *overriding* good files, not missing files - which
  points straight at item 8 above. Remember to remove the junction afterwards.
- **Some bindings are not derivable from the files.** Mesh materials are named
  `<mesh>_MaterialN` and the texture library uses unrelated names
  (`001_acajoubrown.dds`); `materialManager*.con` is a surface/physics table with
  no texture references. There is no way to compute a mesh's texture set from the
  shipped files, so a texture set must either be taken complete or validated
  visually.


With only one map in `Archives/bf1942/levels/`, the in-game map list shows just
"Battle of Pavlov".

## Install-dir note

BF1942 reads its install path from the registry
(`HKLM\SOFTWARE\Wow6432Node\EA GAMES\Battlefield 1942\InstallDir`). If the game
does not start from the new folder, point that value at the new root (or run the
exe from inside it).
