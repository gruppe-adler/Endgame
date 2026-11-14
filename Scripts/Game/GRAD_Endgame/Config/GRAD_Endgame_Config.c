//------------------------------------------------------------------------------------------------
enum GRAD_Endgame_ETransmissionInterrupt
{
	PAUSE,
	RESET
}

//------------------------------------------------------------------------------------------------
enum GRAD_Endgame_ETransmissionRole
{
	SIGNALLER_ONLY,
	ANY_PLAYER
}

//------------------------------------------------------------------------------------------------
enum GRAD_Endgame_EDirectionFinding
{
	OFF,
	IMMEDIATE,
	DELAYED
}

//------------------------------------------------------------------------------------------------
enum GRAD_Endgame_EDirectionFindingAccuracy
{
	AREA,
	BEARING
}

//------------------------------------------------------------------------------------------------
enum GRAD_Endgame_ERestrictionMode
{
	OFF,
	DAMAGE,
	TELEPORT
}

//------------------------------------------------------------------------------------------------
//! Wave respawn rules.
[BaseContainerProps()]
class GRAD_Endgame_SettingsRespawn
{
	[Attribute(defvalue: "270", desc: "Wave clock in seconds, counted from the death of the oldest player still waiting.")]
	float m_fWaveSeconds = 270.0;

	[Attribute(defvalue: "0", desc: "Per-player minimum wait in seconds before a dead player counts for a wave. 0 = a player who dies just before the release goes with it.")]
	float m_fMinPlayerWaitSeconds = 0.0;

	[Attribute(defvalue: "3", desc: "Wave size = ceil(team size / this divisor). The size is a MINIMUM: a wave only releases once at least that many players wait. Everyone waiting is released, there is no cap.", params: "1 100 1")]
	int m_iWaveSizeDivisor = 3;

	[Attribute(defvalue: "1200", desc: "Longest wait in seconds. When the oldest waiting death is this old, everyone waiting is released even if the wave is not full.")]
	float m_fMaxWaitSeconds = 1200.0;

	[Attribute(defvalue: "0", desc: "Seconds after a release in which a player who dies still goes at once. 0 = off.")]
	float m_fLateJoinSeconds = 0.0;

	[Attribute(defvalue: "1", desc: "Release every waiting player of a side when its FOB becomes held.")]
	bool m_bFreeRespawnOnFob = true;

	[Attribute(defvalue: "1", desc: "Release every waiting player of both sides when the radio phase begins.")]
	bool m_bFreeRespawnOnRadioPhase = true;

	[Attribute(defvalue: "1", desc: "No respawn in the radio phase; dead players spectate.")]
	bool m_bEndPhasePermadeath = true;

	[Attribute(defvalue: "30", desc: "Seconds between the start of the radio phase and permadeath switching on, so the free respawn can be used.")]
	float m_fRadioPhaseGraceSeconds = 30.0;

	[Attribute(defvalue: "0", desc: "Allow dead players to spectate while they wait for their wave.")]
	bool m_bSpectateWhileWaiting = false;
}

//------------------------------------------------------------------------------------------------
//! Faction keys, as in COALITION's registry.
[BaseContainerProps()]
class GRAD_Endgame_SettingsFactions
{
	[Attribute(defvalue: "BLUFOR", desc: "COALITION faction key of side one.")]
	string m_sFactionKeyOne = "BLUFOR";

	[Attribute(defvalue: "OPFOR", desc: "COALITION faction key of side two.")]
	string m_sFactionKeyTwo = "OPFOR";

	[Attribute(defvalue: "FIA", desc: "Faction key of the AI militia.")]
	string m_sMilitiaFactionKey = "FIA";
}

//------------------------------------------------------------------------------------------------
//! FOB and the vehicles at it.
[BaseContainerProps()]
class GRAD_Endgame_SettingsFob
{
	[Attribute(defvalue: "0", uiwidget: UIWidgets.ComboBox, desc: "Decided 2026-11-10: OFF (no restriction; document sites are hidden until phase 2 instead). Not implemented beyond OFF.", enums: ParamEnumArray.FromEnum(GRAD_Endgame_ERestrictionMode))]
	GRAD_Endgame_ERestrictionMode m_eRestrictionMode = GRAD_Endgame_ERestrictionMode.OFF;

