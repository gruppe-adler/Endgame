//------------------------------------------------------------------------------------------------
//! All tunable Endgame values. Create one resource of this class (Configs/GRAD_Endgame_Config.conf)
//! in Workbench: Resource Browser > right click > New > Config > GRAD_Endgame_Config.
//! Rule: no gameplay number lives in code, only here. Defaults are the untested starting values
//! from the concept doc.
[BaseContainerProps(configRoot: true)]
class GRAD_Endgame_Config
{
	// --- Wave respawn ---
	[Attribute(defvalue: "270", desc: "Seconds from the first death of a faction until its wave is released (4.5 min).", category: "Respawn")]
	protected float m_fWaveSeconds;

	[Attribute(defvalue: "150", desc: "Per-player minimum wait in seconds before a dead player counts for a wave (2.5 min).", category: "Respawn")]
	protected float m_fMinPlayerWaitSeconds;

	[Attribute(defvalue: "3", desc: "Wave size = ceil(team size / this divisor).", params: "1 100 1", category: "Respawn")]
	protected int m_iWaveSizeDivisor;

	[Attribute(defvalue: "30", desc: "Late-join window in seconds (not used by the wave stub yet).", category: "Respawn")]
	protected float m_fLateJoinSeconds;

	// --- Faction keys, as in COALITION's registry ---
	[Attribute(defvalue: "BLUFOR", desc: "COALITION faction key of side one.", category: "Factions")]
	protected string m_sFactionKeyOne;

	[Attribute(defvalue: "OPFOR", desc: "COALITION faction key of side two.", category: "Factions")]
	protected string m_sFactionKeyTwo;

	// --- Debug ---
	[Attribute(defvalue: "1", desc: "Divides every timer. 1 = real time, 10 = ten times faster.", params: "0.1 100 0.1", category: "Debug")]
	protected float m_fDebugTimeScale;

	float GetWaveSeconds() { return m_fWaveSeconds / Math.Max(m_fDebugTimeScale, 0.01); }
	float GetMinPlayerWaitSeconds() { return m_fMinPlayerWaitSeconds / Math.Max(m_fDebugTimeScale, 0.01); }
	float GetLateJoinSeconds() { return m_fLateJoinSeconds / Math.Max(m_fDebugTimeScale, 0.01); }
	int GetWaveSizeDivisor() { return Math.Max(m_iWaveSizeDivisor, 1); }
	string GetFactionKeyOne() { return m_sFactionKeyOne; }
	string GetFactionKeyTwo() { return m_sFactionKeyTwo; }
	float GetDebugTimeScale() { return m_fDebugTimeScale; }
}
