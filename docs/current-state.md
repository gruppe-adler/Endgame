# Endgame 1989: current state

As of 2026-11-14 (evening update: table placement, replay fixes, GM debug). Written for whoever picks this up next, human or Claude session. Read this first,
then `docs/test-routine.md` (what to check in play) and `Endgame 1989 implementation plan.md` (the
original plan; parts are superseded, see "Decisions that changed the plan").

Legend: **played** = seen working in a Workbench or dedicated run. **written** = code exists and
compiles, never seen working. **open** = known problem or missing piece.

## What the mode is

Two sides (BLUFOR/US, OPFOR/USSR, 1989) on a COALITION Lobby game mode, Eden terrain. FIA militia
(AI) holds every site and fights both sides. Phase 1: clear your FOB. Phase 2: copy documents at
3 sites. Phase 3: find the cipher and transmit at a radio site for 12 minutes. The briefing for
players is `docs/briefing.md`.

## Repository map

| Path | What |
| --- | --- |
| `Scripts/Game/GRAD_Endgame/GameMode/` | Phase state machine (`GRAD_Endgame_GameModeComponent`), admin RPCs, `EPhase` |
| `.../Respawn/` | Per-side wave respawn on top of COALITION's respawn manager (`modded COA_RespawnManager`), wave rules + self-test |
| `.../Objectives/` | FOB, document site + copy action, radio site + transmit action, cipher + cipher sites, vehicle spots, site selection, `TableScan` (finds tables for the documents), `ArsenalFree` (arsenals cost no supplies) |
| `.../AI/` | `GRAD_Endgame_GarrisonComponent`: spawns FIA groups once, tiers LIGHT/MEDIUM/HEAVY, defend waypoint per group |
| `.../Info/` | `GRAD_Endgame_TaskService` (objectives as vanilla `SCR_Task`s), `InfoService` (server), `InfoClient` (client, owner RPCs) |
| `.../UI/` | `FieldLogDisplay` (HUD info display), `MapStatusLine`, `Typewriter` sounds, `AdminMenu`, `MapJournalGuard` |
| `.../Replay/` | **Copy of BreakingContact's replay** (`GRAD_BC_*` renamed `GRAD_Endgame_*`) + `ReplayStubs` |
| `.../Config/`, `.../Log/` | `GRAD_Endgame_Config` (all tunables), match log (`$profile:GRAD_Endgame/match_*.log`) |
| `Missions/Endgame89.conf` + `Scripts/.../Config/GRAD_Endgame_Config.c` | **The options.** `GRAD_Endgame_Config` is a `SCR_MissionHeader` subclass: all values live in the mission file (currently a TEST config, see "Before a real match"); a server overrides any of them under `game.gameProperties.missionHeader` (`server_config.sample.json` lists all 70). There is no separate config file |
| `Configs/System/chimeraInputCommon.conf` | Input actions: Shift+J/K/T/U, plus V/C on the replay map |
| `Configs/Map/MapFullscreen.conf` | Adds the replay map layer to the fullscreen map (same GUID as vanilla, partial override like BreakingContact) |
| `Configs/Gearscripts/` | US 80s and USSR 80s gearscripts (copied from BreakingContact) |
| `Prefabs/` | FOB (per side), document site (old, table-based) and `DocumentSite_Auto` (a paper prop, placed on a table by script; the layer uses this one), radio site, cipher item, cipher site, vehicle spots, ammo arsenal boxes, test garrison |
| `Prefabs/!Systems/!Characters/Core/COA_PlayerController.et` | Override of COALITION's controller: adds our HUD display (made in Workbench) |
| `Missions/Endgame89.conf` | The scenario (`{3C7A91E05B2D48F6}Missions/Endgame89.conf`) |
| `worlds/endgame.ent` + `endgame_Layers/default.layer` | Test world: sub-scene of Eden; lobby, 2 FOBs, 9 document sites, 3 radio sites, 2 cipher sites, vehicle spots |
| `UI/layouts/`, `UI/Textures/Icons/` | HUD layouts (Roboto Mono font), replay map icons (copied from BreakingContact, same GUIDs) |
| `Sounds/` | Typewriter audio projects (`key1..5`, `return`, `bing`) |
| `docs/` | `briefing.md`, `test-routine.md`, `garrison-orders-research.md`, this file |

