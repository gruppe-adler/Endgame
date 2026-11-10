//------------------------------------------------------------------------------------------------
class GRAD_Endgame_GameModeComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! The Endgame phase state machine (M1). Add this component to the COA_Lobby game-mode entity
//! (later: to Prefabs/GRAD_Endgame_Lobby.et, so every map gets it with the prefab).
//!
//! Server decides, everyone reads:
//!  - WAITING until COALITION's state becomes GAME. Then the document sites for the match are
//!    chosen (balanced between the sides) and the phase becomes FOB; every garrison spawns now.
//!  - FOB -> DOCUMENTS when both FOBs are taken (garrison wiped out), or by an admin request.
//!  - DOCUMENTS -> RADIO at the document threshold, or by an admin request.
//!  - RADIO: free respawn for both sides, open slots locked (a player who joins now lands in
//!    spectator), then permadeath after a grace period; optional time limit; optional loss by
//!    elimination.
//!  - ENDED: winner stored, COALITION moved to its AAR state.
//!
//! Without this component in the world everything else still runs (GetInstance() is null and the
//! other systems treat that as "no phase rules"), so the M0 test world keeps working.
//!
//! NOT COMPILED YET (written without Workbench).
class GRAD_Endgame_GameModeComponent : ScriptComponent
{
	[RplProp(onRplName: "OnPhaseReplicated")]
	protected GRAD_Endgame_EPhase m_ePhase = GRAD_Endgame_EPhase.WAITING;

	//! True when the current phase has a time limit; m_PhaseEndTs is only valid then.
	[RplProp()]
	protected bool m_bPhaseTimed;

	[RplProp()]
	protected WorldTimestamp m_PhaseEndTs;

	//! Faction key of the winner. Empty while running and for a draw.
	[RplProp()]
	protected string m_sWinnerKey;

	[RplProp()]
	protected bool m_bPermadeathActive;

	//! Document threshold set by an admin before the mission starts. -1 = use the config value.
	[RplProp()]
	protected int m_iThresholdOverride = -1;

	protected static GRAD_Endgame_GameModeComponent s_Instance;

	protected ref ScriptInvoker m_OnPhaseChanged = new ScriptInvoker();   //!< (GRAD_Endgame_EPhase oldPhase, GRAD_Endgame_EPhase newPhase)
	protected GRAD_Endgame_EPhase m_eLastNotifiedPhase = GRAD_Endgame_EPhase.WAITING;   //!< client: last phase the invoker was fired for

	// Server only
	protected ref map<string, int> m_mDocumentTotals = new map<string, int>();
	protected string m_sLeadingFaction;   //!< side that reached the document threshold first
	protected bool m_bRespawnDisabledByUs;
	protected int m_iEliminationTicksOne;
	protected int m_iEliminationTicksTwo;
	protected bool m_bEliminationArmed;   //!< both sides had players when the radio phase began; with one side empty (solo test) nobody can be eliminated
	protected int m_iLoadLogCountdown;

	protected const int TICK_MS = 1000;
	protected const int ELIMINATION_CONFIRM_TICKS = 10;   //!< Sugar Lake confirmed an empty side for 10 s
	protected const int LOAD_LOG_EVERY_TICKS = 60;

	//------------------------------------------------------------------------------------------------
	static GRAD_Endgame_GameModeComponent GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	GRAD_Endgame_EPhase GetPhase()
	{
		return m_ePhase;
	}

	//------------------------------------------------------------------------------------------------
	string GetWinnerKey()
	{
		return m_sWinnerKey;
	}

	//------------------------------------------------------------------------------------------------
	//! Copies a side needs for phase 3: the admin's lobby setting, else the config value.
	int GetDocumentThreshold()
	{
		if (m_iThresholdOverride >= 0)
			return m_iThresholdOverride;

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
			return 0;
		return cfg.GetDocumentThreshold();
	}

	//------------------------------------------------------------------------------------------------
	//! Server: an admin sets the threshold. Only before the mission starts, and only to a value the
	//! active sites can reach (one copy per site). Returns false if refused.
	bool SetDocumentThreshold(int threshold, int adminId)
	{
		if (!Replication.IsServer() || m_ePhase != GRAD_Endgame_EPhase.WAITING)
			return false;

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		int sites = GRAD_Endgame_DocumentSiteComponent.GetAll().Count();
		if (cfg && cfg.GetActiveDocumentSites() > 0)
			sites = Math.Min(sites, cfg.GetActiveDocumentSites());
		if (threshold < 1 || (sites > 0 && threshold > sites))
			return false;

		m_iThresholdOverride = threshold;
		Replication.BumpMe();
		GRAD_Endgame_MatchLog.Debug(string.Format("Admin %1 set the document threshold to %2", adminId, threshold));
		return true;
	}

