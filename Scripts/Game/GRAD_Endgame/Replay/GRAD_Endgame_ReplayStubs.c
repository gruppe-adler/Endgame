//------------------------------------------------------------------------------------------------
//! Small stand-ins for BreakingContact classes the copied replay code refers to. Everything else
//! in the Replay folder is a copy of BreakingContact's replay (GRAD_BC_* renamed GRAD_Endgame_*).
class GRAD_Endgame_ReplayDebug
{
	//! BreakingContact's IsDebugMode(): here the config's debug log.
	static bool IsDebugMode()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		return cfg && cfg.GetDebugLog();
	}

	//! COALITION faction keys to the "USSR" / "US" icon naming of the copied map layer. Side two
	//! (OPFOR) is the Soviet side, side one (BLUFOR) the US side.
	static bool IsOpforFactionKey(string key)
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		return cfg && key == cfg.GetFactionKeyTwo();
	}

	static bool IsBluforFactionKey(string key)
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		return cfg && key == cfg.GetFactionKeyOne();
	}
}

//------------------------------------------------------------------------------------------------
//! BreakingContact's loading-text HUD display. Not ported: the copied manager null-checks it, and
//! FindGamestateDisplay() returns null, so these calls never run.
class GRAD_Endgame_Gamestate
{
	void ShowPersistentText(string text) {}
	void UpdateText(string text) {}
	void UpdateProgress(float progress) {}
	void HideText() {}
	void SetHeadline(string text) {}
}

//------------------------------------------------------------------------------------------------
//! BreakingContact's live-marker map module. Not ported; it is not in the map config, so
//! GetMapModule() finds nothing and the copied "replay mode" switches do nothing.
class GRAD_Endgame_MapMarkerManager : SCR_MapModuleBase
{
	void SetReplayMode(bool enabled) {}
}