	[Attribute(defvalue: "10", desc: "Seconds after a FOB vehicle was destroyed or driven off its spot until a new one spawns there (once the spot is free). Real seconds: the debug time scale does not apply.")]
	float m_fTransportRespawnSeconds = 10.0;

	[Attribute(defvalue: "20", desc: "A FOB vehicle further than this many metres from its spot has left it and is replaced.")]
	float m_fTransportRespawnRadius = 20.0;
}

//------------------------------------------------------------------------------------------------
//! Document sites and copying.
[BaseContainerProps()]
class GRAD_Endgame_SettingsDocuments
{
	[Attribute(defvalue: "10", desc: "Not used: the hold time of the copy is the Duration of the copy action in the document site prefab (the widget bar follows that value). Kept so old configs load.")]
	float m_fCopySeconds = 10.0;

	[Attribute(defvalue: "5", desc: "Document sites the server activates per match, chosen at random from all placed sites. 0 = all placed sites.")]
	int m_iActiveDocumentSites = 5;

	[Attribute(defvalue: "0.1", desc: "Largest accepted difference in distance to cover between the sides when choosing document sites: |sum A - sum B| / (sum A + sum B). 0.1 = 10 %.", params: "0 1 0.01")]
	float m_fSiteBalanceTolerance = 0.1;

	[Attribute(defvalue: "1", desc: "Copies one side may take from one site (decided 2026-11-10: 1). 0 = unlimited.")]
	int m_iCopiesPerSitePerFaction = 1;

	[Attribute(defvalue: "3", desc: "Copies (= sites, at one per site) a side needs to start the radio phase (decided 2026-11-10: 3). Default only: an admin can change it in the lobby before the mission starts. 0 = never (admin only).")]
	int m_iDocumentThreshold = 3;

	[Attribute(defvalue: "150", desc: "Radius in metres of the area marker shown for a document site (when exact positions are off).")]
	float m_fDocumentAreaRadius = 150.0;

	[Attribute(defvalue: "0", desc: "Task markers show the exact site instead of an area.")]
	bool m_bExactDocumentPositions = false;

	[Attribute(defvalue: "0", desc: "A side sees the opponent's document count.")]
	bool m_bShowEnemyProgress = false;
}

//------------------------------------------------------------------------------------------------
//! FIA militia: scaling, garrisons, vehicle patrols.
[BaseContainerProps()]
class GRAD_Endgame_SettingsFia
{
	[Attribute(defvalue: "1", desc: "Scale the FIA garrisons of the document sites with the number of players at the start of phase 1. FOB, radio and cipher garrisons and the vehicle patrols are never scaled.")]
	bool m_bFiaScaleEnabled = true;

	[Attribute(defvalue: "3", desc: "FIA soldiers at document sites per slotted player (the budget for all document sites together).", params: "0 20 0.5")]
	float m_fFiaPerPlayer = 3.0;

	[Attribute(defvalue: "1", desc: "Difficulty multiplier on the FIA budget: for example 0.7 easy, 1 normal, 1.4 hard.", params: "0.1 3 0.1")]
	float m_fFiaDifficulty = 1.0;

	[Attribute(defvalue: "80", desc: "Upper limit of the FIA budget for all document sites together (server performance).", params: "0 500 1")]
	int m_iFiaMaxDocumentSoldiers = 80;

	[Attribute(defvalue: "8", desc: "Soldiers of a LIGHT garrison (about: the sum of its group prefabs). Used to pick a tier from the budget.", params: "1 100 1")]
	int m_iFiaSoldiersLight = 8;

	[Attribute(defvalue: "12", desc: "Soldiers of a MEDIUM garrison.", params: "1 100 1")]
	int m_iFiaSoldiersMedium = 12;

	[Attribute(defvalue: "16", desc: "Soldiers of a HEAVY garrison.", params: "1 100 1")]
	int m_iFiaSoldiersHeavy = 16;

