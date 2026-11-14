// OpenMapWithConfig: opens the map anchored to m_wMapFrame using an explicit
// map config resource (e.g. the replay config) instead of the gamemode's
// SCR_MapConfigComponent config (which is what COA_SpectatorMenu.OpenMap() uses).
// Mirrors COA_SpectatorMenu.OpenMap()'s own double-open sequence so keybinds
// and layout come out the same.
modded class COA_SpectatorMenu
{
	// Static reference to the currently open spectator menu instance, mirroring
	// PSCore's PS_SpectatorMenu.s_SpectatorMenu pattern so GRAD_Endgame_ReplayManager
	// can route map-open/close calls through it when it's active.
	static COA_SpectatorMenu s_EndgameSpectatorMenu;

	//------------------------------------------------------------------------------------------------
	override void OnMenuInit()
	{
		super.OnMenuInit();
		s_EndgameSpectatorMenu = this;
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuClose()
	{
		if (s_EndgameSpectatorMenu == this)
			s_EndgameSpectatorMenu = null;

		super.OnMenuClose();
	}

	void OpenMapWithConfig(ResourceName mapConfig)
	{
		Print("Endgame Replay - COA_SpectatorMenu.OpenMapWithConfig: called", LogLevel.NORMAL);

		if (!m_MapEntity)
		{
			Print("Endgame Replay - COA_SpectatorMenu.OpenMapWithConfig: m_MapEntity is null, aborting", LogLevel.ERROR);
			return;
		}

		if (!m_wMapFrame)
		{
			Print("Endgame Replay - COA_SpectatorMenu.OpenMapWithConfig: m_wMapFrame is null, aborting", LogLevel.ERROR);
			return;
		}

		SCR_ManualCamera camera = SCR_ManualCamera.Cast(GetGame().GetCameraManager().CurrentCamera());
		if (camera)
			camera.SetInputEnabled(false);

		m_wMapFrame.SetVisible(true);

		SCR_WidgetHelper.RemoveAllChildren(m_wMapFrame.FindAnyWidget("ToolMenuHoriz"));

		Print("Endgame Replay - COA_SpectatorMenu.OpenMapWithConfig: opening map with replay config", LogLevel.NORMAL);
		MapConfiguration mapConfigFullscreen = m_MapEntity.SetupMapConfig(EMapEntityMode.FULLSCREEN, mapConfig, m_wMapFrame);
		mapConfigFullscreen.MapEntityMode = EMapEntityMode.PLAIN;
		m_MapEntity.OpenMap(mapConfigFullscreen);
		m_MapEntity.CloseMap();
		mapConfigFullscreen.MapEntityMode = EMapEntityMode.FULLSCREEN;
		m_MapEntity.OpenMap(mapConfigFullscreen);
		Print("Endgame Replay - COA_SpectatorMenu.OpenMapWithConfig: map open complete", LogLevel.NORMAL);
	}
}