Dependencies (`addon.gproj`): the base game, COALITION Lobby, Coalition VON.

## How a match runs (as coded)

1. COALITION reaches GAME. Match log opens, document and radio sites are chosen (balanced between
   the FOBs), phase stays WAITING.
2. **Phase 1 (FOB) starts when COALITION's freeze time (safestart) ends** (`WaitForFreezeEnd`,
   log line `freeze_over`). Garrisons and vehicles spawn, each side gets a task for its own FOB.
3. First FOB cleared: phase 2 for both sides. Each side only sees and may copy documents once its
   own FOB is cleared. Spawns move to the cleared FOB, waiting dead are released.
4. A side with the threshold (default 3) of copies: phase 3 for both. Cipher spawns (0.5 m above its
   site), the leading side's leaders get its position as a task. Each side gets one free respawn,
   then permadeath after a grace period. Anyone carrying the cipher may transmit
   (`m_eTransmissionRole 1`).
5. Win: transmission finished, or a side eliminated (only if both sides had players at phase 3
   start), or time (COALITION limit 60 min = draw). Then the replay plays, then COALITION's AAR.

## Status by area

| Area | Status | Notes |
| --- | --- | --- |
| Phase machine, FOB capture, first-FOB rule, document/radio/cipher flow | **played** (listen server and dedicated) | Match logs show every transition |
| Site selection balance | **played** | Self-test 20/20 fair; real layout is unbalanced (see open) |
| Garrison spawn, clearing | **played** | 14 groups / ~69 agents on a 2-core server |
| Garrison waypoints | **played** | Defend waypoint added 2 s after spawn. Navmesh fixed by the mapper; garrisons move (confirmed 2026-11-14) |
| FIA vehicle patrols | **played** | 2 UAZ PKM FIA, crewed by a fire team, loop over the chosen document sites (`GRAD_Endgame_FiaPatrol.c`, config `m_iFiaPatrol*`). Confirmed driving (2026-11-14) |
| Wave respawn | **played** | Death to release exactly 27 s at debug scale 10 (270 s real) |
| Permadeath wave bug (wave kept running after permadeath) | **written** | Fixed in `EndgameTick`, not run |
| Freeze-time phase start | **written** | Not run |
| Tasks (native `SCR_Task`) | **played** | Click-through crash guarded by `MapJournalGuard` (workaround) |
| HUD field log, map status line | **written / partly played** | Display registered via the controller override; keys Shift+J/K/T/U new and untested; status line not registered in a map config yet |
| Copy action vs widget sync | **written** | Reworked: widget follows the action's real `Duration` (prefab, 10 s). Untested |
| Typewriter sounds | **written** | Wired to `Sounds/*.acp`; untested in a match |
| Replay | **written / partly played** | Recording and playback start ran (69 frames). Fixed after that run, not rerun: spectator parked at y=100000 is no longer recorded, living players open the vanilla map menu (pan/zoom, no HUD), FIA garrison soldiers are recorded (neutral icon). After-action hand-off not yet seen |
| Document placement on tables | **written / partly played** | `TableScan` found a table within 100 m of all 9 sites. Auto placement worked for 4 of 5 chosen sites (`doc_placed` log). Drill benches now accepted, same-floor limit 2.5 m, 5 cm drop onto the surface. Untested after the last prefab change |
| Document site action ("Copy documents") | **open** | The Auto prefab is a vanilla paper prop with no RplComponent: added back (with the GM editable component). Whether the prop has a collider the action can aim at is unconfirmed |
| Game Master debug | **written** | Document sites and the cipher item are GM editables (the cipher site is NOT: the editable component put it at 0 0 0, so it has an orange marker instead); a green/grey sphere and line mark the planned document spot (only while the GM view is open); a GM move of a site is sent on to clients (`doc_moved`) |
| Admin RPCs (advance phase, end match, threshold) | **written** | No UI; need a console or a menu |
| Gearscripts | works, 6 check errors | Inherited from BreakingContact; radios are shared default items (see below) |
| Dedicated server / Workshop publish | **played** | Server loads the mission and the addon |