	[Attribute(defvalue: "0", desc: "With a small budget, document sites may have no FIA at all. Off: every document site keeps at least a LIGHT garrison.")]
	bool m_bFiaAllowNoneAtDocuments = false;

	[Attribute(defvalue: "2", desc: "How many FIA vehicle patrols drive between the document sites (0 = none).", params: "0 10 1")]
	int m_iFiaPatrolCount = 2;

	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Vehicle prefab of an FIA patrol. Suggestion: UAZ469_PKM_FIA.", params: "et")]
	ResourceName m_sFiaPatrolVehicle = "{22B327C6752EC4D4}Prefabs/Vehicles/Wheeled/UAZ469/UAZ469_PKM_FIA.et";

	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Group prefab that crews the vehicle (soldiers beyond the seats are removed). Suggestion: FIA fire team.", params: "et")]
	ResourceName m_sFiaPatrolCrewGroup = "Prefabs/Groups/INDFOR/Group_FIA_FireTeam.et";

	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Waypoint prefab the patrol drives to, one stop after the other.", params: "et")]
	ResourceName m_sFiaPatrolWaypoint = "{FFF9518F73279473}PrefabsEditable/Auto/AI/Waypoints/E_AIWaypoint_Move.et";

	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Waypoint that makes the crew get out of the vehicle (vanilla AIWaypoint_GetOut).", params: "et")]
	ResourceName m_sFiaPatrolGetOut = "{C40316EE26846CAB}Prefabs/AI/Waypoints/AIWaypoint_GetOut.et";

	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Waypoint that makes the crew get into the nearest vehicle (vanilla AIWaypoint_GetInNearest).", params: "et")]
	ResourceName m_sFiaPatrolGetIn = "{B049D4C74FBC0C4D}Prefabs/AI/Waypoints/AIWaypoint_GetInNearest.et";

	[Attribute(defvalue: "25", desc: "Metres around a stop that count as arrived.", params: "5 100 1")]
	float m_fFiaPatrolRadius = 25.0;

	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Group prefabs of a light garrison, all spawned together. Suggestion: FIA sentry team + FIA fire team.", params: "et")]
	ref array<ResourceName> m_aGarrisonLight = { "Prefabs/Groups/INDFOR/Group_FIA_FireTeam.et", "Prefabs/Groups/INDFOR/Group_FIA_Team_AT.et" };

	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Group prefabs of a medium garrison. Suggestion: FIA rifle squad + FIA fire team.", params: "et")]
	ref array<ResourceName> m_aGarrisonMedium = { "Prefabs/Groups/INDFOR/Group_FIA_RifleSquad.et", "Prefabs/Groups/INDFOR/Group_FIA_FireTeam.et" };

	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Group prefabs of a heavy garrison. Suggestion: FIA rifle squad + FIA machine-gun team + FIA fire team.", params: "et")]
	ref array<ResourceName> m_aGarrisonHeavy = { "Prefabs/Groups/INDFOR/Group_FIA_RifleSquad.et", "Prefabs/Groups/INDFOR/Group_FIA_FireTeam.et", "Prefabs/Groups/INDFOR/Group_FIA_Team_AT.et" };

	[Attribute(defvalue: "0", desc: "0 = every garrison spawns at game start (decided 2026-11-10). 1 = each spawns when the first player comes near.")]
	bool m_bGarrisonSpawnOnApproach = false;
}

//------------------------------------------------------------------------------------------------
//! Cipher, radio sites and transmission.
[BaseContainerProps()]
class GRAD_Endgame_SettingsRadio
{
	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Inventory item prefab carrying GRAD_Endgame_CipherComponent.", params: "et")]
	ResourceName m_sCipherPrefab = "{6F70209937C07ACB}Prefabs/GRAD_Endgame_CipherItem.et";

	[Attribute(defvalue: "5", desc: "Metres the operator may move away from the radio set before the transmission is interrupted.")]
	float m_fOperatorRange = 5.0;

	[Attribute(defvalue: "0", desc: "Defenders are told where the cipher lies.")]
	bool m_bCipherRevealToDefenders = false;

