# Test routine

How to check Endgame 1989 in Workbench with a host and one client. Run it after every change to
the respawn, garrison or document code, and write the result at the bottom with the commit hash.

Plan and background: https://claude.ai/artifact/3vJegzkXgTHcE9dpEiUV7R

## State of the code (10 Nov 2026)

Everything listed under "Written without Workbench" has never been compiled. Expect compile
errors first; fix those before running any check.

Written without Workbench:

- `Config/GRAD_Endgame_Config.c` (all keys from the plan's config reference)
- `Log/GRAD_Endgame_MatchLog.c`
- `GameMode/GRAD_Endgame_EPhase.c`, `GRAD_Endgame_GameModeComponent.c`, `GRAD_Endgame_AdminRequests.c`
- `Respawn/GRAD_Endgame_FactionWave.c` (new wave rules), `GRAD_Endgame_FactionWaveTest.c`
- `Respawn/GRAD_Endgame_RespawnManager.c` (released list, client hold, refusal handling)
- `AI/GRAD_Endgame_GarrisonComponent.c` (spawn once, no despawn)
- `Objectives/GRAD_Endgame_DocumentSiteComponent.c`, `GRAD_Endgame_CopyDocumentAction.c` (site lock, per-side counts, active flag)
- `Objectives/GRAD_Endgame_FobComponent.c` (FOB taken when its garrison is wiped out, spawn points move there)
- `Objectives/GRAD_Endgame_SiteSelection.c` (5 of all placed sites, balanced between the FOBs)
- `Info/GRAD_Endgame_InfoService.c`, `GRAD_Endgame_InfoClient.c` (per-side markers, hints, decoded order by owner RPC, resync on join and respawn)
- `Objectives/GRAD_Endgame_FobVehicleSpawnerComponent.c` (light vehicles at the FOB, respawn when destroyed or driven away)
- `Objectives/GRAD_Endgame_Cipher.c`, `GRAD_Endgame_RadioSiteComponent.c` (cipher, cipher sites, radio sites, transmit action, direction finding)

Calls to look at first if something does not compile:

| File | Call | Why it might be wrong |
| --- | --- | --- |
| `GRAD_Endgame_MatchLog.c` | `FileIO.MakeDirectory`, `FileIO.OpenFile(path, FileMode.APPEND)`, `FileHandle.WriteLine`, `Close` | Written from memory; no example in COALITION or BreakingContact |
| `GRAD_Endgame_MatchLog.c` | `System.GetYearMonthDay` | Only `GetHourMinuteSecond` is confirmed by COALITION's code |
| `GRAD_Endgame_GameModeComponent.c` | `GetGame().GetWorldFile()` | From memory |
| `GRAD_Endgame_AdminRequests.c` | `[RplRpc]` methods added in a `modded` class | Common pattern, not confirmed for this component |
| `GRAD_Endgame_RespawnManager.c` | `[RplProp()] ref array<int>` in a `modded` class | COALITION replicates int arrays, but not from a modded class |
| `GRAD_Endgame_CopyDocumentAction.c` | `SetCannotPerformReason`, and whether `OnActionStart` / `OnActionCanceled` run on the server | From memory |
| `GRAD_Endgame_FactionWaveTest.c` | A subclass of the config class created with `new` | Should be fine; if not, set the values through a small setter instead |

## Setup

1. Open the addon in Workbench with COALITION Lobby and Coalition VON loaded.
2. Open `worlds/endgame.ent`.
3. Optional, for the phase checks: add `GRAD_Endgame_GameModeComponent` to the `COA_Lobby` entity
   in the world. Without it the respawn, garrison and document checks still work.
4. In `Configs/GRAD_Endgame_Config.conf`: `m_fDebugTimeScale 10` (a wave is then 27 s), and turn
   `m_bDebugLog` on so every match log line is also printed to the console.
5. Start a local server with one extra client (peer tool). Slot both players on BLUFOR unless a
   check says otherwise, and go through briefing and slotting into the game.

The match log is written to `$profile:GRAD_Endgame/match_<date>_<time>.log`.

## 0. Wave rules without multiplayer

In the script console:

    GRAD_Endgame_FactionWaveTest.Run();

Expected last line: `[GRAD_Endgame] FactionWave tests: 22 checks, 0 failed`.

## Respawn

With two players on one side the wave size is 1 (ceil(2 / 3)), so one death is a full wave.

| # | Do | Expect |
| --- | --- | --- |
| 1 | Player A dies | Log: `death;BLUFOR;<id>;;countdown 27 s`. A's respawn menu counts down from about 28 |
| 2 | Wait | Log: `wave_release;BLUFOR;;;released 1, wave size 1, team 2`. A's countdown ends, A can spawn at the chosen point |
| 3 | Die, confirm a spawn point at once, wait | A spawns when the countdown ends. No `respawn_refused` line (finding R1) |
| 4 | A dies; B dies 17 s later | Both are released together at A's time: `released 2`. B waited only about 10 s (the lucky late death) |
| 5 | Set `m_iWaveSizeDivisor 1` (wave size 2). A dies alone | No release at 27 s. A's countdown then shows the longest wait (120 s at scale 10). When B dies too, both go at once |
| 6 | Move one player to OPFOR. The OPFOR player dies | The BLUFOR countdown does not start or change |
| 7 | A dies, disconnects, rejoins | A gets the respawn menu with the same countdown target as before, not a fresh one (finding R5) |
| 8 | Admin menu: respawn BLUFOR while A is dead | A spawns at once, no `respawn_refused` |
| 9 | A client joins while a wave is running, then dies | Correct countdown on that client |
| 10 | A is dead. On an admin client, script console: `COA_PlayerRplToAuthorityManager.GetInstance().EndgameReleaseWave("BLUFOR");` | Log: `admin;BLUFOR;<id>;;release wave` and `free_respawn`. A's countdown ends at once |
| 11 | While dead, try to spectate | Not possible (`m_bSpectateWhileWaiting` is off) |

If check 3 fails: note whether the client gets its respawn menu back after the refusal.

## Documents

The config default is one copy per side per site.

| # | Do | Expect |
| --- | --- | --- |
| 12 | Hold the action for 10 s | Log: `copy;BLUFOR;<id>;<position>;site count 1`. The label reads "(1 made)" for both BLUFOR players and for a BLUFOR client that joins afterwards |
| 13 | Hold again | Action is greyed out: "Already copied" |
| 14 | Set `m_iCopiesPerSitePerFaction 0`. Both players hold the same site at the same time | The second player sees "In use" while the first holds (finding D1). If both can hold, `OnActionStart` does not run on the server; note it |
| 15 | An OPFOR player looks at the site after BLUFOR copied | His label reads "(0 made)" |
| 16 | With the game-mode component in the world, try to copy right after game start | "Not in this phase" (the phase is FOB) |

## Garrison

| # | Do | Expect |
| --- | --- | --- |
| 17 | Walk to within 200 m | Log: `garrison_spawn;;;<position>;agents at spawn N, groups active 1`. Note N; 0 is fine, the soldiers may arrive a moment later |
| 18 | Walk 500 m away and come back | Same soldiers, no second `garrison_spawn` |
| 19 | Kill the whole group | One `garrison_cleared` line. It never spawns again |
| 20 | Place 10 garrisons, wake them all | Note the server frame rate and the agent count from the `server_load` line (needs the game-mode component). That number is the AI budget per round |

## FOB and site selection (needs the game-mode component, two FOB prefabs and 6+ document sites)

A FOB prefab is an entity with `GRAD_Endgame_FobComponent` (faction key set) and `GRAD_Endgame_GarrisonComponent` on the same entity.

| # | Do | Expect |
| --- | --- | --- |
| 27 | Script console: `GRAD_Endgame_SiteSelectionTest.Run();` | `20 of 20 runs fair`, and the chosen sets differ between runs |
| 28 | Start the game | `sites_chosen;;;;5 of N sites, imbalance X %` with X at most 10. Only those 5 show the copy action; garrisons spawn at once at the 5 sites and both FOBs, none at the others |
| 29 | Wipe out the BLUFOR FOB garrison | `fob_state;BLUFOR;...;taken`, `spawn_moved;BLUFOR;...;1 spawn point(s)`, `free_respawn`. A dead BLUFOR player now spawns at the FOB |
| 30 | Wipe out the OPFOR FOB garrison too | `phase;;;;FOB -> DOCUMENTS (rule)` |
| 29b | Place two `GRAD_Endgame_FobVehicleSpawnerComponent` spots per side near the FOBs, with a light vehicle prefab. Take the BLUFOR FOB | `vehicle_spawn;BLUFOR` per spot; nothing for OPFOR yet |
| 29c | Drive a BLUFOR vehicle 600 m away (time scale 10: wait 40 s) | A new vehicle on the empty spot; the driven one stays where it is |
| 29d | Drive one 600 m away, bring it back before 40 s | No new vehicle |
| 29e | Destroy a vehicle on its spot | After 40 s the wreck disappears and a new vehicle stands there |
| 31 | In phase RADIO, a new client joins | No free slot to take; he lands in spectator |

## Phases (needs the game-mode component)

| # | Do | Expect |
| --- | --- | --- |
| 21 | Start the game | A new log file with `match_start`, then `phase;;;;WAITING -> FOB (rule)` |
| 22 | Admin, script console: `COA_PlayerRplToAuthorityManager.GetInstance().EndgameAdvancePhase();` twice | `FOB -> DOCUMENTS (admin)`, then `DOCUMENTS -> RADIO (admin)` and two `free_respawn` lines |
| 23 | Wait 3 s (30 s grace at scale 10), then die | Log: `permadeath;;;;on`. The dead player goes to spectator, no respawn menu |
| 24 | Same call from a client that is not an admin | Nothing happens, no log line |
| 25 | A client joins in phase DOCUMENTS | Document check 16 behaves correctly on that client (it knows the phase) |
| 26 | `...EndgameEndMatch("BLUFOR");` | `match_end;BLUFOR;;;winner BLUFOR (admin)`, COALITION goes to its after-action screen |

## Radio phase (needs the game-mode component, 2+ radio site prefabs, 1+ cipher site, a cipher item prefab)

Prefabs to build in Workbench first:
- **Radio site**: `GRAD_Endgame_RadioSiteComponent`, an ActionsManagerComponent with `GRAD_Endgame_TransmitAction` (Duration about 3 s), optional `GRAD_Endgame_GarrisonComponent`.
- **Cipher site**: an empty entity with `GRAD_Endgame_CipherSiteComponent`.
- **Cipher item**: any small inventory item with `GRAD_Endgame_CipherComponent`; set it as `m_sCipherPrefab` in the config.

With `m_fDebugTimeScale 10` the 720 s transmission takes 72 s, and give one test player the radio operator slot.

| # | Do | Expect |
| --- | --- | --- |
| 32 | Start the game | `radio_sites_chosen`; no document markers yet. When both FOBs are taken (phase 2): "Documents located" and "Documents within 150 m of ..." for each active site, on both sides |
| 33 | BLUFOR copies at 3 sites | Hint "3 of 3 copied" to BLUFOR only; each copied site's marker disappears for BLUFOR only. Then `FOB/DOCUMENTS -> RADIO`, `cipher;;;;spawned`, `threshold;BLUFOR` |
| 34 | BLUFOR squad lead | Hint "Decoded order" with the cipher grid. OPFOR gets no order and no cipher marker |
| 35 | Both sides | Hint "Radio site" for each active radio site |
| 36 | A BLUFOR leader respawns or rejoins | Gets the order and all BLUFOR markers again (SyncPlayer) |
| 37 | Pick up the cipher | `cipher;BLUFOR;<id>;...;picked up` |
| 38 | Non-operator with cipher uses the radio | "Only the radio operator" |
| 39 | Operator with cipher transmits | `transmission;BLUFOR;...;started at 0 s`; OPFOR gets "Enemy transmission within 300 m of ..." and `df_sent` |
| 40 | Operator walks 10 m away | `interrupted (operator left the set) at N s`; with PAUSE, starting again continues from N |
| 41 | Transmit to the end | `transmission ... done`, `match_end;BLUFOR;;;winner BLUFOR (transmission)` |
| 42 | Kill the operator while transmitting | `interrupted (operator killed)`; the cipher can be taken from the body |

## Map validator

| # | Do | Expect |
| --- | --- | --- |
| 43 | World Editor: Plugins > GRAD Endgame > Validate Endgame map | A dialog with PASS / WARN / FAIL lines, also in the console and the clipboard. On the current test world: FAIL for missing FOBs, radio and cipher sites, until those prefabs are placed |
| 44 | Place both FOBs, 6+ document sites, 2+ radio sites, 2 cipher sites; run again | No FAIL. Fix any WARN about distances or variety, or accept it |

If every list reads 0 although the prefabs are placed, reload the world once and run it again.

## Results

| Date | Commit | Passed | Failed | Notes |
| --- | --- | --- | --- | --- |
| | | | | |