## Decisions that changed the plan

- Objectives are native Reforger tasks (no custom marker layer); the local-marker code in
  `InfoClient` (`ShowOnMap`) is now unused and can go once tasks are confirmed.
- No terrain-snap script: placement is exactly what the editor says.
- Phase 2 starts at the FIRST cleared FOB; the other side sees no documents until its own FOB is
  cleared (and cannot copy: "Take your FOB first"). FOB taken-state is mirrored on the game-mode
  component because the FOB prefab has no `RplComponent`.
- Transmission is open to every role; the radio operator slot exists but is optional.
- Phase 1 waits for COALITION's freeze time.
- Copy time = the action's `Duration` in the document site prefab. `m_fCopySeconds` is unused.
- Replay = BreakingContact's own code, copied (fast track, no extra dependency). Our first lean
  replay was deleted.
- Documents go on tables that are already in the world: the server scans around each placed site
  (`m_fPlaceRadius` 100 m), picks the nearest free suitable table on the same floor and sends the
  position to clients. `m_bAutoPlace` off keeps the documents where a mapper put them (manual).
- The cipher item needs a real item with a model: the old parent (`IntelligenceFolder_E_01`) is a
  system placeholder with no mesh or physics.
- Radio/leadership gear flags are off in the lobby (`m_bEnableLeadershipRadios 0`,
  `m_bEnableRTORadios 0`); everyone carries the shared radio and binoculars.

## Open problems, in the order I would do them

0. **Document site action and visibility.** Check on the next run: no `has no RplComponent` error
   for the sites, the papers are there (green marker shows where), "Copy documents" appears. If
   the action does not, add a collision child (a vanilla prop with collision, invisible) to
   `GRAD_Endgame_DocumentSite_Auto.et`. Also the site at 5507,10551 only has a drill bench in reach.
   0b. **Cipher visual:** give `GRAD_Endgame_CipherItem` a real inventory item with a model
   (briefcase, folder, codebook) as parent; no beam (it would reveal the position).
1. ~~Navmesh~~ fixed by the mapper; garrisons and patrols confirmed moving (2026-11-14).
2. **Map exception in the spectator menu in phase 3** (not seen in the last run, only two
   `COA_SpectatorMenu.OnMenuClose` NULL pointers at exit, in COALITION's own code) (earlier: `SCR_MapEntity.UpdateViewPort`, NULL pointer
   every frame). Unknown whether our `Configs/Map/MapFullscreen.conf` causes it. A/B test: remove
   that file, reload, die in phase 3.
3. ~~Arsenal ammo greyed out~~ done: ammunition supply works (confirmed 2026-11-14, with the `ArsenalFree` override).
4. **Layout:** OPFOR start spawn is 54 m from a document site (garrison next to spawn); document
   sites are skewed towards the BLUFOR FOB; the first radio site is 444 m from a FOB; cipher site
   2 is 3.3 km from the OPFOR FOB. A minimum distance from start spawns for site selection (and a
   validator warning) is proposed, not built.
5. **Replay end to end:** check recording starts, playback, the after-action hand-off, and that the
   normal map still works. Trackers for projectiles and explosives (`GRAD_BC_ExplosionTracker`,
   `GrenadeTracker`, `RocketTracker`, `WeaponFireTracker`, `DetonationWatcher`) are not ported.
6. **Task visibility may be a client-side filter only.** If the task system replicates every task
   to every client, exact positions (cipher) leak to a modified client. Check the wiki; fall back
   to owner RPCs for the cipher if needed.
