//------------------------------------------------------------------------------------------------
//! Gives access to the Endgame options. They are the mission header (GRAD_Endgame_Config), so a
//! dedicated server's missionHeader overrides are already in them.
//!
//! Playing the world directly in the Workbench starts no mission, so there is no header: then the
//! mission file itself is loaded as the config (same values, no overrides).
class GRAD_Endgame_ConfigLoader
{
	static const ResourceName CONFIG_PATH = "{3C7A91E05B2D48F6}Missions/Endgame89.conf";

	protected static ref GRAD_Endgame_Config s_Fallback;
	protected static bool s_bTried;

	//! Return the config, or null if neither the running mission nor the mission file gives one (logged once)
	static GRAD_Endgame_Config Get()
	{
		GRAD_Endgame_Config header = GRAD_Endgame_Config.Cast(GetGame().GetMissionHeader());
		if (header)
			return header;

		if (s_Fallback || s_bTried)
			return s_Fallback;

		s_bTried = true;
		Resource holder = BaseContainerTools.LoadContainer(CONFIG_PATH);
		if (!holder || !holder.IsValid())
		{
			Print("[GRAD_Endgame] Mission file not found: " + CONFIG_PATH, LogLevel.ERROR);
			return null;
		}

		s_Fallback = GRAD_Endgame_Config.Cast(BaseContainerTools.CreateInstanceFromContainer(holder.GetResource().ToBaseContainer()));
		if (!s_Fallback)
			Print("[GRAD_Endgame] The mission file is not a GRAD_Endgame_Config: " + CONFIG_PATH, LogLevel.ERROR);
		else
			Print("[GRAD_Endgame] No mission header is running; using the mission file as the config.", LogLevel.NORMAL);

		return s_Fallback;
	}
}