	//------------------------------------------------------------------------------------------------
	bool IsPermadeathActive()
	{
		return m_bPermadeathActive;
	}

	//------------------------------------------------------------------------------------------------
	//! Seconds left in the current phase on the synced server clock, or -1 if the phase has no limit.
	float GetPhaseSecondsRemaining()
	{
		if (!m_bPhaseTimed)
			return -1;

		ChimeraWorld world = GetGame().GetWorld();
		if (!world)
			return -1;

		return Math.Max(m_PhaseEndTs.DiffSeconds(world.GetServerTimestamp()), 0);
	}

	//------------------------------------------------------------------------------------------------
	//! Fired on the server when the phase is set, and on clients when the new value arrives
	//! (and once after joining). Arguments: old phase, new phase.
	ScriptInvoker GetOnPhaseChanged()
	{
		return m_OnPhaseChanged;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_Instance = this;

		if (!Replication.IsServer())
		{
			// A late joiner gets the current value without the change callback (BreakingContact's
			// JIP notes), so tell the local listeners once after things have settled.
			GetGame().GetCallqueue().CallLater(ClientInitialSync, 1000, false);
			return;
		}

		COA_Gamemode gamemode = COA_Gamemode.Cast(owner);
		if (!gamemode)
		{
			GRAD_Endgame_MatchLog.Error("GRAD_Endgame_GameModeComponent must sit on the COA_Gamemode entity (COA_Lobby).");
			return;
		}

		gamemode.GetOnStateChanged().Insert(OnCoalitionStateChanged);
		GetGame().GetCallqueue().CallLater(ServerTick, TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(ServerTick);
		GetGame().GetCallqueue().Remove(ClientInitialSync);
		GetGame().GetCallqueue().Remove(EnablePermadeath);
		GetGame().GetCallqueue().Remove(SendReplayFrame);
		GetGame().GetCallqueue().Remove(AdvanceToAfterAction);
		if (s_Instance == this)
			s_Instance = null;
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	// CLIENT
	//------------------------------------------------------------------------------------------------
	protected void OnPhaseReplicated()
	{
		NotifyPhaseChanged();
	}

	//------------------------------------------------------------------------------------------------
	protected void ClientInitialSync()
	{
		NotifyPhaseChanged();
	}

	//------------------------------------------------------------------------------------------------
	protected void NotifyPhaseChanged()
	{
		if (m_ePhase == m_eLastNotifiedPhase)
			return;

		GRAD_Endgame_EPhase oldPhase = m_eLastNotifiedPhase;
		m_eLastNotifiedPhase = m_ePhase;
		if (m_ePhase == GRAD_Endgame_EPhase.ENDED)
			GRAD_Endgame_ClientInfo.Clear();
		m_OnPhaseChanged.Invoke(oldPhase, m_ePhase);
	}

	//------------------------------------------------------------------------------------------------
	// SERVER: phase control
	//------------------------------------------------------------------------------------------------
	//! COALITION changed its state (briefing, slotting, game, AAR).
	protected void OnCoalitionStateChanged()
	{
		COA_Gamemode gamemode = COA_Gamemode.Cast(GetOwner());
		if (!gamemode)
			return;

		switch (gamemode.m_GamemodeState)
		{
			case COA_EGamemodeState.GAME:
			{
				if (m_ePhase != GRAD_Endgame_EPhase.WAITING)
					break;

				GRAD_Endgame_MatchLog.StartMatch(string.Format("world=%1, document threshold %2", GetGame().GetWorldFile(), GetDocumentThreshold()));
				SelectDocumentSites();
				SelectRadioSites();
				SetPhase(GRAD_Endgame_EPhase.FOB, "rule");
				break;
			}

			case COA_EGamemodeState.AAR:
			{
				// COALITION ended the mission itself (time limit, admin). No winner means a draw.
				if (m_ePhase != GRAD_Endgame_EPhase.ENDED)
					EndMatch("", "coalition ended the mission");
				break;
			}

			case COA_EGamemodeState.SLOTTING:
			{
				// A new round in the same load: back to the start.
				ResetForNewRound();
				break;
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Server: set the phase. \param[in] cause "rule", "timer" or "admin", for the match log.
	void SetPhase(GRAD_Endgame_EPhase phase, string cause)
	{
		if (!Replication.IsServer() || phase == m_ePhase)
			return;

		GRAD_Endgame_EPhase oldPhase = m_ePhase;
		m_ePhase = phase;
		m_eLastNotifiedPhase = phase;
		m_bPhaseTimed = false;

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();

		if (phase == GRAD_Endgame_EPhase.RADIO && cfg)
		{
			float limit = cfg.GetPhase3TimeLimitSeconds();
			ChimeraWorld world = GetGame().GetWorld();
			if (limit > 0 && world)
			{
				m_bPhaseTimed = true;
				m_PhaseEndTs = world.GetServerTimestamp().PlusSeconds(limit);
			}
		}

		Replication.BumpMe();

		GRAD_Endgame_MatchLog.Write("phase", "", 0, vector.Zero, string.Format("%1 -> %2 (%3)", SCR_Enum.GetEnumName(GRAD_Endgame_EPhase, oldPhase), SCR_Enum.GetEnumName(GRAD_Endgame_EPhase, phase), cause));

		if (phase == GRAD_Endgame_EPhase.FOB && cfg)
			ShowFobMarkers(cfg);

		if (phase == GRAD_Endgame_EPhase.DOCUMENTS && cfg)
			RevealDocumentSites(cfg);

		if (phase == GRAD_Endgame_EPhase.RADIO && cfg)
		{
			OnRadioPhaseStarted(cfg);
			int startAliveOne, startAliveTwo;
			CountAlive(cfg, startAliveOne, startAliveTwo);
			m_bEliminationArmed = startAliveOne > 0 && startAliveTwo > 0;
		}

		// Late joining: open in phases 1 and 2, closed from phase 3 (decided 2026-11-10). With all
		// open slots locked, a player who joins can only spectate.
		if (phase == GRAD_Endgame_EPhase.RADIO)
		{
			COA_SlottingManager slottingManager = COA_SlottingManager.GetInstance();
			if (slottingManager)
				slottingManager.LockAllOpenSlots();
		}

		if (phase == GRAD_Endgame_EPhase.ENDED)
			GRAD_Endgame_ClientInfo.Clear();   // the after-action screen must not find our map markers

		m_OnPhaseChanged.Invoke(oldPhase, phase);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: move to the next phase. From RADIO this ends the match as a draw.
	void AdvancePhase(string cause)
	{
		if (!Replication.IsServer())
			return;

		switch (m_ePhase)
		{
			case GRAD_Endgame_EPhase.FOB:
				SetPhase(GRAD_Endgame_EPhase.DOCUMENTS, cause);
				break;
			case GRAD_Endgame_EPhase.DOCUMENTS:
				SetPhase(GRAD_Endgame_EPhase.RADIO, cause);
				break;
			case GRAD_Endgame_EPhase.RADIO:
				EndMatch("", cause);
				break;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Server: end the match. \param[in] winnerKey faction key, or empty for a draw.
	void EndMatch(string winnerKey, string cause)
	{
		if (!Replication.IsServer() || m_ePhase == GRAD_Endgame_EPhase.ENDED)
			return;

		m_sWinnerKey = winnerKey;
		SetPhase(GRAD_Endgame_EPhase.ENDED, cause);

		string result = "draw";
		if (!winnerKey.IsEmpty())
			result = "winner " + winnerKey;
		GRAD_Endgame_MatchLog.Write("match_end", winnerKey, 0, vector.Zero, string.Format("%1 (%2)", result, cause));

		// Hand over to COALITION's after-action state, unless COALITION is what ended the match.
		// TODO (Workbench): COALITION's outro can show a winning faction; find the setter of the
		// outcome it falls back to and set it before this call.
		COA_Gamemode gamemode = COA_Gamemode.Cast(GetOwner());
		if (gamemode && gamemode.m_GamemodeState == COA_EGamemodeState.GAME)
		{
			// Replay first, then the after-action screen.
			float replaySeconds = StartReplay();
			if (replaySeconds > 0)
				GetGame().GetCallqueue().CallLater(AdvanceToAfterAction, replaySeconds * 1000, false);
			else
				gamemode.AdvanceGamemodeState(true);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void AdvanceToAfterAction()
	{
		COA_Gamemode gamemode = COA_Gamemode.Cast(GetOwner());
		if (gamemode && gamemode.m_GamemodeState == COA_EGamemodeState.GAME)
			gamemode.AdvanceGamemodeState(true);
	}

	//------------------------------------------------------------------------------------------------
	// REPLAY (server sends, every client plays; see GRAD_Endgame_Replay and GRAD_Endgame_ReplayPlayer)
	//------------------------------------------------------------------------------------------------
	protected int m_iReplayNextFrame;

	protected const int REPLAY_FRAME_DELAY_MS = 40;
	protected const float REPLAY_TAIL_SECONDS = 5;

	//------------------------------------------------------------------------------------------------
	//! Server: send the recorded replay to everybody. Returns how many seconds the after-action
	//! screen must wait for the replay (transfer plus playback), or 0 if there is no replay.
	protected float StartReplay()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		GRAD_Endgame_Replay replay = GRAD_Endgame_Replay.GetRecording();
		if (!cfg || !cfg.GetReplayEnabled() || replay.m_aFrames.Count() < 2)
			return 0;

		float duration = replay.GetDuration();
		float speed = Math.Max(duration / Math.Max(cfg.GetReplayTargetSeconds(), 10), 1);

		array<int> ids = {};
		array<string> names = {};
		foreach (int id, string name : replay.m_mNames)
		{
			ids.Insert(id);
			names.Insert(name);
		}

		Rpc(RpcDo_ReplayBegin, replay.m_fInterval, speed, ids, names);
		if (HasLocalPlayer())
			RpcDo_ReplayBegin(replay.m_fInterval, speed, ids, names);

		m_iReplayNextFrame = 0;
		GetGame().GetCallqueue().CallLater(SendReplayFrame, REPLAY_FRAME_DELAY_MS, true);

		float transferSeconds = replay.m_aFrames.Count() * REPLAY_FRAME_DELAY_MS / 1000 + 1;
		float playSeconds = duration / speed + replay.m_fInterval / speed;
		GRAD_Endgame_MatchLog.Write("replay", "", 0, vector.Zero, string.Format("%1 frames, %2 events, speed %3x, %4 s", replay.m_aFrames.Count(), replay.m_aEventTexts.Count(), Math.Round(speed), Math.Round(playSeconds)));
		return transferSeconds + playSeconds + REPLAY_TAIL_SECONDS;
	}

	//------------------------------------------------------------------------------------------------
	protected bool HasLocalPlayer()
	{
		return SCR_PlayerController.GetLocalPlayerId() > 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: one frame per call, then the events and the start signal.
	protected void SendReplayFrame()
	{
		GRAD_Endgame_Replay replay = GRAD_Endgame_Replay.GetRecording();

		if (m_iReplayNextFrame < replay.m_aFrames.Count())
		{
			GRAD_Endgame_ReplayFrame frame = replay.m_aFrames[m_iReplayNextFrame];
			m_iReplayNextFrame++;
			Rpc(RpcDo_ReplayFrame, frame.m_fTime, frame.m_aIds, frame.m_aFlags, frame.m_aX, frame.m_aZ);
			if (HasLocalPlayer())
				RpcDo_ReplayFrame(frame.m_fTime, frame.m_aIds, frame.m_aFlags, frame.m_aX, frame.m_aZ);
			return;
		}

		GetGame().GetCallqueue().Remove(SendReplayFrame);
		Rpc(RpcDo_ReplayEnd, replay.m_aEventTimes, replay.m_aEventTexts);
		if (HasLocalPlayer())
			RpcDo_ReplayEnd(replay.m_aEventTimes, replay.m_aEventTexts);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_ReplayBegin(float interval, float speed, array<int> rosterIds, array<string> rosterNames)
	{
		GRAD_Endgame_ReplayPlayer.Begin(interval, speed, rosterIds, rosterNames);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_ReplayFrame(float time, array<int> ids, array<int> flags, array<float> xs, array<float> zs)
	{
		GRAD_Endgame_ReplayPlayer.AddFrame(time, ids, flags, xs, zs);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_ReplayEnd(array<float> eventTimes, array<string> eventTexts)
	{
		GRAD_Endgame_ReplayPlayer.SetEvents(eventTimes, eventTexts);
		GRAD_Endgame_ReplayPlayer.Start();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnRadioPhaseStarted(notnull GRAD_Endgame_Config cfg)
	{
		RevealRadioPhase(cfg);

		COA_RespawnManager respawnManager = COA_RespawnManager.GetInstance();
		if (!respawnManager)
			return;

		// Sugar Lake: both sides get one free respawn when phase 3 begins, then nobody respawns.
		if (cfg.GetFreeRespawnOnRadioPhase())
		{
			respawnManager.EndgameFreeRespawn(cfg.GetFactionKeyOne(), "radio phase");
			respawnManager.EndgameFreeRespawn(cfg.GetFactionKeyTwo(), "radio phase");
		}

		if (cfg.GetEndPhasePermadeath())
			GetGame().GetCallqueue().CallLater(EnablePermadeath, Math.Max(cfg.GetRadioPhaseGraceSeconds(), 1) * 1000, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void EnablePermadeath()
	{
		if (m_ePhase != GRAD_Endgame_EPhase.RADIO || m_bPermadeathActive)
			return;

		m_bPermadeathActive = true;
		Replication.BumpMe();

		// With respawn disabled COALITION offers no respawn screen and sends the dead to spectator.
		COA_RespawnManager respawnManager = COA_RespawnManager.GetInstance();
		if (respawnManager)
		{
			respawnManager.SetRespawnEnabled(false);
			m_bRespawnDisabledByUs = true;
		}

		GRAD_Endgame_MatchLog.Write("permadeath", "", 0, vector.Zero, "on");
	}

	//------------------------------------------------------------------------------------------------
	protected void ResetForNewRound()
	{
		GetGame().GetCallqueue().Remove(EnablePermadeath);

		if (m_bRespawnDisabledByUs)
		{
			COA_RespawnManager respawnManager = COA_RespawnManager.GetInstance();
			if (respawnManager)
				respawnManager.SetRespawnEnabled(true);
			m_bRespawnDisabledByUs = false;
		}

		m_mDocumentTotals.Clear();
		m_sLeadingFaction = "";
		GRAD_Endgame_InfoService.Reset();
		// TODO (Workbench): a new round in the same load also needs fresh garrisons, FOBs back to
		// untaken and spawn points back at their start. Check whether COALITION reloads the world
		// between rounds; if it does, nothing is needed here.
		m_iEliminationTicksOne = 0;
		m_iEliminationTicksTwo = 0;
		m_bEliminationArmed = false;
		m_bPermadeathActive = false;
		m_sWinnerKey = "";

		if (m_ePhase != GRAD_Endgame_EPhase.WAITING)
		{
			GRAD_Endgame_EPhase oldPhase = m_ePhase;
			m_ePhase = GRAD_Endgame_EPhase.WAITING;
			m_eLastNotifiedPhase = m_ePhase;
			m_bPhaseTimed = false;
			m_OnPhaseChanged.Invoke(oldPhase, m_ePhase);
		}

		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	// SERVER: FOBs and site selection
	//------------------------------------------------------------------------------------------------
	//! Called by a FOB when its garrison has been wiped out. Both taken: phase DOCUMENTS.
	void OnFobTaken(GRAD_Endgame_FobComponent fob)
	{
		if (!Replication.IsServer() || m_ePhase != GRAD_Endgame_EPhase.FOB)
			return;

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
			return;

		GRAD_Endgame_InfoService.RemoveMarker(fob.GetFactionKey(), "fob_" + fob.GetFactionKey());

		GRAD_Endgame_FobComponent fobOne = GRAD_Endgame_FobComponent.GetForFaction(cfg.GetFactionKeyOne());
		GRAD_Endgame_FobComponent fobTwo = GRAD_Endgame_FobComponent.GetForFaction(cfg.GetFactionKeyTwo());
		if (fobOne && fobTwo && fobOne.IsTaken() && fobTwo.IsTaken())
			SetPhase(GRAD_Endgame_EPhase.DOCUMENTS, "rule");
	}

	//------------------------------------------------------------------------------------------------
	//! Phase 1: each side sees its own FOB and has to clear the garrison there.
	protected void ShowFobMarkers(GRAD_Endgame_Config cfg)
	{
		array<string> keys = {cfg.GetFactionKeyOne(), cfg.GetFactionKeyTwo()};
		foreach (string key : keys)
		{
			GRAD_Endgame_FobComponent fob = GRAD_Endgame_FobComponent.GetForFaction(key);
			if (!fob)
				continue;

			GRAD_Endgame_InfoService.SetMarker(key, "fob_" + key, fob.GetOwner().GetOrigin(), 0, "Your FOB: clear the garrison");
			GRAD_Endgame_InfoService.HintFaction(key, "Phase 1: take your FOB", "Clear the militia from the marked FOB to unlock your spawn there.");
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Switch on the configured number of document sites, chosen at random but balanced between the
	//! sides' FOBs (GRAD_Endgame_SiteSelection). Runs once, at game start, before garrisons spawn.
	protected void SelectDocumentSites()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
			return;

		array<GRAD_Endgame_DocumentSiteComponent> all = GRAD_Endgame_DocumentSiteComponent.GetAll();
		array<vector> positions = {};
		foreach (GRAD_Endgame_DocumentSiteComponent site : all)
			positions.Insert(site.GetOwner().GetOrigin());

		GRAD_Endgame_FobComponent fobOne = GRAD_Endgame_FobComponent.GetForFaction(cfg.GetFactionKeyOne());
		GRAD_Endgame_FobComponent fobTwo = GRAD_Endgame_FobComponent.GetForFaction(cfg.GetFactionKeyTwo());
		if (!fobOne || !fobTwo)
			GRAD_Endgame_MatchLog.Error("Document site selection: a FOB is missing, so the choice is random without balancing.");

		vector posOne = vector.Zero;
		vector posTwo = vector.Zero;
		if (fobOne && fobTwo)
		{
			posOne = fobOne.GetOwner().GetOrigin();
			posTwo = fobTwo.GetOwner().GetOrigin();
		}

		array<int> chosen = {};
		float imbalance = GRAD_Endgame_SiteSelection.Choose(positions, posOne, posTwo, cfg.GetActiveDocumentSites(), cfg.GetSiteBalanceTolerance(), chosen);

		for (int i = 0; i < all.Count(); i++)
			all[i].SetActive(chosen.Contains(i));


		GRAD_Endgame_MatchLog.Write("sites_chosen", "", 0, vector.Zero, string.Format("%1 of %2 sites, imbalance %3 \%", chosen.Count(), all.Count(), Math.Round(imbalance * 100)));
		if (fobOne && fobTwo && imbalance > cfg.GetSiteBalanceTolerance())
			GRAD_Endgame_MatchLog.Error(string.Format("No fair set of document sites found (best imbalance %1 \%). Place more sites halfway between the FOBs.", Math.Round(imbalance * 100)));
	}

	//------------------------------------------------------------------------------------------------
	//! Phase 2 start (decided 2026-11-10: no restriction radius in phase 1; instead the document
	//! sites stay hidden until phase 2). Both sides learn where the active sites are: an area, or
	//! the exact spot.
	protected void RevealDocumentSites(notnull GRAD_Endgame_Config cfg)
	{
		float areaRadius = cfg.GetDocumentAreaRadius();
		if (cfg.GetExactDocumentPositions())
			areaRadius = 0;

		foreach (GRAD_Endgame_DocumentSiteComponent site : GRAD_Endgame_DocumentSiteComponent.GetAll())
		{
			if (!site.IsSiteActive())
				continue;

			vector shown = GRAD_Endgame_InfoService.Blur(site.GetOwner().GetOrigin(), areaRadius);
			string id = site.GetMarkerId();
			GRAD_Endgame_InfoService.SetMarker(cfg.GetFactionKeyOne(), id, shown, areaRadius, "Documents");
			GRAD_Endgame_InfoService.SetMarker(cfg.GetFactionKeyTwo(), id, shown, areaRadius, "Documents");
		}

		GRAD_Endgame_InfoService.HintFaction(cfg.GetFactionKeyOne(), "Documents located", "Copy documents at the marked sites.");
		GRAD_Endgame_InfoService.HintFaction(cfg.GetFactionKeyTwo(), "Documents located", "Copy documents at the marked sites.");
	}

	//------------------------------------------------------------------------------------------------
	//! Choose the radio sites at game start (so their militia spawns with the other garrisons), with
	//! the same balance rule as the document sites. Their positions are revealed in phase 3.
	protected void SelectRadioSites()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
			return;

		array<GRAD_Endgame_RadioSiteComponent> all = GRAD_Endgame_RadioSiteComponent.GetAll();
		if (all.IsEmpty())
		{
			GRAD_Endgame_MatchLog.Error("No radio site placed: the radio phase cannot be won by transmission.");
			return;
		}

		array<vector> positions = {};
		foreach (GRAD_Endgame_RadioSiteComponent site : all)
			positions.Insert(site.GetOwner().GetOrigin());

		vector posOne, posTwo;
		GetFobPositions(cfg, posOne, posTwo);

		array<int> chosen = {};
		float imbalance = GRAD_Endgame_SiteSelection.Choose(positions, posOne, posTwo, cfg.GetActiveRadioSites(), cfg.GetSiteBalanceTolerance(), chosen);
		for (int i = 0; i < all.Count(); i++)
			all[i].SetActive(chosen.Contains(i), i);

		GRAD_Endgame_MatchLog.Write("radio_sites_chosen", "", 0, vector.Zero, string.Format("%1 of %2 sites, imbalance %3 \%", chosen.Count(), all.Count(), Math.Round(imbalance * 100)));
	}

	//------------------------------------------------------------------------------------------------
	protected void GetFobPositions(notnull GRAD_Endgame_Config cfg, out vector posOne, out vector posTwo)
	{
		posOne = vector.Zero;
		posTwo = vector.Zero;
		GRAD_Endgame_FobComponent fobOne = GRAD_Endgame_FobComponent.GetForFaction(cfg.GetFactionKeyOne());
		GRAD_Endgame_FobComponent fobTwo = GRAD_Endgame_FobComponent.GetForFaction(cfg.GetFactionKeyTwo());
		if (fobOne && fobTwo)
		{
			posOne = fobOne.GetOwner().GetOrigin();
			posTwo = fobTwo.GetOwner().GetOrigin();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Phase 3: reveal the radio sites to both sides, spawn the cipher at a random cipher site, and
	//! give its location to the leading side's leaders as the decoded order.
	protected void RevealRadioPhase(notnull GRAD_Endgame_Config cfg)
	{
		foreach (GRAD_Endgame_RadioSiteComponent radio : GRAD_Endgame_RadioSiteComponent.GetAll())
		{
			if (!radio.IsSiteActive())
				continue;
			GRAD_Endgame_InfoService.SetMarker(cfg.GetFactionKeyOne(), radio.GetMarkerId(), radio.GetOwner().GetOrigin(), 0, "Radio site");
			GRAD_Endgame_InfoService.SetMarker(cfg.GetFactionKeyTwo(), radio.GetMarkerId(), radio.GetOwner().GetOrigin(), 0, "Radio site");
		}

		array<GRAD_Endgame_CipherSiteComponent> cipherSites = GRAD_Endgame_CipherSiteComponent.GetAll();
		if (cipherSites.IsEmpty())
		{
			GRAD_Endgame_MatchLog.Error("No cipher site placed: nobody can transmit.");
			return;
		}

		GRAD_Endgame_CipherSiteComponent cipherSite = cipherSites.GetRandomElement();
		cipherSite.SpawnCipher(cfg.GetCipherPrefab());
		vector cipherPos = cipherSite.GetOwner().GetOrigin();

		// Who learns where it is: the side that reached the threshold. Advanced by an admin, the side
		// with more copies; even, both.
		string leader = m_sLeadingFaction;
		if (leader.IsEmpty())
		{
			int one = m_mDocumentTotals.Get(cfg.GetFactionKeyOne());
			int two = m_mDocumentTotals.Get(cfg.GetFactionKeyTwo());
			if (one > two)
				leader = cfg.GetFactionKeyOne();
			else if (two > one)
				leader = cfg.GetFactionKeyTwo();
		}

		string orderText = string.Format("The cipher lies at grid %1 / %2. Bring it to a radio site and transmit.", Math.Round(cipherPos[0]), Math.Round(cipherPos[2]));
		array<string> told = {};
		if (leader.IsEmpty() || cfg.GetCipherRevealToDefenders())
		{
			told.Insert(cfg.GetFactionKeyOne());
			told.Insert(cfg.GetFactionKeyTwo());
		}
		else
		{
			told.Insert(leader);
		}

		foreach (string factionKey : told)
		{
			GRAD_Endgame_InfoService.SetOrder(factionKey, orderText);
			GRAD_Endgame_InfoService.SetMarker(factionKey, "cipher", cipherPos, 0, "Cipher");
		}

		GRAD_Endgame_MatchLog.Write("threshold", leader, 0, cipherPos, string.Format("order sent to %1 side(s)", told.Count()));
	}

	//------------------------------------------------------------------------------------------------
	// SERVER: documents (M4)
	//------------------------------------------------------------------------------------------------
	//! Server: one copy was made by this faction. Returns the faction's new total.
	//! At the threshold the radio phase starts. TODO (M4): send the decoded order to the leader.
	int AddDocument(string factionKey, int playerId, vector position, string siteMarkerId = "")
	{
		if (!Replication.IsServer())
			return 0;

		int total = m_mDocumentTotals.Get(factionKey) + 1;
		m_mDocumentTotals.Set(factionKey, total);
		GRAD_Endgame_MatchLog.Write("copy", factionKey, playerId, position, string.Format("total %1", total));

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
			return total;

		// Own side only: the count, and this site leaves its map (one copy per site per side).
		GRAD_Endgame_InfoService.HintFaction(factionKey, "Documents", string.Format("%1 of %2 copied", total, GetDocumentThreshold()));
		if (!siteMarkerId.IsEmpty() && cfg.GetCopiesPerSitePerFaction() == 1)
			GRAD_Endgame_InfoService.RemoveMarker(factionKey, siteMarkerId);

		if (GetDocumentThreshold() > 0 && total >= GetDocumentThreshold() && m_ePhase == GRAD_Endgame_EPhase.DOCUMENTS)
		{
			m_sLeadingFaction = factionKey;
			SetPhase(GRAD_Endgame_EPhase.RADIO, "rule");
		}

		return total;
	}

	//------------------------------------------------------------------------------------------------
	//! Server only. Clients must not learn the opponent's total (see "Network contract").
	int GetDocumentTotal(string factionKey)
	{
		return m_mDocumentTotals.Get(factionKey);
	}

	//------------------------------------------------------------------------------------------------
	// SERVER: tick
	//------------------------------------------------------------------------------------------------
	protected void ServerTick()
	{
		if (m_ePhase == GRAD_Endgame_EPhase.WAITING || m_ePhase == GRAD_Endgame_EPhase.ENDED)
			return;

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
			return;

		// Time limit of the radio phase: nobody transmitted in time, so it is a draw.
		if (m_bPhaseTimed && GetPhaseSecondsRemaining() <= 0)
		{
			EndMatch("", "timer");
			return;
		}

		GRAD_Endgame_Replay.GetRecording().Tick(TICK_MS / 1000, cfg);

		int aliveOne, aliveTwo;
		CountAlive(cfg, aliveOne, aliveTwo);

		if (m_ePhase == GRAD_Endgame_EPhase.RADIO && m_bPermadeathActive && m_bEliminationArmed && cfg.GetEliminationEndsMatch())
		{
			if (aliveOne == 0)
				m_iEliminationTicksOne++;
			else
				m_iEliminationTicksOne = 0;

			if (aliveTwo == 0)
				m_iEliminationTicksTwo++;
			else
				m_iEliminationTicksTwo = 0;

			bool oneOut = m_iEliminationTicksOne >= ELIMINATION_CONFIRM_TICKS;
			bool twoOut = m_iEliminationTicksTwo >= ELIMINATION_CONFIRM_TICKS;
			if (oneOut && twoOut)
			{
				EndMatch("", "both sides eliminated");
				return;
			}
			if (oneOut)
			{
				EndMatch(cfg.GetFactionKeyTwo(), "elimination");
				return;
			}
			if (twoOut)
			{
				EndMatch(cfg.GetFactionKeyOne(), "elimination");
				return;
			}
		}

		m_iLoadLogCountdown--;
		if (m_iLoadLogCountdown <= 0)
		{
			m_iLoadLogCountdown = LOAD_LOG_EVERY_TICKS;
			// TODO (Workbench): add the server frame time once the right call is confirmed.
			GRAD_Endgame_MatchLog.Write("server_load", "", 0, vector.Zero, string.Format("garrison groups %1, garrison agents %2, alive %3 %4, alive %5 %6", GRAD_Endgame_GarrisonComponent.s_iActiveGroups, GRAD_Endgame_GarrisonComponent.s_iActiveAgents, cfg.GetFactionKeyOne(), aliveOne, cfg.GetFactionKeyTwo(), aliveTwo));
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Connected, slotted players of each side whose slot is not dead.
	protected void CountAlive(notnull GRAD_Endgame_Config cfg, out int aliveOne, out int aliveTwo)
	{
		aliveOne = 0;
		aliveTwo = 0;

		COA_SlottingManager slottingManager = COA_SlottingManager.GetInstance();
		if (!slottingManager)
			return;

		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);
		foreach (int playerId : players)
		{
			if (!slottingManager.IsPlayerInASlot(playerId) || slottingManager.IsPlayerConsideredDead(playerId))
				continue;

			Faction faction = slottingManager.GetPlayerSlotFaction(playerId);
			if (!faction)
				continue;

			string key = faction.GetFactionKey();
			if (key == cfg.GetFactionKeyOne())
				aliveOne++;
			else if (key == cfg.GetFactionKeyTwo())
				aliveTwo++;
		}
	}
}