	[Attribute(defvalue: "25", desc: "When the cipher carrier goes down (dead or unconscious) both sides get a task and a HUD notice with the last known position, blurred to within this many metres. 0 = off.", params: "0 200 1")]
	float m_fCipherLastKnownRadius = 25;

	[Attribute(defvalue: "2", desc: "Radio sites the server activates per match.")]
	int m_iActiveRadioSites = 2;

	[Attribute(defvalue: "720", desc: "Seconds a transmission takes. Untested starting value.")]
	float m_fTransmissionSeconds = 720.0;

	[Attribute(defvalue: "0", uiwidget: UIWidgets.ComboBox, desc: "What happens to the progress when the operator leaves or dies.", enums: ParamEnumArray.FromEnum(GRAD_Endgame_ETransmissionInterrupt))]
	GRAD_Endgame_ETransmissionInterrupt m_eTransmissionInterrupt = GRAD_Endgame_ETransmissionInterrupt.PAUSE;

	[Attribute(defvalue: "1", uiwidget: UIWidgets.ComboBox, desc: "Who may start a transmission. ANY_PLAYER (default, decided 2026-11-14): whoever carries the cipher. SIGNALLER_ONLY: only a player in the radio operator slot.", enums: ParamEnumArray.FromEnum(GRAD_Endgame_ETransmissionRole))]
	GRAD_Endgame_ETransmissionRole m_eTransmissionRole = GRAD_Endgame_ETransmissionRole.ANY_PLAYER;

	[Attribute(defvalue: "1", uiwidget: UIWidgets.ComboBox, desc: "When the opponent gets a fix on a transmitting site.", enums: ParamEnumArray.FromEnum(GRAD_Endgame_EDirectionFinding))]
	GRAD_Endgame_EDirectionFinding m_eDirectionFinding = GRAD_Endgame_EDirectionFinding.IMMEDIATE;

	[Attribute(defvalue: "0", uiwidget: UIWidgets.ComboBox, desc: "Search area or bearing line.", enums: ParamEnumArray.FromEnum(GRAD_Endgame_EDirectionFindingAccuracy))]
	GRAD_Endgame_EDirectionFindingAccuracy m_eDirectionFindingAccuracy = GRAD_Endgame_EDirectionFindingAccuracy.AREA;

	[Attribute(defvalue: "60", desc: "OPEN. Seconds between two direction-finding fixes.")]
	float m_fDfIntervalSeconds = 60.0;

	[Attribute(defvalue: "300", desc: "OPEN. Radius in metres of the direction-finding search area.")]
	float m_fDfAreaRadius = 300.0;

