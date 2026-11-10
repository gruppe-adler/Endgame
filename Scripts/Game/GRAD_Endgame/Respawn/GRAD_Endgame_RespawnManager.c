//------------------------------------------------------------------------------------------------
//! Endgame per-faction wave respawn on top of COALITION's COA_RespawnManager.
//!
//! How it fits COALITION (read from COA_RespawnManager.c, commit 2023ee3):
//!  - A dead client waits in RespawnTimer() and, once its local timer is up and a spawn point is
//!    confirmed, asks the server via RespawnPlayer(). COALITION's server never checked the time.
//!    The gate in RespawnPlayer() below does.
//!  - COALITION's client closes its menus BEFORE it sends the request, so a refused request would
//!    leave the player without a respawn menu (finding R1). Two measures against that:
//!      1. The client knows whether it is released (m_aEndgameReleased) and holds its own timer
//!         above zero until then, so an unreleased client never sends the request.
//!      2. If a request is refused anyway, the server sends the respawn screen again.
//!  - The countdown comes from GetWaveSecondsRemaining(): the local player's FACTION wave,
//!    replicated as one timestamp per faction. A released player gets 0, which ends his countdown.
//!  - Death is detected by polling IsPlayerConsideredDead on the server.
//!  - Keep the lobby's m_bWaveRespawn OFF: COALITION's global wave must not run beside ours.
//!    m_bRespawnEnabled stays ON so the respawn menu works.
//!
//! The wave rules themselves live in GRAD_Endgame_FactionWave.
//!
//! CHANGED WITHOUT WORKBENCH (2026-11-10): not compiled, not run. Run the respawn part of the
//! checklist in the plan (items 1 to 9) before building on it.
modded class COA_RespawnManager
{
	//! Replicated countdown targets, one per faction (keys from GRAD_Endgame_Config).
	[RplProp()] protected WorldTimestamp m_EndgameWaveTsOne;
	[RplProp()] protected WorldTimestamp m_EndgameWaveTsTwo;

	//! Replicated: dead players who may spawn now. A handful of ints, sent only when it changes.
	[RplProp()] protected ref array<int> m_aEndgameReleased = {};

	protected ref map<string, ref GRAD_Endgame_FactionWave> m_mEndgameWaves = new map<string, ref GRAD_Endgame_FactionWave>();
	protected ref map<string, ref set<int>> m_mEndgamePresent = new map<string, ref set<int>>();   //!< connected, slotted players per faction, from the last tick
	protected ref set<int> m_aEndgameTracked = new set<int>();   //!< connected dead players the tick has already seen dead
	protected bool m_bEndgameForced;                             //!< true while an admin/forced respawn runs
	protected bool m_bEndgameKeyWarningDone;
	protected float m_fEndgameBuffer;

	protected const float ENDGAME_TICK_SECONDS = 0.25;
	protected const float ENDGAME_PUBLISH_MARGIN = 1;   //!< countdown ends this long after the real release, so the server is always first
	protected const float ENDGAME_HOLD_SECONDS = 2;     //!< value an unreleased client's timer is held at when no countdown target is known

	//------------------------------------------------------------------------------------------------
	override void EOnFixedFrame(IEntity owner, float timeSlice)
	{
		super.EOnFixedFrame(owner, timeSlice);

		m_fEndgameBuffer += timeSlice;
		if (m_fEndgameBuffer < ENDGAME_TICK_SECONDS)
			return;
		m_fEndgameBuffer = 0;
		EndgameTick();
	}

	//------------------------------------------------------------------------------------------------
	protected GRAD_Endgame_FactionWave EndgameGetWave(string factionKey)
	{
		GRAD_Endgame_FactionWave wave = m_mEndgameWaves.Get(factionKey);
		if (!wave)
		{
			wave = new GRAD_Endgame_FactionWave();
			m_mEndgameWaves.Set(factionKey, wave);
		}
		return wave;
	}

	//------------------------------------------------------------------------------------------------
	protected set<int> EndgameGetPresent(string factionKey)
	{
		set<int> present = m_mEndgamePresent.Get(factionKey);
		if (!present)
		{
			present = new set<int>();
			m_mEndgamePresent.Set(factionKey, present);
		}
		return present;
	}

	//------------------------------------------------------------------------------------------------
	protected float EndgameNow()
	{
		return GetGame().GetWorld().GetWorldTime() * 0.001;
	}

	//------------------------------------------------------------------------------------------------
	// SERVER
	//------------------------------------------------------------------------------------------------
	//! Register deaths and spawns, release waves, publish countdown targets and released players.
	protected void EndgameTick()
	{
		if (!Replication.IsServer() || !m_SlottingManager || !m_Gamemode)
			return;
		if (m_Gamemode.m_GamemodeState != COA_EGamemodeState.GAME)
			return;

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
			return;

		float now = EndgameNow();
		array<string> keys = {cfg.GetFactionKeyOne(), cfg.GetFactionKeyTwo()};

		foreach (string resetKey : keys)
			EndgameGetPresent(resetKey).Clear();

		// Connected players only: a player who left must neither count for the team size nor take
		// part in a wave (finding R5). His death time stays in the wave for a reconnect.
		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);

		int slotted;
		int matched;
		set<int> connectedDead = new set<int>();

		foreach (int playerId : players)
		{
			if (!m_SlottingManager.IsPlayerInASlot(playerId))
				continue;
			Faction faction = m_SlottingManager.GetPlayerSlotFaction(playerId);
			if (!faction)
				continue;

			slotted++;
			string key = faction.GetFactionKey();
			if (!cfg.IsPlayerFaction(key))
				continue;

			matched++;
			EndgameGetPresent(key).Insert(playerId);
			GRAD_Endgame_FactionWave wave = EndgameGetWave(key);

			bool dead = m_SlottingManager.IsPlayerConsideredDead(playerId);
			bool tracked = m_aEndgameTracked.Contains(playerId);
			if (dead)
				connectedDead.Insert(playerId);

			if (dead && !tracked)
			{
				m_aEndgameTracked.Insert(playerId);
				// A reconnecting dead player is already known to the wave and keeps his death time.
				if (!wave.IsDead(playerId))
				{
					wave.OnPlayerDeath(playerId, now);
					float due = wave.GetReleaseTime(now, EndgameGetPresent(key), cfg) - now;
					GRAD_Endgame_MatchLog.Write("death", key, playerId, vector.Zero, string.Format("countdown %1 s", Math.Round(due)));
				}
			}
			else if (!dead && tracked)
			{
				m_aEndgameTracked.RemoveItem(playerId);
				wave.OnPlayerSpawned(playerId);
			}
		}

		// Players who disconnected while dead: stop tracking them, but leave the wave entry alone.
		array<int> gone = {};
		foreach (int trackedId : m_aEndgameTracked)
		{
			if (!connectedDead.Contains(trackedId))
				gone.Insert(trackedId);
		}
		foreach (int goneId : gone)
			m_aEndgameTracked.RemoveItem(goneId);

		// A wrong faction key in the config would switch the wave rules off without a word (R7).
		if (slotted > 0 && matched == 0 && !m_bEndgameKeyWarningDone)
		{
			m_bEndgameKeyWarningDone = true;
			GRAD_Endgame_MatchLog.Error(string.Format("No slotted player belongs to faction '%1' or '%2' (GRAD_Endgame_Config). Wave respawn is NOT active.", keys[0], keys[1]));
		}

		array<int> released = {};
		for (int i = 0; i < 2; i++)
		{
			GRAD_Endgame_FactionWave tickWave = EndgameGetWave(keys[i]);
			set<int> present = EndgameGetPresent(keys[i]);
			int teamSize = present.Count();

			int count = tickWave.Tick(now, teamSize, present, cfg);
			if (count > 0)
				GRAD_Endgame_MatchLog.Write("wave_release", keys[i], 0, vector.Zero, string.Format("released %1, wave size %2, team %3", count, GRAD_Endgame_FactionWave.GetWaveSize(teamSize, cfg), teamSize));

			EndgamePublishCountdown(i == 0, tickWave.GetReleaseTime(now, present, cfg), now);

			array<int> factionReleased = {};
			tickWave.GetReleased(factionReleased);
			foreach (int releasedId : factionReleased)
				released.Insert(releasedId);
		}

		EndgamePublishReleased(released);
	}

	//------------------------------------------------------------------------------------------------
	//! Put the faction's countdown target into its replicated timestamp. Only re-sends when it
	//! really moved. With nobody waiting it publishes "now" once, which clients read as "no countdown".
	protected void EndgamePublishCountdown(bool sideOne, float release, float now)
	{
		ChimeraWorld world = GetGame().GetWorld();
		if (!world)
			return;

		WorldTimestamp serverNow = world.GetServerTimestamp();
		WorldTimestamp current;
		if (sideOne)
			current = m_EndgameWaveTsOne;
		else
			current = m_EndgameWaveTsTwo;

		WorldTimestamp expected = serverNow;
		if (release >= 0)
		{
			expected = serverNow.PlusSeconds(release - now + ENDGAME_PUBLISH_MARGIN);
			if (current != null && Math.AbsFloat(expected.DiffSeconds(current)) < 1.5)
				return;
		}
		else if (current != null && current.DiffSeconds(serverNow) <= 0)
		{
			return;
		}

		if (sideOne)
			m_EndgameWaveTsOne = expected;
		else
			m_EndgameWaveTsTwo = expected;

		GRAD_Endgame_MatchLog.Debug(string.Format("Published countdown side one=%1: waiting=%2, in %3 s", sideOne, release >= 0, expected.DiffSeconds(serverNow)));
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	//! Replicate the released players, but only when the list changed.
	protected void EndgamePublishReleased(notnull array<int> released)
	{
		bool same = released.Count() == m_aEndgameReleased.Count();
		if (same)
		{
			foreach (int playerId : released)
			{
				if (!m_aEndgameReleased.Contains(playerId))
				{
					same = false;
					break;
				}
			}
		}
		if (same)
			return;

		m_aEndgameReleased.Clear();
		foreach (int releasedId : released)
			m_aEndgameReleased.Insert(releasedId);
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	//! Server: release every waiting player of a faction now (FOB taken, radio phase started, admin).
	//! They still pick a spawn point in the respawn menu; nobody is spawned by force.
	//! \return number of players released
	int EndgameFreeRespawn(string factionKey, string cause)
	{
		if (!Replication.IsServer())
			return 0;

		int count = EndgameGetWave(factionKey).ReleaseAll(EndgameNow(), EndgameGetPresent(factionKey));
		GRAD_Endgame_MatchLog.Write("free_respawn", factionKey, 0, vector.Zero, string.Format("%1, released %2", cause, count));
		return count;
	}

	//------------------------------------------------------------------------------------------------
	//! Server gate: true if this player must not spawn now.
	bool EndgameBlocksSpawn(int playerId)
	{
		if (!Replication.IsServer() || m_bEndgameForced || !m_SlottingManager)
			return false;

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
			return false;

		Faction faction = m_SlottingManager.GetPlayerSlotFaction(playerId);
		if (!faction || !cfg.IsPlayerFaction(faction.GetFactionKey()))
			return false;

		// The first spawn of an alive slot is not a respawn.
		if (!m_SlottingManager.IsPlayerConsideredDead(playerId))
			return false;

		// Radio phase: nobody comes back once permadeath is on.
		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (gameMode && gameMode.IsPermadeathActive())
			return true;

		return !EndgameGetWave(faction.GetFactionKey()).IsReleased(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Every respawn request ends up here: the client's RPC, spawn-on-group and the admin paths.
	//! COALITION's InitilizePlayer needs no second gate: it sends every dead slot to spectator, and
	//! only this method clears the dead flag (finding R6).
	override void RespawnPlayer(int playerId, int spawnPointID = -1, RplId entityRplID = RplId.Invalid())
	{
		if (EndgameBlocksSpawn(playerId))
		{
			GRAD_Endgame_MatchLog.Write("respawn_refused", "", playerId, vector.Zero, "wave not released");

			// The client has already closed its menus; give it the respawn screen back (R1).
			COA_RplBroadcastManager broadcastManager = COA_RplBroadcastManager.GetInstance();
			if (broadcastManager)
				broadcastManager.SendRespawnScreen(playerId);
			return;
		}

		super.RespawnPlayer(playerId, spawnPointID, entityRplID);
	}

	//------------------------------------------------------------------------------------------------
	//! Admin and round-reset respawns bypass the wave gate.
	override void RespawnSide(FactionKey faction)
	{
		m_bEndgameForced = true;
		super.RespawnSide(faction);
		m_bEndgameForced = false;
	}

	//------------------------------------------------------------------------------------------------
	// CLIENT
	//------------------------------------------------------------------------------------------------
	//! Faction key of the local player if it is one of the two Endgame factions, else empty.
	protected string EndgameGetLocalFactionKey(GRAD_Endgame_Config cfg)
	{
		if (!cfg || !m_SlottingManager)
			return string.Empty;

		Faction faction = m_SlottingManager.GetPlayerSlotFaction(SCR_PlayerController.GetLocalPlayerId());
		if (!faction || !cfg.IsPlayerFaction(faction.GetFactionKey()))
			return string.Empty;

		return faction.GetFactionKey();
	}

	//------------------------------------------------------------------------------------------------
	//! Client: has the server released the local player?
	bool EndgameIsLocalReleased()
	{
		return m_aEndgameReleased.Contains(SCR_PlayerController.GetLocalPlayerId());
	}

	//------------------------------------------------------------------------------------------------
	//! Countdown source for COALITION's respawn timer.
	//!  - released: 0, which ends the countdown so the player can spawn
	//!  - waiting: seconds to the faction's countdown target
	//!  - no target known (not arrived yet, or nobody waits): -1, "leave your timer alone"
	//! Factions outside Endgame fall back to COALITION's own wave.
	override float GetWaveSecondsRemaining()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		string key = EndgameGetLocalFactionKey(cfg);
		if (key.IsEmpty())
			return super.GetWaveSecondsRemaining();

		if (EndgameIsLocalReleased())
			return 0;

		WorldTimestamp ts;
		if (key == cfg.GetFactionKeyOne())
			ts = m_EndgameWaveTsOne;
		else
			ts = m_EndgameWaveTsTwo;

		ChimeraWorld world = GetGame().GetWorld();
		if (ts == null || !world)
			return -1;

		float remaining = ts.DiffSeconds(world.GetServerTimestamp());
		if (remaining <= 0)
			return -1;
		return remaining;
	}

	//------------------------------------------------------------------------------------------------
	//! Client: COALITION sends the respawn request as soon as its timer is at zero and a spawn point
	//! is confirmed. Hold the timer above zero until the server has released the local player, so
	//! the request is never sent early and the countdown always shows the next real release.
	override void RespawnTimer(float timeSlice)
	{
		EndgameHoldTimerUntilReleased();
		super.RespawnTimer(timeSlice);
	}

	//------------------------------------------------------------------------------------------------
	protected void EndgameHoldTimerUntilReleased()
	{
		if (m_fRespawnTimer >= 0.5)
			return;

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (EndgameGetLocalFactionKey(cfg).IsEmpty())
			return;

		// COALITION lets the after-action state through regardless of the timer; so do we.
		if (m_Gamemode && m_Gamemode.m_GamemodeState == COA_EGamemodeState.AAR)
			return;

		if (!m_SlottingManager.IsPlayerConsideredDead(SCR_PlayerController.GetLocalPlayerId()))
			return;

		if (EndgameIsLocalReleased())
			return;

		m_fRespawnTimer = Math.Max(GetWaveSecondsRemaining(), ENDGAME_HOLD_SECONDS);
	}

	//------------------------------------------------------------------------------------------------
	//! Concept rule: waiting players do not spectate (config: m_bSpectateWhileWaiting).
	override bool CanSpectateWhileWaiting()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (cfg && !cfg.GetSpectateWhileWaiting() && !EndgameGetLocalFactionKey(cfg).IsEmpty())
			return false;

		return super.CanSpectateWhileWaiting();
	}
}