7. **Re-pick the FIA group prefabs** in the config UI to remove the `Wrong GUID {0000...}` log spam
   (the config holds plain paths).
8. **HUD finish:** register `GRAD_Endgame_MapStatusLine` in the in-game map config; typewriter
   font / paper texture polish; confirm the key bindings (Shift+J/K/T/U) are free.
9. **Mission thumbnails:** the mission uses vanilla `GM_Eden` images; the publisher logs
   `Failed to load metafile of image`. Use own images.
10. **Gear check errors (6):** radios per role are not configured for COALITION's own radio system.
    Harmless for now (shared default radios).
11. **Admin UI:** the admin calls have no button except the threshold box (needs a layout override).

## Respawn (research result, COALITION Lobby)

COALITION runs fixed-timer respawn (lobby `m_iTimeToRespawn 60`, waves off); the client counts
down and asks the server for the respawn, the server does not check the time. Our override
supplies the countdown (`GetWaveSecondsRemaining`) and gates `RespawnPlayer`. The "30 s" wait was
ours: 27.4 s (270 s / debug scale 10), not COALITION's. Open: the pre-timestamp seed is 60 s
(set `m_iTimeToRespawn` to 3-5 in the layer, or return a positive value instead of -1), and the
permadeath wave fix was not running in the last test (the script was saved after Workbench had
started). Use `SetRespawnEnabled(false)` for permadeath (already done).

## Before a real match

The live config is a test config. Set in `Missions/Endgame89.conf` (or in the server config's `missionHeader`):
`m_fDebugTimeScale 1` (it is 10: waves 27 s, transmission 72 s), `m_bDebugLog 0`,
`m_bRunSelfTests 0`. Check the document site prefab `Duration` (copy time). Remove or re-place
test objects  (`GRAD_Endgame_Garrison` on the first document site).

## Things that cost time, so you do not repeat them

- **Never override a prefab by hand-writing a file at its path.** An override without the parent
  and root `ID` replaces the prefab (it dropped COALITION's controller and left an ocean camera).
  Create overrides in Workbench ("override/inherit").
- **Replicated state needs an `RplComponent` on the entity.** Prop prefabs inherited from vanilla
  tables have one; our bare `GenericEntity` prefabs (FOB, spots, cipher site) do not. State that
  clients need goes on the game-mode component.
- **Do not overload methods of vanilla base classes** (`Show` on an `SCR_InfoDisplayExtended`
  broke vanilla callbacks with 4 errors in `SCR_VoiceoverSystem`).
- `GetCopySeconds()` and other timers are divided by `m_fDebugTimeScale`; a value compared
  against an engine-side time (prefab `Duration`) will disagree under a test scale.
- Layer files are rewritten by Workbench. Close the world without saving before editing it on disk.
- **Workbench keeps script statics between play sessions.** The client info (order, notices)
  survived a restart and showed an old "Decoded order" at the start; `Clear()` now resets it and
  runs at every match start.
- **`SetActive(true)` on a site that is already active changes nothing**: put logic that must run
  at selection time before that early return.
- Vanilla prop prefabs may lack an `RplComponent` and the editable component; the replication
  guard logs it at startup.
- The replay recorder had parked spectators at (40, 100000, 0) in its frames.
- The replay assets share GUIDs with BreakingContact. Do not load both addons on one server.

## Test checklist pointer

`docs/test-routine.md` has the numbered checks (1 to 51). Next run should cover: `doc_placed` lines and the document action, the replay map (pan/zoom, FIA and your marker visible), the order widget not showing at start, the arsenal ammo, freeze-time phase
start (`freeze_over` before `phase;;;;WAITING -> FOB`), permadeath deaths (no wave lines after
`permadeath`), copy bar vs engine bar, any-role transmit, Shift key bindings, replay and
after-action, the `has waypoint` log lines once the navmesh is fixed.
