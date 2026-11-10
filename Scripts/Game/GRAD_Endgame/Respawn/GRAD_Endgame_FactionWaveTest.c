//------------------------------------------------------------------------------------------------
//! Config with fixed values for the wave tests (attribute defaults are not applied to an object
//! made with "new", so everything the wave reads is set here).
class GRAD_Endgame_WaveTestConfig : GRAD_Endgame_Config
{
	void GRAD_Endgame_WaveTestConfig()
	{
		m_fWaveSeconds = 270;
		m_fMinPlayerWaitSeconds = 0;
		m_iWaveSizeDivisor = 3;
		m_fMaxWaitSeconds = 1200;
		m_fLateJoinSeconds = 0;
		m_fDebugTimeScale = 1;
	}
}

//------------------------------------------------------------------------------------------------
//! Table tests for GRAD_Endgame_FactionWave: the six cases from WP1 in the plan.
//! Run from the Workbench script console (or any server-side script):
//!     GRAD_Endgame_FactionWaveTest.Run();
//! Prints one PASS or FAIL line per check and a summary. No world, players or COALITION needed.
//!
//! All cases: team size 8, divisor 3 (wave size 3), wave 270 s, no personal wait, longest wait 1200 s.
class GRAD_Endgame_FactionWaveTest
{
	protected static int s_iFailed;
	protected static int s_iChecks;

