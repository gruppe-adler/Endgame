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
//! All tunable Endgame values. Create one resource of this class (Configs/GRAD_Endgame_Config.conf)
//! in Workbench: Resource Browser > right click > New > Config > GRAD_Endgame_Config.
//! Rule: no gameplay number lives in code, only here.
//!
//! Every time value is returned through a getter that divides by the debug time scale.
//! Values marked OPEN in the description are undecided (see "Decisions" in the plan); their
//! defaults are neutral, which means "feature off" wherever that is possible.
[BaseContainerProps(configRoot: true)]
class GRAD_Endgame_Config
{
	// --- Wave respawn (rules decided 2026-11-10, see "Wave rules" in the plan) ---
	[Attribute(defvalue: "270", desc: "Wave clock in seconds, counted from the death of the oldest player still waiting.", category: "Respawn")]
	protected float m_fWaveSeconds;

	[Attribute(defvalue: "0", desc: "Per-player minimum wait in seconds before a dead player counts for a wave. 0 = a player who dies just before the release goes with it.", category: "Respawn")]
	protected float m_fMinPlayerWaitSeconds;

	[Attribute(defvalue: "3", desc: "Wave size = ceil(team size / this divisor). The size is a MINIMUM: a wave only releases once at least that many players wait. Everyone waiting is released, there is no cap.", params: "1 100 1", category: "Respawn")]
	protected int m_iWaveSizeDivisor;

	[Attribute(defvalue: "1200", desc: "Longest wait in seconds. When the oldest waiting death is this old, everyone waiting is released even if the wave is not full.", category: "Respawn")]
	protected float m_fMaxWaitSeconds;

	[Attribute(defvalue: "0", desc: "Seconds after a release in which a player who dies still goes at once. 0 = off.", category: "Respawn")]
	protected float m_fLateJoinSeconds;

	[Attribute(defvalue: "1", desc: "Release every waiting player of a side when its FOB becomes held.", category: "Respawn")]
	protected bool m_bFreeRespawnOnFob;

	[Attribute(defvalue: "1", desc: "Release every waiting player of both sides when the radio phase begins.", category: "Respawn")]
	protected bool m_bFreeRespawnOnRadioPhase;

	[Attribute(defvalue: "1", desc: "No respawn in the radio phase; dead players spectate.", category: "Respawn")]
	protected bool m_bEndPhasePermadeath;

	[Attribute(defvalue: "30", desc: "Seconds between the start of the radio phase and permadeath switching on, so the free respawn can be used.", category: "Respawn")]
	protected float m_fRadioPhaseGraceSeconds;

	[Attribute(defvalue: "0", desc: "Allow dead players to spectate while they wait for their wave.", category: "Respawn")]
	protected bool m_bSpectateWhileWaiting;

	// --- Faction keys, as in COALITION's registry ---
	[Attribute(defvalue: "BLUFOR", desc: "COALITION faction key of side one.", category: "Factions")]
	protected string m_sFactionKeyOne;

	[Attribute(defvalue: "OPFOR", desc: "COALITION faction key of side two.", category: "Factions")]
	protected string m_sFactionKeyTwo;

	[Attribute(defvalue: "FIA", desc: "Faction key of the AI militia.", category: "Factions")]
	protected string m_sMilitiaFactionKey;

	// --- FOB and vehicles ---
	// FOB capture (decided 2026-11-10): the FOB is taken when its garrison is wiped out, and the
	// side's spawn points move there. No hold time, no radius, no second spawn.

	[Attribute(defvalue: "0", uiwidget: UIWidgets.ComboBox, desc: "Decided 2026-11-10: OFF (no restriction; document sites are hidden until phase 2 instead). Not implemented beyond OFF.", enums: ParamEnumArray.FromEnum(GRAD_Endgame_ERestrictionMode), category: "FOB")]
	protected GRAD_Endgame_ERestrictionMode m_eRestrictionMode;

	[Attribute(defvalue: "400", desc: "Seconds until a FOB vehicle that was destroyed or driven away respawns on its spot (Sugar Lake value).", category: "Vehicles")]
	protected float m_fTransportRespawnSeconds;

	[Attribute(defvalue: "500", desc: "A FOB vehicle further than this from its spot counts as taken away and is replaced (Sugar Lake value).", category: "Vehicles")]
	protected float m_fTransportRespawnRadius;

	// --- Documents ---
	[Attribute(defvalue: "10", desc: "Seconds to copy the documents at a site. Not read by the action yet: the prefab's Duration still decides.", category: "Documents")]
	protected float m_fCopySeconds;

