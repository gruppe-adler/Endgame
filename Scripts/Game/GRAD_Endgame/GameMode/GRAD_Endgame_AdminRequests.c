//------------------------------------------------------------------------------------------------
//! Admin requests for Endgame, added to COALITION's client-to-server component. It sits on every
//! player's own controller, so no prefab has to be changed to get these RPCs, and the caller's
//! identity comes from the controller (GetCallerPlayerId), never from a parameter.
//!
//! Call on a client, for example from the Workbench script console or later from a menu:
//!     COA_PlayerRplToAuthorityManager.GetInstance().EndgameAdvancePhase();
//!     COA_PlayerRplToAuthorityManager.GetInstance().EndgameReleaseWave("BLUFOR");
//!     COA_PlayerRplToAuthorityManager.GetInstance().EndgameEndMatch("OPFOR");   // "" = draw
//!     COA_PlayerRplToAuthorityManager.GetInstance().EndgameSetThreshold(4);     // before start
//!
//! The server ignores every request whose sender is not an admin (COALITION's IsCallerAdmin, which
//! is SCR_Global.IsAdmin on the caller's id). Each accepted request is written to the match log.
//!
//! NOT COMPILED YET (written without Workbench). To confirm there: that a modded class may add
//! RplRpc methods to this component.
modded class COA_PlayerRplToAuthorityManager
{
	//------------------------------------------------------------------------------------------------
	void EndgameAdvancePhase()
	{
		Rpc(RpcAsk_EndgameAdvancePhase);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_EndgameAdvancePhase()
	{
		if (!IsCallerAdmin())
			return;

		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (!gameMode)
			return;

		GRAD_Endgame_MatchLog.Write("admin", "", GetCallerPlayerId(), vector.Zero, "advance phase");
		gameMode.AdvancePhase("admin");
	}

	//------------------------------------------------------------------------------------------------
	//! Release everyone of this faction who is waiting for a wave.
	void EndgameReleaseWave(string factionKey)
	{
		Rpc(RpcAsk_EndgameReleaseWave, factionKey);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_EndgameReleaseWave(string factionKey)
	{
		if (!IsCallerAdmin())
			return;

		COA_RespawnManager respawnManager = COA_RespawnManager.GetInstance();
		if (!respawnManager)
			return;

		GRAD_Endgame_MatchLog.Write("admin", factionKey, GetCallerPlayerId(), vector.Zero, "release wave");
		respawnManager.EndgameFreeRespawn(factionKey, "admin");
	}

	//------------------------------------------------------------------------------------------------
	//! Set how many copies a side needs for phase 3. Only before the mission starts.
	void EndgameSetThreshold(int threshold)
	{
		Rpc(RpcAsk_EndgameSetThreshold, threshold);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_EndgameSetThreshold(int threshold)
	{
		if (!IsCallerAdmin())
			return;

		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (!gameMode)
			return;

		int callerId = GetCallerPlayerId();
		if (gameMode.SetDocumentThreshold(threshold, callerId))
			GRAD_Endgame_InfoService.HintPlayer(callerId, "Endgame", string.Format("Documents needed for phase 3: %1", threshold));
		else
			GRAD_Endgame_InfoService.HintPlayer(callerId, "Endgame", "Not changed: only before the mission starts, and at most one per active document site.");
	}

	//------------------------------------------------------------------------------------------------
	//! End the match. \param[in] winnerKey faction key of the winner, or empty for a draw.
	void EndgameEndMatch(string winnerKey)
	{
		Rpc(RpcAsk_EndgameEndMatch, winnerKey);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_EndgameEndMatch(string winnerKey)
	{
		if (!IsCallerAdmin())
			return;

		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (!gameMode)
			return;

		GRAD_Endgame_MatchLog.Write("admin", winnerKey, GetCallerPlayerId(), vector.Zero, "end match");
		gameMode.EndMatch(winnerKey, "admin");
	}
}
