//------------------------------------------------------------------------------------------------
//! Widget A, the status line: only on the map (decided 2026-11-10), one typed line along the
//! bottom edge: phase, time left, transmission, "[J] log". Text from GRAD_Endgame_HudText.
//!
//! Registration (Workbench): add this component to the map configuration the game uses for the
//! in-game map (the gadget map config, list of map UI components), like the vanilla map modules.
//! Layout: UI/layouts/GRAD_Endgame_MapStatusLine.layout with a TextWidget "StatusLineText".
//!
//! STUB, WRITTEN WITHOUT WORKBENCH (2026-11-11): not compiled. Unverified: the OnMapOpen /
//! OnMapClose / Update signatures and m_RootWidget of SCR_MapUIBaseComponent.
[BaseContainerProps()]
class GRAD_Endgame_MapStatusLine : SCR_MapUIBaseComponent
{
	[Attribute("", UIWidgets.ResourcePickerThumbnail, "Layout with a TextWidget named StatusLineText.", params: "layout")]
	protected ResourceName m_sLayout;

	protected Widget m_wLine;
	protected TextWidget m_wText;

	//------------------------------------------------------------------------------------------------
	override void OnMapOpen(MapConfiguration config)
	{
		super.OnMapOpen(config);

		if (m_sLayout.IsEmpty() || !m_RootWidget)
			return;

		m_wLine = GetGame().GetWorkspace().CreateWidgets(m_sLayout, m_RootWidget);
		if (m_wLine)
			m_wText = TextWidget.Cast(m_wLine.FindAnyWidget("StatusLineText"));
	}

	//------------------------------------------------------------------------------------------------
	override void OnMapClose(MapConfiguration config)
	{
		if (m_wLine)
			m_wLine.RemoveFromHierarchy();
		m_wLine = null;
		m_wText = null;

		super.OnMapClose(config);
	}

	//------------------------------------------------------------------------------------------------
	override void Update(float timeSlice)
	{
		super.Update(timeSlice);

		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (!m_wText || !gameMode)
			return;

		m_wText.SetText(GRAD_Endgame_HudText.StatusLine(gameMode));
	}
}
