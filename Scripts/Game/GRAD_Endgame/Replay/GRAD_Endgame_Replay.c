//------------------------------------------------------------------------------------------------
//! One sample of every tracked player. Flat arrays, because they go over the network as they are.
class GRAD_Endgame_ReplayFrame
{
	float m_fTime;
	ref array<int> m_aIds = {};
	ref array<int> m_aFlags = {};   //!< faction index * 2 + (alive ? 1 : 0)
	ref array<float> m_aX = {};
	ref array<float> m_aZ = {};
}

//------------------------------------------------------------------------------------------------
//! The after-action replay data of one match: player positions every few seconds and a short list
//! of key events. The server records it during the match; at the end it is sent to every client,
//! which plays it back on the map (GRAD_Endgame_ReplayPlayer).
//!
//! Lean on purpose: players and events only, no vehicles, projectiles or explosives. The model for
//! the full version is BreakingContact's GRAD_BC_ReplayManager.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-10): not compiled, not run.
class GRAD_Endgame_Replay
{
	static const int MAX_EVENTS = 150;
	static const string FACTION_LETTER_ONE = "B";
	static const string FACTION_LETTER_TWO = "O";

	protected static ref GRAD_Endgame_Replay s_Recording;   //!< server: what is being recorded

	ref array<ref GRAD_Endgame_ReplayFrame> m_aFrames = {};
	ref array<float> m_aEventTimes = {};
	ref array<string> m_aEventTexts = {};
	ref map<int, string> m_mNames = new map<int, string>();
	float m_fInterval = 5;
	float m_fElapsed;        //!< seconds recorded so far
	float m_fSinceSample = 1000;   //!< forces a sample on the first tick

	//------------------------------------------------------------------------------------------------
	//! Server: the recording of this match. Created on first use.
	static GRAD_Endgame_Replay GetRecording()
	{
		if (!s_Recording)
			s_Recording = new GRAD_Endgame_Replay();
		return s_Recording;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: forget the last match.
	static void ResetRecording()
	{
		s_Recording = new GRAD_Endgame_Replay();
	}

	//------------------------------------------------------------------------------------------------
	//! Server: called by MatchLog for every log line; keeps the ones worth showing in the replay.
	static void OnLogEvent(string eventName, string factionKey, string details)
	{
		if (!s_Recording)
			return;

		switch (eventName)
		{
			case "phase":
			case "fob_state":
			case "copy":
			case "cipher":
			case "transmission":
			case "df_sent":
			case "permadeath":
			case "match_end":
				break;
			default:
				return;
		}

		string text = eventName;
		if (!factionKey.IsEmpty())
			text = text + " " + factionKey;
		if (!details.IsEmpty())
			text = text + ": " + details;
		s_Recording.AddEvent(text);
	}

	//------------------------------------------------------------------------------------------------
	void AddEvent(string text)
	{
		if (m_aEventTimes.Count() >= MAX_EVENTS)
			return;
		m_aEventTimes.Insert(m_fElapsed);
		m_aEventTexts.Insert(text);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: call once per second while the match runs.
	void Tick(float seconds, notnull GRAD_Endgame_Config cfg)
	{
		m_fElapsed += seconds;
		m_fSinceSample += seconds;
		m_fInterval = Math.Max(cfg.GetReplayIntervalSeconds(), 1);
		if (m_fSinceSample < m_fInterval)
			return;
		m_fSinceSample = 0;
		Sample(cfg);
	}

	//------------------------------------------------------------------------------------------------
	protected void Sample(notnull GRAD_Endgame_Config cfg)
	{
		COA_SlottingManager slottingManager = COA_SlottingManager.GetInstance();
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return;

		GRAD_Endgame_ReplayFrame frame = new GRAD_Endgame_ReplayFrame();
		frame.m_fTime = m_fElapsed;

		array<int> players = {};
		playerManager.GetPlayers(players);
		foreach (int playerId : players)
		{
			string key = GRAD_Endgame_InfoService.GetFactionKey(playerId);
			int factionIndex = -1;
			if (key == cfg.GetFactionKeyOne())
				factionIndex = 0;
			else if (key == cfg.GetFactionKeyTwo())
				factionIndex = 1;
			if (factionIndex < 0)
				continue;

			IEntity entity = playerManager.GetPlayerControlledEntity(playerId);
			if (!entity)
				continue;

			int alive = 1;
			if (slottingManager && slottingManager.IsPlayerConsideredDead(playerId))
				alive = 0;

			vector position = entity.GetOrigin();
			frame.m_aIds.Insert(playerId);
			frame.m_aFlags.Insert(factionIndex * 2 + alive);
			frame.m_aX.Insert(position[0]);
			frame.m_aZ.Insert(position[2]);

			if (!m_mNames.Contains(playerId))
				m_mNames.Set(playerId, playerManager.GetPlayerName(playerId));
		}

		if (frame.m_aIds.Count() > 0)
			m_aFrames.Insert(frame);
	}

	//------------------------------------------------------------------------------------------------
	float GetDuration()
	{
		if (m_aFrames.IsEmpty())
			return 0;
		return m_aFrames[m_aFrames.Count() - 1].m_fTime;
	}
}
