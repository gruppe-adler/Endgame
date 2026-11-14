# Garrison orders research (FIA guards, Endgame 1989)

Read-only research, 2026-11-14. Nothing in the repo was edited.

## TL;DR

1. Root cause of "AI just stands there" is probably the world, not only the missing waypoint. `worlds/endgame_Layers/default.layer` has a bare `SCR_AIWorld : SCR_AIWorld_Eden.et` with no NavmeshWorldComponent override. The server log says `No navmesh file specified! Will initialize empty navmesh world` for both 'Soldiers' and 'BTRlike'. With an empty navmesh no AI can path, so every Move/Defend/Patrol waypoint fails (units can only act in place).
2. Fix, proven in 3 other addons on the same Eden parent: override the two NavmeshWorldComponents in our layer to point at the vanilla GM_Eden navmesh (snippet below). The vanilla Eden navmeshes exist in the base game.
3. Then add a Defend waypoint (vanilla `AIWaypoint_Defend.et`, GUID found in GME source) per group. CRX is not a garrison addon, it is a behaviour overhaul. It is not needed for waypoints.

## 5. Navmesh (most important)

Facts from files:
- `Endgame/worlds/endgame.ent` line 2: `Parent "{853E92315D1D9EFE}worlds/Eden/Eden.ent"`.
- `Endgame/worlds/endgame_Layers/default.layer` lines 1-3: `SCR_AIWorld : "{70CCCF16487C927F}Prefabs/AI/SCR_AIWorld_Eden.et" { coords 4813.21 28 11100 }`, no components override.
- `Endgame/server_log.txt` lines 741-744: `PATHFINDING : Navmesh Init 'Soldiers'` / `PATHFINDING(W): No navmesh file specified! Will initialize empty navmesh world.`, same for 'BTRlike'.
- So the vanilla prefab `SCR_AIWorld_Eden.et` itself carries no navmesh file (it is only a prefab-level default; I could not read the .et because game_read/game_browse could not see inside the paks, see "unverified").
- BI wiki (Navmesh Tutorial, via wiki_search, https://community.bistudio.com/wiki/Arma_Reforger:Navmesh_Tutorial): for a sub-scene of an existing terrain, put SCR_AIWorld in the default layer and point the `NavmeshFilesConfig` of its NavmeshWorldComponent at a .nmn. Wiki says the prefab holds three NavmeshWorldComponents (Soldiers, BTRLike (vehicles), LowRes for streaming). The log shows only two inits (Soldiers, BTRlike) for the Eden prefab.

Vanilla Eden navmesh files (grep -a on `C:\SteamLibrary\steamapps\common\Arma Reforger\addons\data\resourceDatabase.rdb`; the earlier grep was too narrow):
- `worlds/GameMaster/Navmeshes/GM_Eden.nmn`, `GM_Eden_vehicles.nmn`
- `worlds/MP/Navmeshes/CTI_Campaign_Eden.nmn`, `CTI_Campaign_Eden_vehicles.nmn`, `CTI_Campaign_HQC_Eden(.nmn|_vehicles.nmn)`, `Coop_CombatOps_Eden(.nmn|_vehicles.nmn)`, `LowResEden.nmn`
- configs: `Configs/Navmesh/Navmesh_GM_Eden_Soldier.conf`, `Navmesh_GM_Eden_Vehicle.conf`, `Navmesh_CTI_Campaign_Eden_Soldier.conf`, `..._Vehicle.conf`, `Navmesh_CTI_Campaign_HQC_Eden_*.conf`
- also Arland/Cain/Everon/Montignac equivalents, plus `worlds/SP/...` (no Eden).

Working pattern on a SubScene of Eden (GUIDs copied from files, not guessed):
- `addons\CoTemplate\worlds\CoTemplate_Eden_Layers\GM_vanilla.layer` (parent `worlds/Eden/Eden.ent`, see CoTemplate_Eden.ent line 2), and identical in `addons\GRAD-On-The-Fly\Worlds\OTF_Eden_Layers\gm_manager.layer`, and `addons\CoTemplate\Prefabs\aiworld.et`:

```
SCR_AIWorld : "{70CCCF16487C927F}Prefabs/AI/SCR_AIWorld_Eden.et" {
 components {
  NavmeshWorldComponent "{5584F30E67F617AD}" {
   NavmeshSettings NavmeshWorld "{50FC63BEBE3973C5}" {
    NavmeshFilesConfig BaseNavmeshFilesConfig "{5AF8B9A1594F5532}" : "{8BBC73472092902E}Configs/Navmesh/Navmesh_GM_Eden_Soldier.conf" {
    }
   }
  }
  NavmeshWorldComponent "{5584F30EEFEE1223}" {
   NavmeshSettings ChimeraNavmeshWorld "{50FC63BEBE3973C5}" {
    NavmeshFilesConfig BaseNavmeshFilesConfig "{5CF6FEFBD1A09749}" {
     NavmeshFile "{C35ECF3276824653}worlds/GameMaster/Navmeshes/GM_Eden_vehicles.nmn"
    }
   }
  }
 }
 coords ...
}
```
- Alternative with direct .nmn GUIDs (plus LowRes streaming, on the generic `SCR_AIWorld.et` {E0A05C76552E7F58}): `addons\GME_AR\addons\GME\worlds\GM_Everon\GM_Full_Layers\default.layer` uses `{AD5F99CD7C59D5E0}worlds/GameMaster/Navmeshes/GM_Eden.nmn`, `{C35ECF3276824654}.../GM_Eden_vehicles.nmn`, `{5E19E60F26327356}worlds/MP/Navmeshes/LowResEden.nmn`. `addons\WITA_nov\WITA_nov_Layers\AI_systems.layer` uses `{A07353FE17EB2C49}worlds/MP/Navmeshes/CTI_Campaign_Eden.nmn`.
  (Note the vehicles GUID differs by one last digit, ...653 vs ...654, between files; I did not verify which is right. The `...653` one is used in 3 files incl. the template that overrides the Eden prefab. Verify in Workbench's resource picker.)
- Easiest way to do it: in Workbench open the world, select the SCR_AIWorld entity, and set the NavmeshFilesConfig of the Soldiers component to `Navmesh_GM_Eden_Soldier.conf` (or the GM_Eden.nmn) using the resource picker, so Workbench writes the correct GUIDs.

Caveats (unverified):
- GM_Eden.nmn is generated for the vanilla Eden Game Master world. Our world adds no buildings (SubScene parent only). If our layer places structures (FOB/site prefabs), they will have no navmesh holes/cut-outs, minor. `SCR_AIWorld.RequestNavmeshRebuild(min,max)` / `RequestNavmeshRebuildEntity(IEntity)` exist in API (`interfaceSCR__AIWorld.html`) if rebuilding around placed objects is ever wanted (not checked whether it works on a dedicated server).
- Which navmesh variant (GM vs CTI_Campaign vs Coop_CombatOps) best matches us: GM_Eden is the generic full-island one; CTI/Coop ones are generated for their mission layouts. Not measured. Use GM_Eden unless the AI misbehaves.
- Dedicated server must be able to load the .nmn (it is in vanilla paks, so yes) and memory/startup cost is the island navmesh.
- The dedicated server was in this state for the logged session, so "units stand still" is consistent with the empty navmesh. Not proven that nothing else is wrong (no waypoint was ever assigned in that run anyway, `m_sWaypointPrefab` is empty).

## 1. Vanilla waypoints

### Prefabs (names from resourceDatabase.rdb grep -a, base game `addons\data\resourceDatabase.rdb`)
`Prefabs/AI/Waypoints/`: AIWaypoint_Defend, _DefendSmall, _Defend_Small, _Defend_Large, _Defend_CP, _Defend_Hierarchy, _Defend_Large_CO, _Defend_Small_CO_Interior, _Defend_ConflictBaseTeamBackup, _Defend_ConflictBaseTeamPatrol, AIWaypoint_Patrol, _Patrol_Hierarchy, AIWaypoint_Cycle, AIWaypoint_ForcedMove, AIWaypoint_Move, AIWaypoint_Scout, AIWaypoint_SearchAndDestroy, AIWaypoint_Attack, AIWaypoint_ObservationPost, AIWaypoint_Wait, AIWaypoint_AtEase, AIWaypoint_Loiter_CO, AIWaypoint_Suppress, AIWaypoint_Follow, AIWaypoint_GetIn*, AIWaypoint_GetOut*, AIWaypoint_Animation*, AIWaypoint_Base, etc.
Editable variants `PrefabsEditable/Auto/AI/Waypoints/E_AIWaypoint_{Defend,DefendSmall,ForcedMove,Move,Patrol,Scout,SearchAndDestroy,Wait,...}.et`.

GUIDs actually found in files (only these; others I have path but no GUID because the rdb is binary):
- `{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et` in `addons\GME_AR\addons\GME\Scripts\Game\GME\Editor\Modules\AI\GME_Modules_AddGarrison.c`
- `{06E1B6EBD480C6E0}Prefabs/AI/Waypoints/AIWaypoint_ForcedMove.et` in `BreakingContact\Scripts\Game\Components\GRAD_BC_CivilianScoutManager.c` and `GRAD-Traffic\scripts\Game\Components\SCR_AmbientTrafficManager.c`
- `{3C790BB71C5CD960}PrefabsEditable/Auto/AI/Waypoints/E_AIWaypoint_ForcedMove.et` and `{FFF9518F73279473}.../E_AIWaypoint_Move.et` in `GME_Modules_SpawnReinforcements.c`
- Patrol, Cycle, Scout, SearchAndDestroy GUIDs: NOT found. Get them from the Workbench resource browser (right click > copy resource name) or just use the resource picker attribute.

### Class API (all confirmed in `C:\SteamLibrary\steamapps\common\Arma Reforger Tools\Workbench\docs\ArmaReforgerScriptAPIPublic\html\`)
- `interfaceAIGroup.html`: `AddWaypoint(AIWaypoint)`, `AddWaypointAt(w, index)`, `RemoveWaypoint`, `CompleteWaypoint`, `GetCurrentWaypoint()`, `GetWaypoints(out array<AIWaypoint>)`, `GetAgents`, `GetAgentsCount`, `GetLeaderEntity`; events `OnWaypointCompleted(AIWaypoint)` (GME uses `group.GetOnWaypointCompleted()` invoker, see below).
- `interfaceAIWaypoint.html`: `SetCompletionRadius(float)`, `GetCompletionRadius`, `SetCompletionType(EAIWaypointCompletionType)` (enum All, Leader, Any: `group__AI.html`), `SetCompletionYPrecision`.
- `interfaceSCR__AIWaypoint.html`: `SetPriorityLevel(float)`, `GetPriorityLevel`, `AddSetting(SCR_AISettingBase)`, `GetSettings`.
- `interfaceSCR__DefendWaypoint.html` (source `Game/AI/Group/SCR_DefendWaypoint.c`): `SetFastInit(bool)`, `AddDefendPreset(SCR_DefendWaypointPreset)`, `SetCurrentDefendPreset(int)`, `ClearDefendPresets`, inherits `SCR_TimedWaypoint.SetHoldingTime(float)/GetHoldingTime()`.
- `interfaceSCR__DefendWaypointPreset.html`: `SetUseTurrets(bool)`, `SetFractionOfSA(float)`, `SetTagsForSearch(array<string>)`, `SetPresetName(string)`. "SA" is presumably smart actions (see section 2).
- `interfaceSCR__AIGroupUtilityComponent.html`: `SetCombatMode(EAIGroupCombatMode)`, `GetCombatModeExternal`; proven in BreakingContact (`utility.SetCombatMode(EAIGroupCombatMode.HOLD_FIRE)`). For our hostile guards the default FIRE_AT_WILL is right.
- `interfaceSCR__AIGroup.html`: `GetGroupUtilityComponent()`, plus `AddWaypointsDynamic`, `AddWaypointsStatic` (these appear to be the spawn-time waypoint lists on the group prefab; signature of dynamic uses `SCR_WaypointPrefabLocation`; not verified how they are filled).
- Cycle: `interfaceAIWaypointCycle.html` ("a series of waypoints repeating for an infinite or finite time"); member list not inspected further, so how to add child waypoints from script is UNVERIFIED.

Defaults of the prefab (completion radius, hold time, FastInit) are in the .et, which I could not read. Set them explicitly in script.

### Working code patterns (copy these)
GME garrison (defend waypoint, per soldier group) `GME_Modules_AddGarrison.c`:
```
SCR_AIWaypoint waypoint = SCR_AIWaypoint.Cast(GetGame().SpawnEntityPrefab(Resource.Load("{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et"), null, params));
waypoint.SetCompletionRadius(waypointRadius);
newGroup.AddWaypoint(waypoint);
```
(params: `ETransformMode.WORLD`, `params.Transform[3]` = position.)

GRAD-Traffic `SCR_AmbientTrafficManager.c` CreateWaypointForGroup (ForcedMove, spawn with world, then SCR cast for priority, completion radius, AddWaypoint, and a 2000 ms `CallLater` delay before assigning so units are in place):
```
AIWaypoint wp = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(Resource.Load("{06E1B6EBD480C6E0}Prefabs/AI/Waypoints/AIWaypoint_ForcedMove.et"), GetGame().GetWorld(), wpParams));
SCR_AIWaypoint wpScr = SCR_AIWaypoint.Cast(wp); if (wpScr) wpScr.SetPriorityLevel(2000);
wp.SetCompletionRadius(...); group.AddWaypoint(wp);
```
BreakingContact `GRAD_BC_CivilianScoutManager.c`: group from `Prefabs/Groups/Group_Base.et` as SCR_AIGroup, `SetDeleteWhenEmpty(false)`, waypoint assigned via `CallLater(AssignScoutWaypoint, 2000, false)`.
GME reinforcements `GME_Modules_SpawnReinforcements.c`: `m_pCrewGroup.GetOnWaypointCompleted().Insert(OnLZReached);` (callback signature `(AIWaypoint wp)`).

Our existing code (GarrisonComponent.c lines 306-314) already does the same: spawns `m_sWaypointPrefab` at group spawn params and `group.AddWaypoint(waypoint)`. It casts to AIWaypoint, fine. Issues to note: waypoint is added immediately although the group members spawn over frames (log: `agents at spawn 0`); Traffic and BC both add after ~2 s, GME adds after members spawned (it hooks `GME_GetOnDoneSpawningMembers()`, a GME modded method, not vanilla). Adding a waypoint to a group before members exist is probably fine for a group-level waypoint list, but this is unverified; the safe copy is a delayed assign (CallLater 2000) or waiting until `GetAgentsCount() > 0`.

## 2. Garrisoning buildings / positions

What vanilla offers (from docs and GME source):
- Defend waypoint: `SCR_DefendWaypoint` + `SCR_AIDefendWaypointState` (docs). Presets carry `m_bUseTurrets`, `m_fFractionOfSA`, `m_aTagsForSearch`. `SCR_AIGetDefendWaypointParameters` has a `m_tagsArray` (tags for search). The mechanism appears to be: the group finds smart actions (`AISmartActionComponent` / `SCR_AISmartActionSentinelComponent`, `interfaceSCR__AISmartActionSentinelComponent.html` with `GetLookPosition`, `GetLookDirectionRange`, `GetDesiredStance`, `GetUseBinoculars`) within the waypoint's completion radius, optionally filtered by tags, and sends a fraction of the members to them (guard posts in towers/bunkers/sandbag positions) while the rest hold. That is a reading of the API names, not confirmed by reading the code (SCR_DefendWaypoint.c is inside the pak and unreadable with my tools).
- Therefore: a Defend waypoint with a radius covering the site is the vanilla "garrison/defend" and automatically uses whatever sentinel smart actions are on prefabs nearby. Needs: navmesh (section 5), and smart-action-bearing objects (vanilla bunkers/towers/sandbags; vanilla buildings are not required) in range. Nothing like "building slots" was found in the API names I grepped (only smart actions).
- GME's building garrison (`GME_Modules_AddGarrison.c`) rolls its own: queries `SCR_AISmartActionSentinelComponent` on the building, uses `GME_BuildingPosQuery` (GME class, not vanilla) for other interior positions, makes one single-agent group per soldier (`GME_GroupHelper.CreateEmptyGroup`, `group.RemoveAgent/AddAgent`), puts an `AIWaypoint_Defend` at the spot with completion radius 0 (sentinel posts) or 3 (positions), then `entity.SetTransform`. GME is an installed mod addon (`addons\GME_AR`), the .c source is readable. Usable as a pattern, but it depends on GME classes.
- Vanilla defend prefab variants exist for Conflict (`_Defend_ConflictBaseTeamBackup`, `_Large_CO`, `_Small_CO_Interior`), meaning vanilla ships tuned radii/presets. Not inspected.

## 3. CRX AI

Installed as pak only: `addons\CRXEnfusionA.I._5F268647F8A1A1F4` (v 1.3.72, "Stable v.1.7.0.41", ID `CRX_EAI_1-7-0-41`; description in `meta`; depends only on `58D0FB3206B6F859` (base game)). No other addon's source references it by name, except `addons\GRAD Coop Template\addon.gproj` which lists `5F268647F8A1A1F4` as a dependency (no code uses found). No Endgame code references CRX.
`data.pak` is compressed (grep -a for class names inside it only gave 5 hits for `class CRX_`, unusable), but `resourceDatabase.rdb` holds plaintext file paths. From it (grep -a):
- It is a modded-behaviour package under `Scripts/Game/CRX_EAI_1-7-0-41_1-3-72/`: `Modded/SCR_AIWaypoint.c`, `Modded/SCR_AIWorld.c`, `Modded/SCR_AIGroup/SCR_AIGroup.c`, `SCR_AIGroupUtilityComponent.c`, `SCR_AIGroupInfoComponent.c`, `Modded/SCR_AIBehaviors/*` (Attack, MoveBehavior, MoveAndInvestigate, Retreat...), `Modded/SCR_Components/SCR_WaypointAreaMeshComponent.c`, `Modded/SCR_ScenarioFrameworkSlotAI.c`, `Modded/SCR_AIComponents/*`, `AIDecoratorScripted/CRX_AIHoldPosition.c`, `CRX/CRX_AIGameMaster/Waypoint/CRX_AIGroupWaypointCompletionRadiusEditorAttribute.c`, `..ForceMoveAttackChanceEditorAttribute.c`, `..WaypointOnDetectedEditorAttribute.c`, `CRX_AIWaypointPriorityLevelEditorAttribute.c`, `CRX_AIWaypointVehicleSpeedEditorAttribute.c`, `Group/CRX_AIGroupReturnToPositionOriginTypeEditorAttribute.c`, `Group/CRX_AIGroupInvestigate*`, `CRX_AIGroupCombatBehaviorTypeEditorAttribute.c`, `CRX_AIGroupFormationEditorAttribute.c`, `ContextActions/HighCommand/CRX_AIHighCommand{Hold,Move,GetIn}ContextAction.c`, `CRX/CRX_AIScenarioFramework/CRX_AIGroupFormationSetting.c`.
- The meta text says: Hold Position (+radius, +update), Return To Position (Never/Always/Attacking/Investigating), combat behaviour types (CQB/Urban/Default/Defensive/Offensive), formations, stance, investigate options, rank/skill. Those are Game-Master (editor attribute) and AI-behaviour features, not script-callable garrison orders that I could confirm.
- Missions with names `CRX_Arland_Field_Optimized_Building_Garrison.ent` / `_Building_Search.ent` exist in it, i.e. its garrison test uses vanilla mechanism in a mission; content not readable.
- Also contains `Prefabs/AI/SCR_AIWorld.et` override and test-world navmeshes (`Missions/CRX_Arland_Field_Trench_*_Layers/NavMesh/*.nmn`), irrelevant to Eden.
- Public API of the CRX classes (e.g. method names to set hold position or return-to-origin from script) could NOT be determined. Dependency on it would also change AI behaviour globally and the license is APL-ND. Verdict: not needed to get waypoints; optional bonus for better combat AI and `ReturnToPosition` after fights. To use any CRX script API we would need its source or the CRX author's docs.

## 4. Other addons spawning AI with waypoints (readable source)
- `BreakingContact/Scripts/Game/Components/GRAD_BC_CivilianScoutManager.c` (ForcedMove, group prefab, SetDeleteWhenEmpty, combat mode, RoadNetworkManager.GetReachableWaypointInRoad).
- `GRAD-Traffic/scripts/Game/Components/SCR_AmbientTrafficManager.c` (ForcedMove, priority 2000, delayed assign, `group.GetCurrentWaypoint()` check logging).
- `GME_AR/addons/GME/...` AddGarrison (Defend), SpawnReinforcements (ForcedMove/Move + `GetOnWaypointCompleted`), `GME_CompletionRadiusEditorAttribute.c`, `GME_CompletionTypeEditorAttribute.c` (SetCompletionType), `GME_WaypointEntitySerializer.c` (persistence).
- GRAD-Undead, GRAD Ruha GM FIA, OnTheFly, GRAD-On-The-Fly, GRAD Undead Exp: no `Waypoint` usage in any .c (grep). OnTheFly/On-The-Fly worlds only useful as navmesh layer examples.

## Recommendation for GRAD_Endgame_GarrisonComponent.c

World first (must be done or nothing moves):
1. In Workbench, in `worlds/endgame_Layers/default.layer`, set the SCR_AIWorld NavmeshWorldComponents as in the snippet above (GM_Eden Soldier config + GM_Eden_vehicles; optionally LowResEden like GME). Restart the dedicated server and check the log no longer says "No navmesh file specified". Navmesh loading by position can also be requested via `SCR_AIWorld.RequestNavmeshLoad(vector)` (API-listed, not needed normally).

Script:
2. Defend waypoint per group (not one waypoint shared between groups; a waypoint belongs to one group): spawn `{93291E72AC23930F}Prefabs/AI/Waypoints/AIWaypoint_Defend.et` at the site centre (or the group's ring position), cast to `SCR_DefendWaypoint`/`SCR_AIWaypoint`, `SetCompletionRadius(r)` where r about 15-30 for a light guard (radio/documents) and 30-50 for a FOB (tune), optionally `SetHoldingTime(...)` and `SetFastInit(true)`. Assign it with a delay of 1-2 s (or when `group.GetAgentsCount() > 0`), copying the Traffic/BC pattern. Make `m_sWaypointPrefab` default to the Defend GUID. Keep `SetDeleteWhenEmpty(false)`.
3. Optional patrol variant for the bigger groups (medium/heavy): a second group-level patrol around the site; use AIWaypoint_Patrol or Cycle (GUIDs not found; pick via resource picker). Cycle API for adding children is unverified.
4. Keep combat mode default (FIRE_AT_WILL), faction FIA is on the group prefab already.
5. Log `group.GetCurrentWaypoint()` a few seconds after assign like Traffic does, to confirm the assignment worked, and log the group leader's position change to spot navmesh failures.

Confirmed vs unconfirmed API
- Confirmed in docs: AIGroup.AddWaypoint/AddWaypointAt/RemoveWaypoint/GetCurrentWaypoint/GetWaypoints/GetAgentsCount; AIWaypoint.SetCompletionRadius/SetCompletionType/SetCompletionYPrecision; SCR_AIWaypoint.SetPriorityLevel; SCR_DefendWaypoint.SetFastInit/AddDefendPreset/SetCurrentDefendPreset/ClearDefendPresets; SCR_TimedWaypoint.SetHoldingTime; SCR_DefendWaypointPreset.SetUseTurrets/SetFractionOfSA/SetTagsForSearch; SCR_AIGroupUtilityComponent.SetCombatMode; SCR_AIWorld.RequestNavmeshLoad/RequestNavmeshRebuild/GetNavmeshWorldComponent/GetRoadNetworkManager.
- Confirmed by working source in other addons: spawn waypoint prefab + SCR_AIWaypoint cast + SetCompletionRadius + AddWaypoint; delayed assignment; SetPriorityLevel; SetCombatMode on SCR_AIGroupUtilityComponent; `group.GetOnWaypointCompleted()` (GME, from `SCR_AIGroup`/`AIGroup` event invoker).
- Unconfirmed: exact semantics/defaults of the Defend prefab (radius, hold time, how smart actions/tags are used, whether `m_fFractionOfSA` etc. apply by default); whether adding a waypoint before members exist works in all cases; Cycle/Patrol scripting; the `...653` vs `...654` GM_Eden_vehicles GUID; that GM_Eden.nmn is the best fit; anything about CRX public API; `AddWaypointsDynamic/Static` usage.
- Not readable by me: vanilla .c/.et content (enfusion-mcp `game_read`/`game_browse` returned "not found"/empty for `Prefabs/AI/...`, so the game paks were not accessible through it; only the rdb path strings and the API html docs were usable).

## Files this is based on
- `C:\Users\nomisum\Documents\my games\ArmaReforgerWorkbench\addons\Endgame\worlds\endgame.ent`, `worlds\endgame_Layers\default.layer`, `server_log.txt` (lines 740-744, 1000+), `Scripts\Game\GRAD_Endgame\AI\GRAD_Endgame_GarrisonComponent.c`
- `...\addons\CoTemplate\worlds\CoTemplate_Eden_Layers\GM_vanilla.layer`, `...\CoTemplate\Prefabs\aiworld.et`, `...\GRAD-On-The-Fly\Worlds\OTF_Eden_Layers\gm_manager.layer`, `...\GME_AR\addons\GME\worlds\GM_Everon\GM_Full_Layers\default.layer`, `...\WITA_nov\WITA_nov_Layers\AI_systems.layer`
- `...\GME_AR\addons\GME\Scripts\Game\GME\Editor\Modules\AI\GME_Modules_AddGarrison.c`, `...\Modules\Reinforcements\GME_Modules_SpawnReinforcements.c`
- `...\BreakingContact\Scripts\Game\Components\GRAD_BC_CivilianScoutManager.c`, `...\GRAD-Traffic\scripts\Game\Components\SCR_AmbientTrafficManager.c`
- `...\CRXEnfusionA.I._5F268647F8A1A1F4\{addon.gproj,meta,resourceDatabase.rdb,data.pak}`, `...\GRAD Coop Template\addon.gproj`
- `C:\SteamLibrary\steamapps\common\Arma Reforger\addons\data\resourceDatabase.rdb` (grep -a for .nmn, Navmesh, Waypoints/ paths)
- API docs in `C:\SteamLibrary\steamapps\common\Arma Reforger Tools\Workbench\docs\ArmaReforgerScriptAPIPublic\html\`: interfaceAIGroup, interfaceAIWaypoint, interfaceSCR__AIWaypoint, interfaceSCR__DefendWaypoint, interfaceSCR__DefendWaypointPreset, interfaceSCR__AIDefendWaypointState, interfaceSCR__AIGroupUtilityComponent, interfaceSCR__AIGroup, interfaceSCR__AIWorld, interfaceSCR__AISmartActionSentinelComponent, interfaceAIWaypointCycle, group__AI.
- BI wiki via wiki_search: Arma_Reforger:Navmesh_Tutorial, World_Editor:_Navmesh_Tool_Tutorial.