	//------------------------------------------------------------------------------------------------
	static void Run()
	{
		s_iFailed = 0;
		s_iChecks = 0;

		CaseLoneDeath();
		CaseLuckyLateDeath();
		CaseThirdDeathFillsWave();
		CaseNoCap();
		CaseNewClockAfterRelease();
		CaseAbsentPlayerDoesNotCount();

		Print(string.Format("[GRAD_Endgame] FactionWave tests: %1 checks, %2 failed", s_iChecks, s_iFailed), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected static void Check(string name, float actual, float expected)
	{
		s_iChecks++;
		if (Math.AbsFloat(actual - expected) < 0.01)
		{
			Print(string.Format("[GRAD_Endgame] PASS %1", name), LogLevel.NORMAL);
			return;
		}

		s_iFailed++;
		Print(string.Format("[GRAD_Endgame] FAIL %1: got %2, expected %3", name, actual, expected), LogLevel.ERROR);
	}

	//------------------------------------------------------------------------------------------------
	protected static void CheckTrue(string name, bool actual)
	{
		if (actual)
			Check(name, 1, 1);
		else
			Check(name, 0, 1);
	}

	//------------------------------------------------------------------------------------------------
	protected static set<int> Players(int count)
	{
		set<int> present = new set<int>();
		for (int i = 1; i <= count; i++)
			present.Insert(i);
		return present;
	}

	//------------------------------------------------------------------------------------------------
	//! Deaths at 0: clock is up at 270 but 1 of 3 wait. Released alone at 1200.
	protected static void CaseLoneDeath()
	{
		GRAD_Endgame_Config cfg = new GRAD_Endgame_WaveTestConfig();
		GRAD_Endgame_FactionWave wave = new GRAD_Endgame_FactionWave();
		set<int> present = Players(8);

		wave.OnPlayerDeath(1, 0);
		Check("lone death: nothing at 269", wave.Tick(269, 8, present, cfg), 0);
		Check("lone death: nothing at 270, wave not full", wave.Tick(270, 8, present, cfg), 0);
		Check("lone death: countdown now points at the longest wait", wave.GetReleaseTime(300, present, cfg), 1200);
		Check("lone death: nothing at 1199", wave.Tick(1199, 8, present, cfg), 0);
		Check("lone death: released alone at 1200", wave.Tick(1200, 8, present, cfg), 1);
	}

	//------------------------------------------------------------------------------------------------
	//! Deaths at 0, 100, 260: all three go at 270, the third after ten seconds.
	protected static void CaseLuckyLateDeath()
	{
		GRAD_Endgame_Config cfg = new GRAD_Endgame_WaveTestConfig();
		GRAD_Endgame_FactionWave wave = new GRAD_Endgame_FactionWave();
		set<int> present = Players(8);

		wave.OnPlayerDeath(1, 0);
		wave.OnPlayerDeath(2, 100);
		wave.OnPlayerDeath(3, 260);
		Check("lucky: countdown ends at 270", wave.GetReleaseTime(260, present, cfg), 270);
		Check("lucky: nothing at 269", wave.Tick(269, 8, present, cfg), 0);
		Check("lucky: three released at 270", wave.Tick(270, 8, present, cfg), 3);
		CheckTrue("lucky: the late death is released", wave.IsReleased(3));
	}

	//------------------------------------------------------------------------------------------------
	//! Deaths at 0, 100, then 400: nothing at 270 (2 of 3); the third death fills the wave.
	protected static void CaseThirdDeathFillsWave()
	{
		GRAD_Endgame_Config cfg = new GRAD_Endgame_WaveTestConfig();
		GRAD_Endgame_FactionWave wave = new GRAD_Endgame_FactionWave();
		set<int> present = Players(8);

		wave.OnPlayerDeath(1, 0);
		wave.OnPlayerDeath(2, 100);
		Check("fill: nothing at 270 with two waiting", wave.Tick(270, 8, present, cfg), 0);
		Check("fill: nothing at 399", wave.Tick(399, 8, present, cfg), 0);
		wave.OnPlayerDeath(3, 400);
		Check("fill: three released when the third dies", wave.Tick(400, 8, present, cfg), 3);
	}

	//------------------------------------------------------------------------------------------------
	//! Deaths at 0, 5, 10, 15, 20: all five go at 270, the size is not a cap.
	protected static void CaseNoCap()
	{
		GRAD_Endgame_Config cfg = new GRAD_Endgame_WaveTestConfig();
		GRAD_Endgame_FactionWave wave = new GRAD_Endgame_FactionWave();
		set<int> present = Players(8);

		for (int i = 1; i <= 5; i++)
			wave.OnPlayerDeath(i, (i - 1) * 5);
		Check("no cap: five released at 270", wave.Tick(270, 8, present, cfg), 5);
		Check("no cap: nobody left waiting", wave.GetWaitingCount(present), 0);
	}

	//------------------------------------------------------------------------------------------------
	//! Deaths at 0, 100, 260, then 300: the death at 300 starts a new clock, earliest 570.
	protected static void CaseNewClockAfterRelease()
	{
		GRAD_Endgame_Config cfg = new GRAD_Endgame_WaveTestConfig();
		GRAD_Endgame_FactionWave wave = new GRAD_Endgame_FactionWave();
		set<int> present = Players(8);

		wave.OnPlayerDeath(1, 0);
		wave.OnPlayerDeath(2, 100);
		wave.OnPlayerDeath(3, 260);
		wave.Tick(270, 8, present, cfg);
		wave.OnPlayerSpawned(1);
		wave.OnPlayerSpawned(2);
		wave.OnPlayerSpawned(3);

		wave.OnPlayerDeath(4, 300);
		Check("new clock: countdown ends at 570", wave.GetReleaseTime(300, present, cfg), 570);
		wave.OnPlayerDeath(5, 310);
		wave.OnPlayerDeath(6, 320);
		Check("new clock: nothing at 569", wave.Tick(569, 8, present, cfg), 0);
		Check("new clock: three released at 570", wave.Tick(570, 8, present, cfg), 3);
	}

	//------------------------------------------------------------------------------------------------
	//! Death at 0, then that player disconnects; deaths at 130 and 140. The absent player does not
	//! count: the clock runs from 130 and the wave still needs a third.
	protected static void CaseAbsentPlayerDoesNotCount()
	{
		GRAD_Endgame_Config cfg = new GRAD_Endgame_WaveTestConfig();
		GRAD_Endgame_FactionWave wave = new GRAD_Endgame_FactionWave();
		set<int> present = Players(8);

		wave.OnPlayerDeath(1, 0);
		present.RemoveItem(1);   // disconnected; the death time is kept
		wave.OnPlayerDeath(2, 130);
		wave.OnPlayerDeath(3, 140);

		Check("absent: countdown ends at 400", wave.GetReleaseTime(200, present, cfg), 400);
		Check("absent: nothing at 400 with two waiting", wave.Tick(400, 7, present, cfg), 0);

		present.Insert(1);       // he reconnects, still dead, with his old death time
		Check("absent: back, three waiting, clock from 0 is long up", wave.Tick(401, 8, present, cfg), 3);
		CheckTrue("absent: the returned player is released", wave.IsReleased(1));
	}
}
