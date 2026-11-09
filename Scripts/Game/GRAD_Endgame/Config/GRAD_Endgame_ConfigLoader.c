//------------------------------------------------------------------------------------------------
//! Loads and caches the GRAD_Endgame_Config resource. Every system reads its values through Get().
class GRAD_Endgame_ConfigLoader
{
	static const ResourceName CONFIG_PATH = "{C1DFC56704124BF6}Configs/GRAD_Endgame_Config.conf";

	protected static ref GRAD_Endgame_Config s_Config;
	protected static bool s_bTried;

	//! eturn the config, or null if the resource is missing or invalid (logged once)
	static GRAD_Endgame_Config Get()
	{
		if (s_Config || s_bTried)
			return s_Config;

		s_bTried = true;
		Resource holder = BaseContainerTools.LoadContainer(CONFIG_PATH);
		if (!holder || !holder.IsValid())
		{
			Print("[GRAD_Endgame] Config resource not found: " + CONFIG_PATH, LogLevel.ERROR);
			return null;
		}

		s_Config = GRAD_Endgame_Config.Cast(BaseContainerTools.CreateInstanceFromContainer(holder.GetResource().ToBaseContainer()));
		if (!s_Config)
			Print("[GRAD_Endgame] Config resource has the wrong class: " + CONFIG_PATH, LogLevel.ERROR);

		return s_Config;
	}
}