	[Attribute(defvalue: "5", desc: "Document sites the server activates per match, chosen at random from all placed sites. 0 = all placed sites.", category: "Documents")]
	protected int m_iActiveDocumentSites;

	[Attribute(defvalue: "0.1", desc: "Largest accepted difference in distance to cover between the sides when choosing document sites: |sum A - sum B| / (sum A + sum B). 0.1 = 10 %.", params: "0 1 0.01", category: "Documents")]
	protected float m_fSiteBalanceTolerance;

	[Attribute(defvalue: "1", desc: "Copies one side may take from one site (decided 2026-11-10: 1). 0 = unlimited.", category: "Documents")]
	protected int m_iCopiesPerSitePerFaction;

	[Attribute(defvalue: "3", desc: "Copies (= sites, at one per site) a side needs to start the radio phase (decided 2026-11-10: 3). Default only: an admin can change it in the lobby before the mission starts. 0 = never (admin only).", category: "Documents")]
	protected int m_iDocumentThreshold;

	[Attribute(defvalue: "150", desc: "Radius in metres of the area marker shown for a document site (when exact positions are off).", category: "Documents")]
	protected float m_fDocumentAreaRadius;

	[Attribute(defvalue: "0", desc: "Task markers show the exact site instead of an area.", category: "Documents")]
	protected bool m_bExactDocumentPositions;

	[Attribute(defvalue: "0", desc: "A side sees the opponent's document count.", category: "Documents")]
	protected bool m_bShowEnemyProgress;

	// --- Cipher and radio ---
	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Inventory item prefab carrying GRAD_Endgame_CipherComponent.", params: "et", category: "Radio")]
	protected ResourceName m_sCipherPrefab;

	[Attribute(defvalue: "5", desc: "Metres the operator may move away from the radio set before the transmission is interrupted.", category: "Radio")]
	protected float m_fOperatorRange;

	[Attribute(defvalue: "0", desc: "Defenders are told where the cipher lies.", category: "Radio")]
	protected bool m_bCipherRevealToDefenders;

	[Attribute(defvalue: "2", desc: "Radio sites the server activates per match.", category: "Radio")]
	protected int m_iActiveRadioSites;

	[Attribute(defvalue: "720", desc: "Seconds a transmission takes. Untested starting value.", category: "Radio")]
	protected float m_fTransmissionSeconds;

	[Attribute(defvalue: "0", uiwidget: UIWidgets.ComboBox, desc: "What happens to the progress when the operator leaves or dies.", enums: ParamEnumArray.FromEnum(GRAD_Endgame_ETransmissionInterrupt), category: "Radio")]
	protected GRAD_Endgame_ETransmissionInterrupt m_eTransmissionInterrupt;

	[Attribute(defvalue: "0", uiwidget: UIWidgets.ComboBox, desc: "Who may start a transmission.", enums: ParamEnumArray.FromEnum(GRAD_Endgame_ETransmissionRole), category: "Radio")]
	protected GRAD_Endgame_ETransmissionRole m_eTransmissionRole;

	[Attribute(defvalue: "1", uiwidget: UIWidgets.ComboBox, desc: "When the opponent gets a fix on a transmitting site.", enums: ParamEnumArray.FromEnum(GRAD_Endgame_EDirectionFinding), category: "Radio")]
	protected GRAD_Endgame_EDirectionFinding m_eDirectionFinding;

	[Attribute(defvalue: "0", uiwidget: UIWidgets.ComboBox, desc: "Search area or bearing line.", enums: ParamEnumArray.FromEnum(GRAD_Endgame_EDirectionFindingAccuracy), category: "Radio")]
	protected GRAD_Endgame_EDirectionFindingAccuracy m_eDirectionFindingAccuracy;

	[Attribute(defvalue: "60", desc: "OPEN. Seconds between two direction-finding fixes.", category: "Radio")]
	protected float m_fDfIntervalSeconds;

	[Attribute(defvalue: "300", desc: "OPEN. Radius in metres of the direction-finding search area.", category: "Radio")]
	protected float m_fDfAreaRadius;

