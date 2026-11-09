//------------------------------------------------------------------------------------------------
//! Wave state of ONE faction. Pure server-side logic: no COALITION or replication calls in here,
//! so it can be driven by whatever hooks the respawn manager (T3) and fed to the HUD (T4).
//!
//! Rules (from the plan):
//!  - The wave starts when the first member of the faction dies.
//!  - It releases WaveSeconds later.
//!  - A dead player only counts once he has waited MinPlayerWaitSeconds before the release.
//!  - Wave size = ceil(team size / divisor); players beyond that wait for the next wave.
//! Times are world time in seconds, passed in by the caller.
class GRAD_Endgame_FactionWave
{
	protected float m_fWaveStart = -1;               //!< -1 = no wave running
	protected ref map<int, float> m_mDeathTime = new map<int, float>();   //!< playerId -> time of death
	protected ref set<int> m_aReleased = new set<int>();                  //!< players allowed to spawn now

	bool IsRunning() { return m_fWaveStart >= 0; }

	//! Replicated end timestamp for the countdown, or -1 when no wave is running.
	float GetReleaseTime(GRAD_Endgame_Config cfg)
	{
		if (!IsRunning() || !cfg)
			return -1;
		return m_fWaveStart + cfg.GetWaveSeconds();
	}

	void OnPlayerDeath(int playerId, float now)
	{
		m_aReleased.RemoveItem(playerId);
		m_mDeathTime.Set(playerId, now);
		if (!IsRunning())
			m_fWaveStart = now;
	}

	void OnPlayerLeft(int playerId)
	{
		m_mDeathTime.Remove(playerId);
		m_aReleased.RemoveItem(playerId);
	}

	//! Server gate: true only if this player's wave has been released.
	bool IsReleased(int playerId)
	{
		return m_aReleased.Contains(playerId);
	}

	//! Call when the player actually spawned, so the release is consumed.
	void OnPlayerSpawned(int playerId)
	{
		m_aReleased.RemoveItem(playerId);
		m_mDeathTime.Remove(playerId);
	}

	//! Call regularly on the server. Returns the number of players released by this call.
	int Tick(float now, int teamSize, GRAD_Endgame_Config cfg)
	{
		if (!IsRunning() || !cfg)
			return 0;
		if (now < m_fWaveStart + cfg.GetWaveSeconds())
			return 0;

		int waveSize = Math.Ceil(teamSize / (float)cfg.GetWaveSizeDivisor());
		waveSize = Math.Max(waveSize, 1);

		// Eligible players, oldest death first.
		array<int> eligible = {};
		array<float> times = {};
		foreach (int pid, float died : m_mDeathTime)
		{
			if (m_aReleased.Contains(pid))
				continue;
			if (now - died < cfg.GetMinPlayerWaitSeconds())
				continue;
			int at = 0;
			while (at < times.Count() && times[at] <= died)
				at++;
			eligible.InsertAt(pid, at);
			times.InsertAt(died, at);
		}

		int released = 0;
		foreach (int pid : eligible)
		{
			if (released >= waveSize)
				break;
			m_aReleased.Insert(pid);
			released++;
		}

		// Anyone still waiting (too recent, or over the size) starts the next wave now.
		bool pending = false;
		foreach (int pid, float died : m_mDeathTime)
		{
			if (!m_aReleased.Contains(pid))
			{
				pending = true;
				break;
			}
		}
		if (pending)
			m_fWaveStart = now;
		else if (released > 0)
			m_fWaveStart = -1;

		return released;
	}
}
