//------------------------------------------------------------------------------------------------
//! Wave state of ONE faction. Pure server-side logic: no engine, COALITION or replication calls in
//! here, so it can be tested with plain numbers (see GRAD_Endgame_FactionWaveTest).
//!
//! Rules (decided 2026-11-10, taken from the Sugar Lake mission where noted):
//!  - The wave clock runs WaveSeconds from the death of the OLDEST player still waiting.
//!  - The wave size, ceil(team size / divisor), is a MINIMUM (Sugar Lake): the wave releases once
//!    the clock is up AND at least that many players are waiting.
//!  - A release takes EVERYONE who is waiting, there is no cap. A player who died ten seconds
//!    before the release goes with it (MinPlayerWaitSeconds is 0 by default).
//!  - If the oldest waiting death is MaxWaitSeconds old, everyone waiting is released even if the
//!    wave is not full (Sugar Lake: MAXRESPAWNTIME).
//!  - Optional: a player who dies within LateJoinSeconds after a release goes at once.
//!  - Players who are not connected keep their death time but never count and are never released.
//!
//! Times are world time in seconds, passed in by the caller.
class GRAD_Endgame_FactionWave
{
	protected ref map<int, float> m_mDeathTime = new map<int, float>();   //!< playerId -> time of death, while dead
	protected ref set<int> m_aReleased = new set<int>();                  //!< dead players allowed to spawn now
	protected float m_fLastRelease = -1;                                  //!< time of the last release, -1 = none yet

	//------------------------------------------------------------------------------------------------
	//! A player died. A death that is already registered keeps its time, so a dead player who
	//! reconnects does not start his wait again.
	void OnPlayerDeath(int playerId, float now)
	{
		if (m_mDeathTime.Contains(playerId))
			return;

		m_aReleased.RemoveItem(playerId);
		m_mDeathTime.Set(playerId, now);
	}

	//------------------------------------------------------------------------------------------------
	//! The player actually spawned: the release is consumed.
	void OnPlayerSpawned(int playerId)
	{
		m_aReleased.RemoveItem(playerId);
		m_mDeathTime.Remove(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! The player no longer belongs to this faction's waiting list at all (lost the slot, changed
	//! side). A plain disconnect must NOT call this: the death time is kept for a reconnect.
	void Forget(int playerId)
	{
		m_aReleased.RemoveItem(playerId);
		m_mDeathTime.Remove(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Server gate: true only if this player has been released and has not spawned yet.
	bool IsReleased(int playerId)
	{
		return m_aReleased.Contains(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Released players who have not spawned yet, for replication to the clients.
	void GetReleased(notnull array<int> released)
	{
		released.Clear();
		foreach (int pid : m_aReleased)
			released.Insert(pid);
	}

	//------------------------------------------------------------------------------------------------
	bool IsDead(int playerId)
	{
		return m_mDeathTime.Contains(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Connected players who are dead and not released yet.
	int GetWaitingCount(notnull set<int> present)
	{
		int count;
		foreach (int pid, float died : m_mDeathTime)
		{
			if (!m_aReleased.Contains(pid) && present.Contains(pid))
				count++;
		}
		return count;
	}

	//------------------------------------------------------------------------------------------------
	static int GetWaveSize(int teamSize, notnull GRAD_Endgame_Config cfg)
	{
		int waveSize = Math.Ceil(teamSize / (float)cfg.GetWaveSizeDivisor());
		return Math.Max(waveSize, 1);
	}

	//------------------------------------------------------------------------------------------------
	//! Death time of the oldest connected player still waiting, or -1 if nobody waits.
	protected float GetOldestWaitingDeath(notnull set<int> present)
	{
		float oldest = -1;
		foreach (int pid, float died : m_mDeathTime)
		{
			if (m_aReleased.Contains(pid) || !present.Contains(pid))
				continue;
			if (oldest < 0 || died < oldest)
				oldest = died;
		}
		return oldest;
	}

	//------------------------------------------------------------------------------------------------
	//! The moment to show in the countdown, or -1 when nobody waits.
	//! While the wave clock runs this is its end. Once the clock is up and the wave is still not
	//! full, it is the moment the longest wait runs out, which is when a release is guaranteed.
	float GetReleaseTime(float now, notnull set<int> present, notnull GRAD_Endgame_Config cfg)
	{
		float oldest = GetOldestWaitingDeath(present);
		if (oldest < 0)
			return -1;

		float waveEnd = oldest + cfg.GetWaveSeconds();
		if (now < waveEnd)
			return waveEnd;

		return oldest + cfg.GetMaxWaitSeconds();
	}

	//------------------------------------------------------------------------------------------------
	//! Release every connected waiting player now, whatever the clock says (free respawn when a
	//! FOB is taken or the radio phase starts, admin "release wave"). Returns the number released.
	int ReleaseAll(float now, notnull set<int> present)
	{
		int released;
		foreach (int pid, float died : m_mDeathTime)
		{
			if (m_aReleased.Contains(pid) || !present.Contains(pid))
				continue;
			m_aReleased.Insert(pid);
			released++;
		}

		if (released > 0)
			m_fLastRelease = now;
		return released;
	}

	//------------------------------------------------------------------------------------------------
	//! Call regularly on the server. Returns the number of players released by this call.
	//! \param[in] now world time in seconds
	//! \param[in] teamSize connected, slotted players of this faction, dead or alive
	//! \param[in] present ids of the connected, slotted players of this faction
	int Tick(float now, int teamSize, notnull set<int> present, notnull GRAD_Endgame_Config cfg)
	{
		// Waiting players, and those among them who have served the personal minimum wait.
		array<int> waiting = {};
		array<int> eligible = {};
		array<int> lateJoiners = {};
		float oldest = -1;

		bool lateWindowOpen = cfg.GetLateJoinSeconds() > 0 && m_fLastRelease >= 0 && now <= m_fLastRelease + cfg.GetLateJoinSeconds();

		foreach (int pid, float died : m_mDeathTime)
		{
			if (m_aReleased.Contains(pid) || !present.Contains(pid))
				continue;

			waiting.Insert(pid);
			if (oldest < 0 || died < oldest)
				oldest = died;
			if (now - died >= cfg.GetMinPlayerWaitSeconds())
				eligible.Insert(pid);
			if (lateWindowOpen && died >= m_fLastRelease)
				lateJoiners.Insert(pid);
		}

		if (waiting.IsEmpty())
			return 0;

		// Late-join window: deaths just after a release go at once and do not move m_fLastRelease,
		// so the window cannot extend itself.
		if (!lateJoiners.IsEmpty())
		{
			foreach (int latePid : lateJoiners)
				m_aReleased.Insert(latePid);
			return lateJoiners.Count();
		}

		// Longest wait reached: everyone goes, full wave or not.
		if (now >= oldest + cfg.GetMaxWaitSeconds())
		{
			foreach (int maxPid : waiting)
				m_aReleased.Insert(maxPid);
			m_fLastRelease = now;
			return waiting.Count();
		}

		// Normal release: clock up and the wave has its minimum size.
		if (now < oldest + cfg.GetWaveSeconds())
			return 0;
		if (eligible.Count() < GetWaveSize(teamSize, cfg))
			return 0;

		foreach (int goPid : eligible)
			m_aReleased.Insert(goPid);
		m_fLastRelease = now;
		return eligible.Count();
	}
}
