# Endgame 1989: implementation plan

Nov 7, 2026 · @nomi

Nine milestones take the mode from an empty addon to a tuned two-map release; each ends with a test gate, and the first one only answers open Enfusion questions. Sizes are rough estimates for one developer and should be recalibrated after milestone 1. The concept this builds on is in the doc "Endgame: Gruppe Adler Edition und Reforger-Konzept".

## Roadmap

The nine milestones fall into four stretches, each closed by a gate. The bands are equal for readability and are not to scale; the core loop is the longest.

&#91;embedded content: roadmap · 4 stretches, 4 gates\]

## Milestones

Each milestone is playable on its own, so a problem shows up one layer after it was introduced.

| # | Milestone | Deliverables | Exit gate | Size |
| --- | --- | --- | --- | --- |
| M0 | Setup and spikes | Repo, core addon skeleton, test world, answers to the spike questions | Every spike question answered or a fallback chosen | S |
| M1 | Skeleton | Game-mode entity, phase state machine, config resource, match log, debug commands to advance a phase | Two players join, phases advance by debug command, state is correct for a late joiner | M |
| M2 | FOB phase | FOB slot prefab, garrison clearing and hold, spawn point activation, arsenal with the loadout system, vehicle depot with a budget (reuse BreakingContact's campaign building integration with budget type VEHICLES; direct depot spawn as fallback) | One side captures its FOB, spawns there and gets a loadout and a vehicle | M |
| M3 | Wave respawn and HUD | Wave manager, timers, late-join window, spectator block, countdown HUD, limited FOB resupply | Four players die and return in waves; timers match the config; no instant respawn path remains | L |
| M4 | Documents phase | Document site slot, garrison tiers, copy hold-action, counter and threshold, decoded order, area markers | One side reaches the threshold and its leader receives the order; progress is correct on all clients | L |
| M5 | Cipher and radio phase | Cipher item, radio site slot, transmission hold-action, interrupt rules, direction finding, win and draw logic, phase 3 permadeath and spectator | A full match from start to win with 2 vs 2 players and shortened timers | L |
| M6 | Information and polish | Marker rules per the information table, final HUD, audio loop, radio messages, Gamemaster controls | Each row of the information table verified with two clients per side | M |
| M7 | Second map and validator | Validation plugin, map config override, a second map, scenario entries | The second map passes the validator and plays a full match | L |
| M8 | Playtest, tuning, release | 8v8 sessions with logs, tuning rounds, one 16v16 session, Workshop publish | Three 8v8 matches in a row without a blocking bug; 16v16 within the server frame budget | L |

Size guide, for one developer: S about 2 working days, M about 4, L about 8. These are estimates, not measurements.

## Spikes first (M0)

The concept rests on seven Enfusion assumptions that I could not verify. Each spike is a small throwaway test in Workbench, and each has a fallback so that a negative answer changes the plan instead of stopping it.

| # | Question | How to answer it | Fallback |
| --- | --- | --- | --- |
| 1 | Which class or hook lets the mode delay or refuse a player's respawn? | Read the vanilla game mode and respawn scripts; build a minimal mode that delays a respawn by 10 s | Custom spawn handling, or hold the player in a waiting state and spawn them manually |
| 2 | Can a prop carry a hold-action with progress visible on all clients? | A prop with a user action that counts to 10 s, tested with two clients | Instant action plus a cooldown |
| 3 | What does the vanilla radio prefab offer for interaction and sound, and can a loop play at a prop? | Place a vanilla radio, inspect its components, test a looping sound at a prop | Custom prop with its own sound component |
| 4 | Do timers and progress values reach late joiners correctly? | Replicated values changed on the server, a client joining mid-way | Server sends the current state by remote call when a player joins |
| 5 | Scenario Framework layers or lightweight custom slot prefabs for the slots? | Build one slot each way and compare setup effort and map-making effort | Custom slot prefabs with one component |
| 6 | Can a garrison be spawned by a trigger and despawned when cleared, and does it path at the slot? | A light garrison with defend waypoints, spawned and despawned by a trigger | Pre-placed garrisons enabled and disabled per layer |
| 7 | Can the server show a temporary map marker to one faction only? | Test the marker or task API with a faction filter | A local marker drawn on each faction member's client after a remote call |

The spike notes become a short page in the repo. Any fallback that is chosen is written into the milestone it affects before M1 starts.

## What BreakingContact already answers

I scanned [gruppe-adler/BreakingContact](https://github.com/gruppe-adler/BreakingContact) (main, last commit 2026-09-06, "remove too many radios after spawn") against the seven spike questions. It is the closest existing code, because it already runs a radio transmission with interruption, faction-specific actions and a vehicle budget. File paths are relative to the repo root.

| # | Status | What the repo shows | Still open |
| --- | --- | --- | --- |
| 1 Respawn gate | Partly | BreakingContact does not gate respawn itself. It runs on the COALITION lobby (`COA_Gamemode`), whose settings already include wave respawn, respawn time, cutoff minutes and per-slot respawn pools (`Worlds/MP/BC_everon_Layers/test_mode_2.layer`); BC leaves wave respawn off. Spawn points are `COA_StaticSpawnPoint` entities registered with `COA_RespawnManager`, and the spawn location is read from the entity when a player spawns, so moving the entity moves the spawn (`GRAD_BC_BreakingContactManager.c`, `SyncSpawnPointsToTrucks`) | Whether COALITION's wave respawn fits the GA rules (wave size by player count, fill plus timer, 30 s late join). Test that before writing our own manager |
| 2 Hold-action | Answered | `SCR_ScriptedUserAction` with `PerformContinuousAction` and `LoopActionUpdate(timeSlice)`, a completion guard flag, `HasLocalEffectOnlyScript` false so that show and perform checks run on the client and `PerformAction` on the server; faction gating in `CanBeShownScript` by faction key (`Scripts/Game/UserActions/GRAD_BC_ToggleRadioTransmission.c`, `GRAD_BC_DisableRadioTruck.c`) | The action's progress bar is local to the performer; other players need a replicated value (row 4) |
| 3 Radio | Mostly | `GRAD_BC_TransmissionComponent` is a state machine with OFF, TRANSMITTING, INTERRUPTED, DISABLED and DONE, with replicated state: very close to the transmit and interrupt logic planned here. Sounds are one-shot events (`AudioSystem.PlayEvent` with an `.acp` event at a position) plus a broadcast RPC | A looping radio sound at a site: not found in the repo. BC uses a custom radio truck, not the vanilla radio prefab |
| 4 Replication and late join | Answered | `JIP_COMPATIBILITY.md`: replicated properties reach a late joiner with their current value, but the change callbacks do not fire on that first sync, so the manager calls a sync routine after a short delay. RPC types: Owner is not replayed, Server and Broadcast are. `Replication.BumpMe()` after changes (`GRAD_BC_TransmissionComponent.c`) | Nothing for now; apply the same pattern in every component |
| 5 Slots | Partly | No Scenario Framework. Each world has layer files (`_INIT`, `OBJECTIVES`, `SPAWNPOINTSGROUPS`, `managers`), with the manager components on the game-mode entity and values set as component attributes, copied per map (the vehicle-buying notes say "mirror to Everon"). Server-side parameters go through a typed `SCR_MissionHeader` subclass plus the server JSON (`Scripts/Game/MissionHeader/GRAD_BC_MissionHeader.c`) | Scenario Framework versus custom slot prefabs is still untested. Per-map copies of attributes are the weak point; a shared config resource avoids them |
| 6 Garrisons | Open | AI is used only for one civilian scout driver: `SCR_AIGroup`, a forced-move waypoint prefab, `HOLD_FIRE` combat mode (`GRAD_BC_CivilianScoutManager.c`). The ambient traffic manager spawns about 50 vehicles. No infantry garrison and no trigger-based spawn and despawn | The whole spike |
| 7 Faction-only markers | Answered | Two patterns. A custom map module draws replicated arrays locally (`GRAD_MapMarkerManager.c`); the data reaches every client, so filtering is client-side only. `GRAD_PlayerComponent.c` sends per-player owner RPCs (`RpcDo_Owner_InsertMarker`, `ShowHint`, circle markers) | Use owner RPCs to the opposing faction for direction finding; never replicate the cipher holder |

**Decisions this forces**

- **Base layer:** BreakingContact moved from PSCore to the COALITION lobby for slotting and respawn pools. Decided: Endgame builds on the COALITION Lobby, which shapes M1, M3 and the validator.
- **Vehicle buying works.** The repo's `docs/vehicle-buying-investigation.md` (resolution dated 2026-08-14) says the campaign building menu works under COALITION after three fixes: a modded label check, a prefab whitelist in the content browser, and the budget type set to `VEHICLES`. Budget enforcement is marked not yet verified, and the label-check copy of vanilla code can break silently after game updates.

**Reuse candidates**

- `GRAD_BC_TransmissionComponent` and `GRAD_BC_ToggleRadioTransmission` for the radio phase.
- The JIP sync routine and the owner-RPC marker pattern for every replicated feature.
- `GRAD_BC_VehicleSupplyComponent` for the vehicle budget.
- `GRAD_BC_ReplayManager` as a base for the match log and an after-action replay.

## What COALITION Lobby answers

I also scanned the COALITION Lobby addon (main, commit 195530a), the base BreakingContact runs on. Short result: it gives us slotting, game states, spawn points, vehicle spawners and admin tools, but its wave respawn does not match the GA rules. Paths are relative to `Scripts/Game/Systems/Core/`.

| # | Status | What the repo shows | Still open |
| --- | --- | --- | --- |
| 1 Respawn gate | Partly | `COA_RespawnManager` runs one global wave clock. A server timer counts down once per second and resets at 0; the end time is replicated as a timestamp so all clients count down locally. Players wait in a spectate state until the tick. Tickets and per-slot respawn pools limit how often a slot returns. Staff can toggle wave respawn at runtime. This is a periodic global wave, not GA's per-faction wave that starts when the first member dies and fills to ceil(team / 3) | Per-faction fill-and-timer waves, the 30 s late-join window and the wave size rule need a custom manager or a modded `COA_RespawnManager`. The release step is client-driven (see below): the server does not check the timer, so a modded server check is mandatory |
| 3 Radio | Open | Role enum has `RADIO_TELEPHONE_OPERATOR`, usable as the transmission role | No radio or looping sound code; keep BreakingContact's transmission component |
| 5 Slots | Mostly | Game states `BRIEFING`, `SLOTTING`, `GAME`, `AAR`; slotting groups; mission plugins in `WorkbenchGame/MissionPlugins` for configuration, factions, gamemode and slotting; base mission config `Missions/COA_BaseMissionConfig.conf` | Whether a custom map validator fits beside these plugins |
| 6 Garrisons | Not covered | `COA_ObjectSpawner` and `COA_VehicleSpawner` exist, the vehicle spawner with tickets and a respawn timer. Read in full: the object spawner only places one static prefab on the server at start (no trigger logic, no despawn), and the repo has no infantry AI spawning. COALITION gives us nothing here | Build our own garrison manager (AI group plus waypoint pattern from BreakingContact's scout) and test it in M0 |
| Spawn points | Answered | `COA_SpawnPoint` with a spawn block radius: spawning near enemies can be blocked. Fits FOB-only spawns and the "no spawn next to the enemy" rule | Activating a spawn point only after the FOB is captured |
| Borders | Answered | Game border entities with an enforcement effect | Not needed for the mode; optional play-area limit |

**Decision: COALITION or vanilla.** I recommend building on COALITION, with one custom respawn manager that replaces the global wave clock:

- It brings slotting, briefing and after-action states, staff tools, a mission configuration pipeline, spawn points with block radius and vehicle spawners, which are about three milestones of work (parts of M1, M2 and M7) that BreakingContact already uses in practice.
- The cost is a dependency on a large external addon that GA does not own, so updates can break us. Keep our own logic in `GRAD_Endgame_Core` and touch COALITION classes only through small `modded` overrides.
- If the GA community does not play on COALITION servers, the vanilla route stays open: M1 and M3 grow by roughly one L each, and slotting needs its own solution.

Decided (2026-11-07): Endgame builds on the COALITION Lobby. The wave release step has been read (below); M0 still has to prove in a test that a per-faction wave with a server gate works.

**Wave release, read in full.** The wait is a client loop: `RespawnTimer` in `COA_RespawnManager` counts down to the replicated wave timestamp, and once it has expired and the player has confirmed a spawn point in the respawn menu, the client sends `RpcAsk_RespawnPlayer` (spawn point id only). On the server, `RespawnPlayer` checks that the player is connected and has a faction and that the faction has spawn points, then marks the slot alive and calls `InitilizePlayer`. It does not check the wave time, so a modified client could respawn at once.

Consequences for Endgame:

- **Server gate (must do):** a `modded` `RespawnPlayer` that refuses until the player's faction wave has been released, with the wave state held on the server per faction. The same gate must also sit in the game-mode manager's InitilizePlayer: any client can call RequestInitilizePlayer, which reaches it without a spawn point.
- **Client display:** keep the existing countdown and respawn menu; feed them our per-faction timestamp through the same replicated field, so the HUD and spectate-while-waiting need no rewrite.
- **GA rules in the manager:** wave starts when the first member of a faction dies, releases at 4.5 min, size = ceil(team / 3), 30 s late-join window. The 2.5 min player wait is a per-player minimum before the wave counts them.
- **Size:** M3 stays L, but it is a mod of an existing manager plus a server gate, not a manager from scratch. The no-spectator-while-waiting rule needs the spectate button hidden (`CanSpectateWhileWaiting`).
- **Still to verify in M0:** that `InitilizePlayer` can be reached through RequestInitilizePlayer. Test in M0 whether that spawns a dead slot early, and gate it if so.

## Ready for implementation

Reading the two repos has turned six of the seven spike questions into either an answer or a concrete M0 test. What is left cannot be settled by reading code; it needs Workbench and two running clients. Implementation can start with the M0 backlog below.

| Spike | State after the scans | What M0 still does |
| --- | --- | --- |
| 1 Respawn gate | Answered by code: client-driven wait, no server check | Prove the server gate in `RespawnPlayer` and `InitilizePlayer` blocks an early spawn |
| 2 Hold-action | Answered (BreakingContact) | Copy the pattern for the document and radio sites |
| 3 Radio | Mostly (BreakingContact transmission) | One looping sound at a site |
| 4 Late join | Answered (BreakingContact notes) | Apply in every component |
| 5 Slots | Decided: custom slot prefabs inside COALITION's layer and slotting model | Build one slot prefab and place it on a test world |
| 6 Garrisons | Not covered by either repo | Build and test the garrison manager |
| 7 Faction markers | Answered (owner RPCs) | Reuse |

**M0 backlog, in order**

| # | Task | Done when | Size |
| --- | --- | --- | --- |
| T1 | Create the repo and `GRAD_Endgame_Core` with a dependency on the COALITION Lobby addon; load a test world with the COALITION game mode | The world starts in Workbench with a host and one client | S |
| T2 | Run BreakingContact once on the same setup | Its radio transmission works with two clients, so the reference is known good | S |
| T3 | Modded `RespawnPlayer` and `InitilizePlayer` with a per-faction wave stub and a server gate | A modified request sent before the wave is released is refused; after release it spawns | M |
| T4 | Per-faction wave stub feeding the replicated timestamp the respawn menu already reads | Two clients on one faction see the same countdown, a late joiner sees it too | S |
| T5 | Document-site prop with a copy hold-action and a replicated counter | Progress is visible to both clients and survives a late join | S |
| T6 | Looping radio sound at a prop, played and stopped from replicated state | Starts and stops on all clients | S |
| T7 | Garrison manager: spawn an AI group with a defend waypoint when a player enters a trigger, despawn it when it is cleared or the trigger is left | Group appears, holds, despawns; 10 groups together show the frame cost | M |
| T8 | One custom slot prefab and a `GRAD_Endgame_Config` resource with two values read at runtime | A value changed in the resource shows up in the game without code changes | S |
| T9 | Two-client test routine written down (host plus client, join, late join, die, respawn) | Any developer can repeat it from the page in the repo | S |

**Rule for the end of M0:** every task that fails gets its fallback from the spike table written into the milestone it affects before M1 starts. T3 and T7 are the two that can change the plan; if T7 shows the garrison cost is too high, phase 2 uses fewer and smaller garrisons and the parameter `garrisonTier` defaults down.

**Decisions already taken**

- Base: COALITION Lobby, with our logic in `GRAD_Endgame_Core` and only small `modded` overrides of COALITION classes.
- Reuse from BreakingContact: transmission component, hold-action pattern, join sync routine, owner-RPC markers, vehicle budget and the working building integration.
- Own code: per-faction wave logic with server gate, garrison manager, document and cipher logic, direction finding, config resource, validator.
- All decisions on the server; clients display replicated state only.

**Open items that do not block the start:** budget enforcement of the vehicle menu (checked in M2), whether the validator fits beside COALITION's mission plugins (M7), exact balance values (M8).

## Addon structure and classes

Two addon types keep the mode reusable: one core addon that holds all logic, and one thin addon per map. Names are proposals using the GRAD\_ prefix; the base classes need checking against current Enfusion scripts.

```text
GRAD_Endgame_Core/
  Scripts/Game/GRAD_Endgame/
    GameMode/   Respawn/   Objectives/   Info/
    AI/         UI/        Config/       Log/
  Prefabs/      slots, garrisons, items
  Configs/      base config, faction loadouts
  UI/  Sounds/
  WorkbenchPlugins/   map validator
GRAD_Endgame_<Map>/
  Worlds/  Configs/ (override)  Scenarios/
```

| Class | Responsibility | Runs on | Replicated state |
| --- | --- | --- | --- |
| GRAD\_EndgameGameMode | Phase state machine, win check, owns the config | Server | Current phase, phase end time |
| GRAD\_WaveRespawnManager | Wave queues per faction, timers, releasing spawns | Server | Countdown and fill per faction wave |
| GRAD\_DocumentSite | Copy hold-action, link to garrison, counter increment | Server and clients | Copy progress |
| GRAD\_CipherItem | Pickup, drop on death, holder tracking | Server | Nothing public; the holder stays server-side |
| GRAD\_RadioSite | Transmission hold-action, interrupt rules, direction-finding trigger | Server and clients | Transmission progress, active flag |
| GRAD\_InfoVisibility | Decides which marker or message reaches which faction | Server | Per-faction markers |
| GRAD\_GarrisonManager | Spawns and despawns garrisons by trigger, enforces caps | Server | None |
| GRAD\_EndgameHud | Countdown, phase, own counter, transmission progress | Client | Reads replicated values |
| GRAD\_MatchLog | Writes one line per event for after-action review | Server | None |
| GRAD\_EndgameValidator | Workbench plugin that checks a map's slots | Editor | None |

**Rules that keep it portable**

- No class refers to a map, a named entity or a coordinate; slots are found by component or tag.
- All tunable values come from the config resource, never from constants in code.
- The server owns every decision; clients only display replicated state and send hold-action requests.
- The cipher holder is never replicated to the other faction, so no client can read it.

## Test and release

Every milestone ends with its gate from the table above, run before the next milestone starts.

**Testing**

- **Debug controls from M1:** commands to advance a phase, skip timers and spawn a test cipher, plus a `debugTimeScale` config value that shortens all timers, so a full match can be run in minutes.
- **Multiplayer from the start:** every gate is checked with a host and at least one more client, because replication and late-join errors only show with two machines or instances. Workbench can run a local server with clients; confirm the exact setup in M0.
- **Late-join check at every gate:** a player joins after the state has changed and must see the right phase, timers and markers.
- **Information check at M6:** one row of the information table at a time, with a player of each faction.

**Playtests**

From M5 on, a short session with GA players once per milestone, with the match log reviewed afterwards. From M8, full 8v8 sessions with one parameter group changed per session, as in the balance plan.

**Versioning and publishing**

- Core and map addons carry the same version number and are published together; a map addon pins the core version it needs, so a Workshop update cannot silently break a map.
- Each release has a changelog and a settings list generated from the config, so the announcement can state the settings the way GA's mission threads do.

**Release checklist**

- [ ] All milestone gates passed on the current build
- [ ] Validator passes on every published map
- [ ] Config defaults match the documented parameter table
- [ ] Core and map addons versioned, pinned and uploaded together
- [ ] Changelog and generated settings list written
- [ ] One clean 8v8 run on the published build, not the dev build

## Cutlines and risks

If time runs short, features go in this order, first dropped first. Wave respawn, direction finding and the document threshold are never cut, because they define the mode.

1. Destroyable radio sites
2. Vehicle-mounted radio sites
3. Gamemaster radio injects
4. Delayed direction-finding mode (keep off and immediate)
5. Observer view
6. The `anyPlayer` option for who may transmit (keep signaller only)
7. 16v16 support (keep 8v8)

| Risk | Effect | Mitigation | Where it shows |
| --- | --- | --- | --- |
| Respawn cannot be hooked cleanly | Wave system needs custom spawn handling | Spike 1 first; fallback in the table above | M0, M3 |
| Replication and late-join errors | Wrong timers or markers for some players | Server-authoritative design; late-join check at every gate | M1 to M6 |
| AI load on a full server | Frame drops in phase 2 and 3 | Garrison caps, despawn rules, measured at M8 | M4, M8 |
| Vehicle spawning depends on the campaign building menu | Depot stays empty or the menu fails | Reuse BreakingContact's working integration (modded label check, prefab whitelist, budget type VEHICLES); its budget enforcement is still unverified, so test it in M2 and keep a direct depot spawn as fallback; re-check after game updates | M2 |
| Workshop version drift between core and maps | A map breaks after a core update | Same version number, pinned dependency, published together | M7, M8 |
| Scope growth from the concept doc | Milestones slip | Cutline list above; any new feature replaces one on the list | Throughout |

**Assumptions**

Sizes are rough and assume one developer. The Enfusion class names, hooks and Workbench features named here are not verified and are tested in M0. The match length, timer defaults and transmission time are untested starting values from the concept doc.