	[Attribute(defvalue: "{EFFB3D8065A486E3}", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Audio project of the loop a transmitting radio site plays. Default: the vanilla deployed-radio chatter COALITION uses at rally points, as a placeholder.", params: "acp")]
	ResourceName m_sRadioLoopProject = "{EFFB3D8065A486E3}";

	[Attribute(defvalue: "SOUND_DEPLOYED_RADIO_CHATTER", desc: "Event in that audio project. It must loop; how far it carries is set in the audio project (target about 150 m).")]
	string m_sRadioLoopEvent = "SOUND_DEPLOYED_RADIO_CHATTER";

	[Attribute(defvalue: "1", desc: "Militia guards the radio sites.")]
	bool m_bMilitiaAtRadioSites = true;
}

//------------------------------------------------------------------------------------------------
//! HUD sound (typewriter).
[BaseContainerProps()]
class GRAD_Endgame_SettingsHud
{
	[Attribute(defvalue: "1", desc: "Typewriter strike for each character typed in a HUD widget, a return at the end of a line, a bell at 100 %.")]
	bool m_bTypewriterSound = true;

	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Audio projects with one key strike each (Sounds/key1.acp to key5.acp). One is picked at random per strike.", params: "acp")]
	ref array<ResourceName> m_aTypewriterKeyProjects = { "{C3002D39994A65CC}Sounds/key1.acp", "{5A1E94A084AD545C}Sounds/key2.acp", "{2D14FC28700FBB2C}Sounds/key3.acp", "{2AD30679168901EF}Sounds/key4.acp", "{5DD96EF1E22BEE9F}Sounds/key5.acp" };

	[Attribute(defvalue: "EG_key", desc: "Event name inside every key audio project.")]
	string m_sTypewriterKeyEvent = "EG_key";

	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Audio project with the bell that rings when a bar reaches 100 % (Sounds/bing.acp).", params: "acp")]
	ResourceName m_sTypewriterBellProject = "{2B0A510098FF0ED0}Sounds/bing.acp";

	[Attribute(defvalue: "EG_bing", desc: "Event name inside the bell audio project.")]
	string m_sTypewriterBellEvent = "EG_bing";

	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Audio project with the carriage return at the end of a typed line (Sounds/return.acp).", params: "acp")]
	ResourceName m_sTypewriterReturnProject = "{5AA445FF0824C807}Sounds/return.acp";

	[Attribute(defvalue: "EG_return", desc: "Event name inside the return audio project.")]
	string m_sTypewriterReturnEvent = "EG_return";
}

//------------------------------------------------------------------------------------------------
//! Phase limits, end conditions, replay.
[BaseContainerProps()]
class GRAD_Endgame_SettingsMatch
{
	[Attribute(defvalue: "0", desc: "Extra time limit of the radio phase in minutes. 0 = none; the whole match is limited by COALITION's m_iTimeLimitMinutes (60, decided 2026-11-10), which ends as a draw.")]
	float m_fPhase3TimeLimitMinutes = 0.0;

	[Attribute(defvalue: "1", desc: "A side with no living players in the radio phase loses; the other side wins (decided 2026-11-10).")]
	bool m_bEliminationEndsMatch = true;

	[Attribute(defvalue: "1", desc: "Join in progress: a player who connects after the match went live may pick a free slot (Shift+O, or the hint) until the phase below begins. Off: COALITION's own behaviour (only during safestart).")]
	bool m_bLateJoinSlotting = true;

	[Attribute(defvalue: "3", uiwidget: UIWidgets.ComboBox, desc: "Late joiners may take a slot until this phase begins. RADIO (default) = open in phases 1 and 2, closed from phase 3 on (decided 2026-11-10; phase 3 also locks every open slot). DOCUMENTS = phase 1 only.", enums: ParamEnumArray.FromEnum(GRAD_Endgame_EPhase))]
	GRAD_Endgame_EPhase m_eLateJoinClosesAt = GRAD_Endgame_EPhase.RADIO;

	[Attribute(defvalue: "1", desc: "Record the match and play it back on the map (BreakingContact's replay, copied) before the after-action screen. Recording interval and maximum replay length are attributes of the GRAD_Endgame_ReplayManager component on the lobby entity.")]
	bool m_bReplayEnabled = true;
}

//------------------------------------------------------------------------------------------------
//! Test aids. Must be off for a release.
[BaseContainerProps()]
class GRAD_Endgame_SettingsDebug
{
	[Attribute(defvalue: "1", desc: "Divides every timer. 1 = real time, 10 = ten times faster. Must be 1 for a release.", params: "0.1 100 0.1")]
	float m_fDebugTimeScale = 1.0;

	[Attribute(defvalue: "0", desc: "Also print every match log line and extra debug lines to the console.")]
	bool m_bDebugLog = false;

	[Attribute(defvalue: "0", desc: "Run the built-in self tests (wave rules, site selection) once when the match starts and print the results to the log. Replaces the script console.")]
	bool m_bRunSelfTests = false;
}

//------------------------------------------------------------------------------------------------
//! All Endgame settings in one object, grouped. Header key: m_GRAD_Endgame.<group>.<field>.
[BaseContainerProps()]
class GRAD_Endgame_Settings
{
	[Attribute(uiwidget: UIWidgets.Object, desc: "Wave respawn rules.")]
	ref GRAD_Endgame_SettingsRespawn m_Respawn = new GRAD_Endgame_SettingsRespawn();

	[Attribute(uiwidget: UIWidgets.Object, desc: "Faction keys, as in COALITION's registry.")]
	ref GRAD_Endgame_SettingsFactions m_Factions = new GRAD_Endgame_SettingsFactions();

	[Attribute(uiwidget: UIWidgets.Object, desc: "FOB and the vehicles at it.")]
	ref GRAD_Endgame_SettingsFob m_Fob = new GRAD_Endgame_SettingsFob();

	[Attribute(uiwidget: UIWidgets.Object, desc: "Document sites and copying.")]
	ref GRAD_Endgame_SettingsDocuments m_Documents = new GRAD_Endgame_SettingsDocuments();

	[Attribute(uiwidget: UIWidgets.Object, desc: "FIA militia: scaling, garrisons, vehicle patrols.")]
	ref GRAD_Endgame_SettingsFia m_Fia = new GRAD_Endgame_SettingsFia();

	[Attribute(uiwidget: UIWidgets.Object, desc: "Cipher, radio sites and transmission.")]
	ref GRAD_Endgame_SettingsRadio m_Radio = new GRAD_Endgame_SettingsRadio();

	[Attribute(uiwidget: UIWidgets.Object, desc: "HUD sound (typewriter).")]
	ref GRAD_Endgame_SettingsHud m_Hud = new GRAD_Endgame_SettingsHud();

	[Attribute(uiwidget: UIWidgets.Object, desc: "Phase limits, end conditions, replay.")]
	ref GRAD_Endgame_SettingsMatch m_Match = new GRAD_Endgame_SettingsMatch();

	[Attribute(uiwidget: UIWidgets.Object, desc: "Test aids. Must be off for a release.")]
	ref GRAD_Endgame_SettingsDebug m_Debug = new GRAD_Endgame_SettingsDebug();
}

//------------------------------------------------------------------------------------------------
//! All tunable Endgame values. This class IS the mission header (a SCR_MissionHeader subclass):
//! the values live in Missions/Endgame89.conf, and a dedicated server can override any of them in
//! its server config under game.gameProperties.missionHeader, by the nested field names, m_GRAD_Endgame.<group>.<field> (see
//! server_config.sample.json). There is no separate config file.
//! Rule: no gameplay number lives in code, only here.
//!
//! Every time value is returned through a getter that divides by the debug time scale.
//! Values marked OPEN in the description are undecided (see "Decisions" in the plan); their
//! defaults are neutral, which means "feature off" wherever that is possible.
[BaseContainerProps()]
class GRAD_Endgame_Config : SCR_MissionHeader
{
	[Attribute(uiwidget: UIWidgets.Object, desc: "All Endgame settings, grouped: Respawn, Factions, Fob, Documents, Fia, Radio, Hud, Match, Debug.")]
	protected ref GRAD_Endgame_Settings m_GRAD_Endgame = new GRAD_Endgame_Settings();

	//------------------------------------------------------------------------------------------------
	//! The settings object (made on demand if the engine left it out).
	protected GRAD_Endgame_Settings S()
	{
		if (!m_GRAD_Endgame)
			m_GRAD_Endgame = new GRAD_Endgame_Settings();
		return m_GRAD_Endgame;
	}

	//------------------------------------------------------------------------------------------------
	protected float Scaled(float seconds)
	{
		return seconds / Math.Max(S().m_Debug.m_fDebugTimeScale, 0.01);
	}

	// Respawn
	float GetWaveSeconds() { return Scaled(S().m_Respawn.m_fWaveSeconds); }
	float GetMinPlayerWaitSeconds() { return Scaled(S().m_Respawn.m_fMinPlayerWaitSeconds); }
	int GetWaveSizeDivisor() { return Math.Max(S().m_Respawn.m_iWaveSizeDivisor, 1); }
	float GetMaxWaitSeconds() { return Scaled(S().m_Respawn.m_fMaxWaitSeconds); }
	float GetLateJoinSeconds() { return Scaled(S().m_Respawn.m_fLateJoinSeconds); }
	bool GetFreeRespawnOnFob() { return S().m_Respawn.m_bFreeRespawnOnFob; }
	bool GetFreeRespawnOnRadioPhase() { return S().m_Respawn.m_bFreeRespawnOnRadioPhase; }
	bool GetEndPhasePermadeath() { return S().m_Respawn.m_bEndPhasePermadeath; }
	float GetRadioPhaseGraceSeconds() { return Scaled(S().m_Respawn.m_fRadioPhaseGraceSeconds); }
	bool GetSpectateWhileWaiting() { return S().m_Respawn.m_bSpectateWhileWaiting; }

	// Factions
	string GetFactionKeyOne() { return S().m_Factions.m_sFactionKeyOne; }
	string GetFactionKeyTwo() { return S().m_Factions.m_sFactionKeyTwo; }
	string GetMilitiaFactionKey() { return S().m_Factions.m_sMilitiaFactionKey; }
	bool IsPlayerFaction(string key) { return key == S().m_Factions.m_sFactionKeyOne || key == S().m_Factions.m_sFactionKeyTwo; }

	// FOB and vehicles
	GRAD_Endgame_ERestrictionMode GetRestrictionMode() { return S().m_Fob.m_eRestrictionMode; }
	float GetTransportRespawnSeconds() { return S().m_Fob.m_fTransportRespawnSeconds; }
	float GetTransportRespawnRadius() { return S().m_Fob.m_fTransportRespawnRadius; }

	// Documents
	float GetCopySeconds() { return Scaled(S().m_Documents.m_fCopySeconds); }
	int GetActiveDocumentSites() { return S().m_Documents.m_iActiveDocumentSites; }
	float GetSiteBalanceTolerance() { return S().m_Documents.m_fSiteBalanceTolerance; }
	int GetCopiesPerSitePerFaction() { return S().m_Documents.m_iCopiesPerSitePerFaction; }
	int GetDocumentThreshold() { return S().m_Documents.m_iDocumentThreshold; }
	bool GetExactDocumentPositions() { return S().m_Documents.m_bExactDocumentPositions; }
	float GetDocumentAreaRadius() { return S().m_Documents.m_fDocumentAreaRadius; }
	bool GetShowEnemyProgress() { return S().m_Documents.m_bShowEnemyProgress; }

	// FIA scaling
	bool GetFiaScaleEnabled() { return S().m_Fia.m_bFiaScaleEnabled; }
	float GetFiaPerPlayer() { return S().m_Fia.m_fFiaPerPlayer; }
	float GetFiaDifficulty() { return S().m_Fia.m_fFiaDifficulty; }
	int GetFiaMaxDocumentSoldiers() { return S().m_Fia.m_iFiaMaxDocumentSoldiers; }
	int GetFiaSoldiersLight() { return S().m_Fia.m_iFiaSoldiersLight; }
	int GetFiaSoldiersMedium() { return S().m_Fia.m_iFiaSoldiersMedium; }
	int GetFiaSoldiersHeavy() { return S().m_Fia.m_iFiaSoldiersHeavy; }
	bool GetFiaAllowNoneAtDocuments() { return S().m_Fia.m_bFiaAllowNoneAtDocuments; }

	// FIA patrols
	int GetFiaPatrolCount() { return S().m_Fia.m_iFiaPatrolCount; }
	ResourceName GetFiaPatrolVehicle() { return S().m_Fia.m_sFiaPatrolVehicle; }
	ResourceName GetFiaPatrolCrewGroup() { return S().m_Fia.m_sFiaPatrolCrewGroup; }
	ResourceName GetFiaPatrolWaypoint() { return S().m_Fia.m_sFiaPatrolWaypoint; }
	ResourceName GetFiaPatrolGetOut() { return S().m_Fia.m_sFiaPatrolGetOut; }
	ResourceName GetFiaPatrolGetIn() { return S().m_Fia.m_sFiaPatrolGetIn; }
	float GetFiaPatrolRadius() { return S().m_Fia.m_fFiaPatrolRadius; }

	// Cipher and radio
	bool GetCipherRevealToDefenders() { return S().m_Radio.m_bCipherRevealToDefenders; }
	float GetCipherLastKnownRadius() { return S().m_Radio.m_fCipherLastKnownRadius; }
	ResourceName GetCipherPrefab() { return S().m_Radio.m_sCipherPrefab; }
	float GetOperatorRange() { return S().m_Radio.m_fOperatorRange; }
	int GetActiveRadioSites() { return S().m_Radio.m_iActiveRadioSites; }
	float GetTransmissionSeconds()
	{
		float seconds = Scaled(S().m_Radio.m_fTransmissionSeconds);
#ifdef WORKBENCH
		seconds *= 0.1;   // local Workbench play only: transmissions run ten times shorter, for testing
#endif
		return seconds;
	}
	GRAD_Endgame_ETransmissionInterrupt GetTransmissionInterrupt() { return S().m_Radio.m_eTransmissionInterrupt; }
	GRAD_Endgame_ETransmissionRole GetTransmissionRole() { return S().m_Radio.m_eTransmissionRole; }
	GRAD_Endgame_EDirectionFinding GetDirectionFinding() { return S().m_Radio.m_eDirectionFinding; }
	GRAD_Endgame_EDirectionFindingAccuracy GetDirectionFindingAccuracy() { return S().m_Radio.m_eDirectionFindingAccuracy; }
	float GetDfIntervalSeconds() { return Scaled(S().m_Radio.m_fDfIntervalSeconds); }
	float GetDfAreaRadius() { return S().m_Radio.m_fDfAreaRadius; }
	ResourceName GetRadioLoopProject() { return S().m_Radio.m_sRadioLoopProject; }
	string GetRadioLoopEvent() { return S().m_Radio.m_sRadioLoopEvent; }
	bool GetMilitiaAtRadioSites() { return S().m_Radio.m_bMilitiaAtRadioSites; }

	// AI
	void GetGarrisonGroups(GRAD_Endgame_EGarrisonTier tier, notnull array<ResourceName> groups)
	{
		groups.Clear();
		array<ResourceName> source;
		switch (tier)
		{
			case GRAD_Endgame_EGarrisonTier.LIGHT: source = S().m_Fia.m_aGarrisonLight; break;
			case GRAD_Endgame_EGarrisonTier.MEDIUM: source = S().m_Fia.m_aGarrisonMedium; break;
			case GRAD_Endgame_EGarrisonTier.HEAVY: source = S().m_Fia.m_aGarrisonHeavy; break;
		}
		if (source)
			groups.InsertAll(source);
	}
	bool GetGarrisonSpawnOnApproach() { return S().m_Fia.m_bGarrisonSpawnOnApproach; }

	// HUD sound
	bool GetTypewriterSound() { return S().m_Hud.m_bTypewriterSound; }
	void GetTypewriterKeyProjects(notnull array<ResourceName> projects)
	{
		projects.Clear();
		if (S().m_Hud.m_aTypewriterKeyProjects)
			projects.InsertAll(S().m_Hud.m_aTypewriterKeyProjects);
	}
	ResourceName GetTypewriterBellProject() { return S().m_Hud.m_sTypewriterBellProject; }
	ResourceName GetTypewriterReturnProject() { return S().m_Hud.m_sTypewriterReturnProject; }
	string GetTypewriterReturnEvent() { return S().m_Hud.m_sTypewriterReturnEvent; }
	string GetTypewriterKeyEvent() { return S().m_Hud.m_sTypewriterKeyEvent; }
	string GetTypewriterBellEvent() { return S().m_Hud.m_sTypewriterBellEvent; }

	// Match
	float GetPhase3TimeLimitSeconds() { return Scaled(S().m_Match.m_fPhase3TimeLimitMinutes * 60); }
	bool GetEliminationEndsMatch() { return S().m_Match.m_bEliminationEndsMatch; }
	bool GetLateJoinSlotting() { return S().m_Match.m_bLateJoinSlotting; }
	GRAD_Endgame_EPhase GetLateJoinClosesAt() { return S().m_Match.m_eLateJoinClosesAt; }

	// Debug
	float GetDebugTimeScale() { return S().m_Debug.m_fDebugTimeScale; }
	bool GetDebugLog() { return S().m_Debug.m_bDebugLog; }
	bool GetRunSelfTests() { return S().m_Debug.m_bRunSelfTests; }
	bool GetReplayEnabled() { return S().m_Match.m_bReplayEnabled; }
}
