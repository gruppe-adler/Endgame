//------------------------------------------------------------------------------------------------
//! Join in progress. COALITION shows a late joiner no slotting menu once the match is live (the
//! "open slotting" prompt of its spectator menu only exists during safestart), so such a player
//! is stuck as a spectator. This lets him open the slotting menu himself (Shift+O, with a HUD
//! hint) until the phase named in the config (m_eLateJoinClosesAt, default RADIO: open in phases 1 and 2) begins.
//!
//! Client: GRAD_Endgame_FieldLogDisplay (key and hint). Server: the modded COA_SlottingManager at
//! the bottom of this file refuses a new slot claim once the window is closed.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-14): not compiled, not run.
class GRAD_Endgame_LateJoin
{
	//------------------------------------------------------------------------------------------------
	//! True while a slotless player may take a slot: the feature is on, the match is live and the
	//! phase has not reached the closing phase. False if the feature is off or the match is over.
	static bool IsOpen()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (!cfg || !gameMode || !cfg.GetLateJoinSlotting())
			return false;

		GRAD_Endgame_EPhase phase = gameMode.GetPhase();
		if (phase == GRAD_Endgame_EPhase.ENDED)
			return false;

		return phase < cfg.GetLateJoinClosesAt();
	}

	//------------------------------------------------------------------------------------------------
	//! True once COALITION's match is live (the state in which late joiners arrive).
	static bool IsMatchLive()
	{
		COA_Gamemode lobby = COA_Gamemode.GetInstance();
		return lobby && lobby.m_GamemodeState == COA_EGamemodeState.GAME;
	}

	//------------------------------------------------------------------------------------------------
	//! Client: this player is live in the match but holds no slot (a spectator who joined late).
	static bool LocalPlayerIsSlotless()
	{
		COA_SlottingManager slotting = COA_SlottingManager.GetInstance();
		if (!slotting)
			return false;

		int playerId = SCR_PlayerController.GetLocalPlayerId();
		return playerId > 0 && !slotting.IsPlayerInASlot(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Client: the local player's menu manager (COALITION).
	protected static COA_PlayerMenuManager GetMenuManager()
	{
		PlayerController controller = GetGame().GetPlayerController();
		if (!controller)
			return null;
		return COA_PlayerMenuManager.Cast(controller.FindComponent(COA_PlayerMenuManager));
	}

	//------------------------------------------------------------------------------------------------
	//! Client: Shift+O. Open the slotting menu; pressed again while it is open, go back to spectating.
	static void Toggle()
	{
		if (!IsMatchLive() || !IsOpen() || !LocalPlayerIsSlotless())
			return;

		COA_PlayerMenuManager menus = GetMenuManager();
		if (!menus)
			return;

		MenuBase top = GetGame().GetMenuManager().GetTopMenu();
		if (top && top.IsInherited(COA_SlottingMenu))
			menus.OpenCurrentStateMenu();   // back to the spectator screen
		else
			menus.OpenSlottingMenu();
	}
}

//------------------------------------------------------------------------------------------------
//! Server gate: a player who holds no slot may not claim one once the late-join window has closed.
//! Players who already hold a slot (reconnects restore theirs by another path), admins' kicks
//! (playerId 0) and the normal slotting phase before the match are not touched.
modded class COA_SlottingManager
{
	override void UpdateSlotPlayerID(int slotId, int playerId = -1)
	{
		if (Replication.IsServer() && playerId > 0 && GRAD_Endgame_LateJoin.IsMatchLive() && !IsPlayerInASlot(playerId))
		{
			GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
			GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
			if (cfg && gameMode && cfg.GetLateJoinSlotting() && !GRAD_Endgame_LateJoin.IsOpen())
			{
				GRAD_Endgame_MatchLog.Write("late_join_refused", "", playerId, vector.Zero, "slot " + slotId.ToString() + ", the window closed");
				return;
			}
		}

		super.UpdateSlotPlayerID(slotId, playerId);
	}
}
