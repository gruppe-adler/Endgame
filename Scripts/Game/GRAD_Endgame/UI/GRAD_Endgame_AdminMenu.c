//------------------------------------------------------------------------------------------------
//! Lobby setting for the admin (decided 2026-11-10): how many document copies a side needs for
//! phase 3, settable before the mission starts. It lives in COALITION's admin menu, on the
//! "Game Mode" page, next to COALITION's own respawn time setting.
//!
//! The widgets are looked up by name and everything is null-safe, so without them the menu works
//! exactly as before. TODO (Workbench): override COALITION's layout
//! UI/layouts/Menus/PauseMenu/AdminMenuWidgets/GameModeMenu.layout in this addon and add, by
//! copying the respawn time row:
//!   - an EditBoxWidget named "EndgameThresholdInput"
//!   - a button with SCR_ButtonTextComponent named "EndgameThresholdButton", text "Set documents needed"
//! The server accepts the value only before the mission starts (phase WAITING), from an admin, and
//! only if the active document sites can reach it.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-10): not compiled, not run.
modded class COA_AdminMenu
{
	//------------------------------------------------------------------------------------------------
	override void InitializeGamemodeMenu()
	{
		super.InitializeGamemodeMenu();

		if (!m_wMenuContent)
			return;

		EditBoxWidget input = EditBoxWidget.Cast(m_wMenuContent.FindAnyWidget("EndgameThresholdInput"));
		Widget buttonWidget = m_wMenuContent.FindAnyWidget("EndgameThresholdButton");
		if (!input || !buttonWidget)
			return;

		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (gameMode)
			input.SetText(gameMode.GetDocumentThreshold().ToString());

		SCR_ButtonTextComponent button = SCR_ButtonTextComponent.Cast(buttonWidget.FindHandler(SCR_ButtonTextComponent));
		if (button)
			button.m_OnClicked.Insert(EndgameSetThreshold);
	}

	//------------------------------------------------------------------------------------------------
	protected void EndgameSetThreshold()
	{
		if (!m_wMenuContent)
			return;

		EditBoxWidget input = EditBoxWidget.Cast(m_wMenuContent.FindAnyWidget("EndgameThresholdInput"));
		if (!input)
			return;

		COA_PlayerRplToAuthorityManager.GetInstance().EndgameSetThreshold(input.GetText().ToInt());
	}
}