	[Attribute(defvalue: "{EFFB3D8065A486E3}", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Audio project of the loop a transmitting radio site plays. Default: the vanilla deployed-radio chatter COALITION uses at rally points, as a placeholder.", params: "acp", category: "Radio")]
	protected ResourceName m_sRadioLoopProject;

	[Attribute(defvalue: "SOUND_DEPLOYED_RADIO_CHATTER", desc: "Event in that audio project. It must loop; how far it carries is set in the audio project (target about 150 m).", category: "Radio")]
	protected string m_sRadioLoopEvent;

	[Attribute(defvalue: "1", desc: "Militia guards the radio sites.", category: "Radio")]
	protected bool m_bMilitiaAtRadioSites;

	// --- AI ---
	// FIA militia (decided 2026-11-10). Suggested vanilla groups from Prefabs/Groups/INDFOR, pick in
	// Workbench: LIGHT = sentry team + fire team (about 6), MEDIUM = rifle squad + fire team (about
	// 12), HEAVY = rifle squad + machine-gun team + fire team (about 16). FIA must be hostile to both
	// player factions in the faction manager (vanilla Conflict makes it friendly to the US).
	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Group prefabs of a light garrison, all spawned together. Suggestion: FIA sentry team + FIA fire team.", params: "et", category: "AI")]
	protected ref array<ResourceName> m_aGarrisonLight;

	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Group prefabs of a medium garrison. Suggestion: FIA rifle squad + FIA fire team.", params: "et", category: "AI")]
	protected ref array<ResourceName> m_aGarrisonMedium;

	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Group prefabs of a heavy garrison. Suggestion: FIA rifle squad + FIA machine-gun team + FIA fire team.", params: "et", category: "AI")]
	protected ref array<ResourceName> m_aGarrisonHeavy;

	[Attribute(defvalue: "0", desc: "0 = every garrison spawns at game start (decided 2026-11-10). 1 = each spawns when the first player comes near.", category: "AI")]
	protected bool m_bGarrisonSpawnOnApproach;

	// --- HUD sound ---
	[Attribute(defvalue: "1", desc: "Typewriter strike for each character typed in a HUD widget, bell at 100 %.", category: "HUD")]
	protected bool m_bTypewriterSound;

	[Attribute(defvalue: "", uiwidget: UIWidgets.ResourcePickerThumbnail, desc: "Audio project with the typewriter events.", params: "acp", category: "HUD")]
	protected ResourceName m_sTypewriterSoundProject;

	[Attribute(defvalue: "GRAD_Endgame_TypeKey", desc: "Event in the audio project for one key strike.", category: "HUD")]
	protected string m_sTypewriterKeyEvent;

	[Attribute(defvalue: "GRAD_Endgame_TypeBell", desc: "Event in the audio project for the bell.", category: "HUD")]
	protected string m_sTypewriterBellEvent;

	// --- Match ---
	[Attribute(defvalue: "0", desc: "Extra time limit of the radio phase in minutes. 0 = none; the whole match is limited by COALITION's m_iTimeLimitMinutes (60, decided 2026-11-10), which ends as a draw.", category: "Match")]
	protected float m_fPhase3TimeLimitMinutes;

	[Attribute(defvalue: "1", desc: "A side with no living players in the radio phase loses; the other side wins (decided 2026-11-10).", category: "Match")]
	protected bool m_bEliminationEndsMatch;

	// --- Debug ---
	[Attribute(defvalue: "1", desc: "Divides every timer. 1 = real time, 10 = ten times faster. Must be 1 for a release.", params: "0.1 100 0.1", category: "Debug")]
	protected float m_fDebugTimeScale;

	[Attribute(defvalue: "0", desc: "Also print every match log line and extra debug lines to the console.", category: "Debug")]
	protected bool m_bDebugLog;

	[Attribute(defvalue: "0", desc: "Run the built-in self tests (wave rules, site selection) once when the match starts and print the results to the log. Replaces the script console.", category: "Debug")]
	protected bool m_bRunSelfTests;

	//------------------------------------------------------------------------------------------------
	protected float Scaled(float seconds)
	{
		return seconds / Math.Max(m_fDebugTimeScale, 0.01);
	}

	// Respawn
	float GetWaveSeconds() { return Scaled(m_fWaveSeconds); }
	float GetMinPlayerWaitSeconds() { return Scaled(m_fMinPlayerWaitSeconds); }
	int GetWaveSizeDivisor() { return Math.Max(m_iWaveSizeDivisor, 1); }
	float GetMaxWaitSeconds() { return Scaled(m_fMaxWaitSeconds); }
	float GetLateJoinSeconds() { return Scaled(m_fLateJoinSeconds); }
	bool GetFreeRespawnOnFob() { return m_bFreeRespawnOnFob; }
	bool GetFreeRespawnOnRadioPhase() { return m_bFreeRespawnOnRadioPhase; }
	bool GetEndPhasePermadeath() { return m_bEndPhasePermadeath; }
	float GetRadioPhaseGraceSeconds() { return Scaled(m_fRadioPhaseGraceSeconds); }
	bool GetSpectateWhileWaiting() { return m_bSpectateWhileWaiting; }

	// Factions
	string GetFactionKeyOne() { return m_sFactionKeyOne; }
	string GetFactionKeyTwo() { return m_sFactionKeyTwo; }
	string GetMilitiaFactionKey() { return m_sMilitiaFactionKey; }
	bool IsPlayerFaction(string key) { return key == m_sFactionKeyOne || key == m_sFactionKeyTwo; }

	// FOB and vehicles
	GRAD_Endgame_ERestrictionMode GetRestrictionMode() { return m_eRestrictionMode; }
	float GetTransportRespawnSeconds() { return Scaled(m_fTransportRespawnSeconds); }
	float GetTransportRespawnRadius() { return m_fTransportRespawnRadius; }

	// Documents
	float GetCopySeconds() { return Scaled(m_fCopySeconds); }
	int GetActiveDocumentSites() { return m_iActiveDocumentSites; }
	float GetSiteBalanceTolerance() { return m_fSiteBalanceTolerance; }
	int GetCopiesPerSitePerFaction() { return m_iCopiesPerSitePerFaction; }
	int GetDocumentThreshold() { return m_iDocumentThreshold; }
	bool GetExactDocumentPositions() { return m_bExactDocumentPositions; }
	float GetDocumentAreaRadius() { return m_fDocumentAreaRadius; }
	bool GetShowEnemyProgress() { return m_bShowEnemyProgress; }

	// Cipher and radio
	bool GetCipherRevealToDefenders() { return m_bCipherRevealToDefenders; }
	ResourceName GetCipherPrefab() { return m_sCipherPrefab; }
	float GetOperatorRange() { return m_fOperatorRange; }
	int GetActiveRadioSites() { return m_iActiveRadioSites; }
	float GetTransmissionSeconds() { return Scaled(m_fTransmissionSeconds); }
	GRAD_Endgame_ETransmissionInterrupt GetTransmissionInterrupt() { return m_eTransmissionInterrupt; }
	GRAD_Endgame_ETransmissionRole GetTransmissionRole() { return m_eTransmissionRole; }
	GRAD_Endgame_EDirectionFinding GetDirectionFinding() { return m_eDirectionFinding; }
	GRAD_Endgame_EDirectionFindingAccuracy GetDirectionFindingAccuracy() { return m_eDirectionFindingAccuracy; }
	float GetDfIntervalSeconds() { return Scaled(m_fDfIntervalSeconds); }
	float GetDfAreaRadius() { return m_fDfAreaRadius; }
	ResourceName GetRadioLoopProject() { return m_sRadioLoopProject; }
	string GetRadioLoopEvent() { return m_sRadioLoopEvent; }
	bool GetMilitiaAtRadioSites() { return m_bMilitiaAtRadioSites; }

	// AI
	void GetGarrisonGroups(GRAD_Endgame_EGarrisonTier tier, notnull array<ResourceName> groups)
	{
		groups.Clear();
		array<ResourceName> source;
		switch (tier)
		{
			case GRAD_Endgame_EGarrisonTier.LIGHT: source = m_aGarrisonLight; break;
			case GRAD_Endgame_EGarrisonTier.MEDIUM: source = m_aGarrisonMedium; break;
			case GRAD_Endgame_EGarrisonTier.HEAVY: source = m_aGarrisonHeavy; break;
		}
		if (source)
			groups.InsertAll(source);
	}
	bool GetGarrisonSpawnOnApproach() { return m_bGarrisonSpawnOnApproach; }

	// HUD sound
	bool GetTypewriterSound() { return m_bTypewriterSound; }
	ResourceName GetTypewriterSoundProject() { return m_sTypewriterSoundProject; }
	string GetTypewriterKeyEvent() { return m_sTypewriterKeyEvent; }
	string GetTypewriterBellEvent() { return m_sTypewriterBellEvent; }

	// Match
	float GetPhase3TimeLimitSeconds() { return Scaled(m_fPhase3TimeLimitMinutes * 60); }
	bool GetEliminationEndsMatch() { return m_bEliminationEndsMatch; }

	// Debug
	float GetDebugTimeScale() { return m_fDebugTimeScale; }
	bool GetDebugLog() { return m_bDebugLog; }
	bool GetRunSelfTests() { return m_bRunSelfTests; }
}
