//------------------------------------------------------------------------------------------------
//! Endgame per-faction wave respawn on top of COALITION's COA_RespawnManager (T3 + T4).
//!
//! How it fits COALITION (read from COA_RespawnManager.c):
//!  - The client waits in RespawnTimer() and then asks the server via RespawnPlayer(). The server
//!    never checked the wave time. We add that check here (the gate).
//!  - The client's countdown is fed by GetWaveSecondsRemaining(). We override it to return the
//!    local player's FACTION wave, replicated as one timestamp per faction.
//!  - Death is detected by polling IsPlayerConsideredDead on the server (no hook needed).
//!  - Keep the lobby's m_bWaveRespawn OFF: COALITION's global wave must not run beside ours.
//!    m_bRespawnEnabled stays ON so the respawn menu works.
modded class COA_RespawnManager
{
	//! Replicated wave release moments, one per faction (keys from GRAD_Endgame_Config).
	[RplProp()] protected WorldTimestamp m_EndgameWaveTsOne;
	[RplProp()] protected WorldTimestamp m_EndgameWaveTsTwo;

	protected ref map<string, ref GRAD_Endgame_FactionWave> m_mEndgameWaves = new map<string, ref GRAD_Endgame_FactionWave>();
	protected ref set<int> m_aEndgameTracked = new set<int>();   //!< dead players already registered in a wave
	protected bool m_bEndgameForced;                             //!< true while an admin/forced respawn runs
	protected float m_fEndgameBuffer;
	protected int m_iEndgameLastClientLog;

	//------------------------------------------------------------------------------------------------
	override void EOnFixedFrame(IEntity owner, float timeSlice)
	{
		super.EOnFixedFrame(owner, timeSlice);

		m_fEndgameBuffer += timeSlice;
		if (m_fEndgameBuffer < 0.25)
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
	protected bool EndgameIsOurFaction(string key, GRAD_Endgame_Config cfg)
	{
		return key == cfg.GetFactionKeyOne() || key == cfg.GetFactionKeyTwo();
	}

	//------------------------------------------------------------------------------------------------
	//! Server: register deaths and spawns, release waves, publish the countdown timestamps.
	protected void EndgameTick()
	{
		if (!Replication.IsServer() || !m_SlottingManager || !m_Gamemode)
			return;
		if (m_Gamemode.m_GamemodeState != COA_EGamemodeState.GAME)
			return;

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
			return;

		float now = GetGame().GetWorld().GetWorldTime() * 0.001;

		array<int> players = {};
		GetGame().GetPlayerManager().GetAllPlayers(players);

		map<string, int> teamSize = new map<string, int>();
		set<int> present = new set<int>();

		foreach (int playerId : players)
		{
			if (!m_SlottingManager.IsPlayerInASlot(playerId))
				continue;
			Faction faction = m_SlottingManager.GetPlayerSlotFaction(playerId);
			if (!faction)
				continue;
			string key = faction.GetFactionKey();
			if (!EndgameIsOurFaction(key, cfg))
				continue;

			present.Insert(playerId);
			teamSize.Set(key, teamSize.Get(key) + 1);

			bool dead = m_SlottingManager.IsPlayerConsideredDead(playerId);
			bool tracked = m_aEndgameTracked.Contains(playerId);
			if (dead && !tracked)
			{
				m_aEndgameTracked.Insert(playerId);
				EndgameGetWave(key).OnPlayerDeath(playerId, now);
				Print(string.Format("[GRAD_Endgame] Death registered: player %1, faction %2, wave release at %3 (now %4)", playerId, key, EndgameGetWave(key).GetReleaseTime(cfg), now), LogLevel.NORMAL);
			}
			else if (!dead && tracked)
			{
				m_aEndgameTracked.RemoveItem(playerId);
				EndgameGetWave(key).OnPlayerSpawned(playerId);
			}
		}

		// Players who left or were unslotted while waiting
		array<int> gone = {};
		foreach (int trackedId : m_aEndgameTracked)
		{
			if (!present.Contains(trackedId))
				gone.Insert(trackedId);
		}
		foreach (int goneId : gone)
		{
			m_aEndgameTracked.RemoveItem(goneId);
			foreach (string waveKey, GRAD_Endgame_FactionWave goneWave : m_mEndgameWaves)
				goneWave.OnPlayerLeft(goneId);
		}

		array<string> keys = {cfg.GetFactionKeyOne(), cfg.GetFactionKeyTwo()};
		for (int i = 0; i < 2; i++)
		{
			GRAD_Endgame_FactionWave wave = EndgameGetWave(keys[i]);
			int released = wave.Tick(now, teamSize.Get(keys[i]), cfg);
			if (released > 0)
				Print(string.Format("[GRAD_Endgame] Wave released for %1: %2 player(s)", keys[i], released), LogLevel.NORMAL);
			EndgamePublish(i == 0, wave, now, cfg);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Server: put the faction's release moment into its replicated timestamp. Only re-sends when it
	//! really moved. An idle wave publishes "now" once, which the clients read as 0 seconds left.
	protected void EndgamePublish(bool sideOne, GRAD_Endgame_FactionWave wave, float now, GRAD_Endgame_Config cfg)
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
		float release = wave.GetReleaseTime(cfg);
		if (release >= 0)
		{
			expected = serverNow.PlusSeconds(release - now);
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
		Print(string.Format("[GRAD_Endgame] Published wave timestamp side %1: running=%2, in %3 s", sideOne, release >= 0, expected.DiffSeconds(serverNow)), LogLevel.NORMAL);
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	//! Client countdown source: the local player's faction wave. Falls back to COALITION's own wave
	//! for factions that are not part of Endgame.
	override float GetWaveSecondsRemaining()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg || !m_SlottingManager)
			return super.GetWaveSecondsRemaining();

		Faction faction = m_SlottingManager.GetPlayerSlotFaction(SCR_PlayerController.GetLocalPlayerId());
		if (!faction)
			return -1;

		string key = faction.GetFactionKey();
		WorldTimestamp ts;
		if (key == cfg.GetFactionKeyOne())
			ts = m_EndgameWaveTsOne;
		else if (key == cfg.GetFactionKeyTwo())
			ts = m_EndgameWaveTsTwo;
		else
			return super.GetWaveSecondsRemaining();

		ChimeraWorld world = GetGame().GetWorld();
		if (ts == null || !world)
			return -1;

		float remaining = ts.DiffSeconds(world.GetServerTimestamp());
		if (System.GetTickCount() - m_iEndgameLastClientLog > 5000)
		{
			m_iEndgameLastClientLog = System.GetTickCount();
			Print(string.Format("[GRAD_Endgame] Client countdown for %1: %2 s", key, remaining), LogLevel.NORMAL);
		}
		// A timestamp in the past is an idle or already released wave, or one the server has not
		// replaced yet right after a death. COALITION only overwrites its timer while it is above 0,
		// so returning 0 here would kill the countdown for good. -1 means "leave your timer alone".
		if (remaining <= 0)
			return -1;
		return remaining;
	}

	//------------------------------------------------------------------------------------------------
	//! Server gate: true if this dead player must not spawn yet.
	//! \param[in] explicitSpawn true when the caller names a spawn point or entity
	bool EndgameBlocksSpawn(int playerId, bool explicitSpawn)
	{
		if (!Replication.IsServer() || m_bEndgameForced || !m_SlottingManager)
			return false;

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
			return false;

		Faction faction = m_SlottingManager.GetPlayerSlotFaction(playerId);
		if (!faction || !EndgameIsOurFaction(faction.GetFactionKey(), cfg))
			return false;

		// Alive players and plain re-initialisation (spectator path) are not an early spawn.
		if (!explicitSpawn || !m_SlottingManager.IsPlayerConsideredDead(playerId))
			return false;

		return !EndgameGetWave(faction.GetFactionKey()).IsReleased(playerId);
	}

	//------------------------------------------------------------------------------------------------
	override void RespawnPlayer(int playerId, int spawnPointID = -1, RplId entityRplID = RplId.Invalid())
	{
		// Every direct spawn request counts as explicit here: this is the call the client sends.
		if (EndgameBlocksSpawn(playerId, true))
		{
			Print(string.Format("[GRAD_Endgame] Refused early respawn of player %1 (wave not released)", playerId), LogLevel.WARNING);
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
}

//------------------------------------------------------------------------------------------------
//! Second half of the server gate: InitilizePlayer with an explicit spawn point or entity.
modded class COA_GamemodeManager
{
	override bool InitilizePlayer(int playerId, int spawnPointID = -1, RplId entityRplID = RplId.Invalid())
	{
		COA_RespawnManager respawnManager = COA_RespawnManager.GetInstance();
		bool explicitSpawn = (spawnPointID != -1 || entityRplID != RplId.Invalid());
		if (respawnManager && respawnManager.EndgameBlocksSpawn(playerId, explicitSpawn))
		{
			Print(string.Format("[GRAD_Endgame] Refused early InitilizePlayer of player %1 (wave not released)", playerId), LogLevel.WARNING);
			return false;
		}

		return super.InitilizePlayer(playerId, spawnPointID, entityRplID);
	}
}
