//------------------------------------------------------------------------------------------------
//! Client: plays the after-action replay on the map. The data arrives from the server through
//! GRAD_Endgame_GameModeComponent (frames one by one, then events, then "start").
//!
//! Each player is a local map marker that is replaced on every step; no map config or custom map
//! layer is needed. The map is opened with the vanilla map menu.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-10): not compiled, not run.
class GRAD_Endgame_ReplayPlayer
{
	protected static ref GRAD_Endgame_Replay s_Data;
	protected static float s_fSpeed = 1;
	protected static float s_fTime;
	protected static int s_iNextEvent;
	protected static bool s_bPlaying;
	protected static ref map<int, ref SCR_MapMarkerBase> s_mMarkers = new map<int, ref SCR_MapMarkerBase>();

	protected static const float STEP_SECONDS = 0.5;   //!< real seconds between display updates

	//------------------------------------------------------------------------------------------------
	static void Begin(float interval, float speed, array<int> rosterIds, array<string> rosterNames)
	{
		Stop();
		s_Data = new GRAD_Endgame_Replay();
		s_Data.m_fInterval = Math.Max(interval, 1);
		s_fSpeed = Math.Max(speed, 1);
		for (int i = 0; i < rosterIds.Count() && i < rosterNames.Count(); i++)
			s_Data.m_mNames.Set(rosterIds[i], rosterNames[i]);
	}

	//------------------------------------------------------------------------------------------------
	static void AddFrame(float time, array<int> ids, array<int> flags, array<float> xs, array<float> zs)
	{
		if (!s_Data)
			return;

		GRAD_Endgame_ReplayFrame frame = new GRAD_Endgame_ReplayFrame();
		frame.m_fTime = time;
		frame.m_aIds.Copy(ids);
		frame.m_aFlags.Copy(flags);
		frame.m_aX.Copy(xs);
		frame.m_aZ.Copy(zs);
		s_Data.m_aFrames.Insert(frame);
	}

	//------------------------------------------------------------------------------------------------
	static void SetEvents(array<float> times, array<string> texts)
	{
		if (!s_Data)
			return;
		s_Data.m_aEventTimes.Copy(times);
		s_Data.m_aEventTexts.Copy(texts);
	}

	//------------------------------------------------------------------------------------------------
	//! All data is here: open the map and start stepping.
	static void Start()
	{
		if (!s_Data || s_Data.m_aFrames.IsEmpty())
			return;

		s_fTime = 0;
		s_iNextEvent = 0;
		s_bPlaying = true;

		GetGame().GetMenuManager().OpenMenu(ChimeraMenuPreset.MapMenu);
		SCR_HintManagerComponent.ShowCustomHint("Replay of the match", "After-action replay", 5);
		GetGame().GetCallqueue().CallLater(Step, STEP_SECONDS * 1000, true);
	}

	//------------------------------------------------------------------------------------------------
	static bool IsPlaying()
	{
		return s_bPlaying;
	}

	//------------------------------------------------------------------------------------------------
	protected static void Step()
	{
		if (!s_bPlaying || !s_Data || s_Data.m_aFrames.IsEmpty())
		{
			Stop();
			return;
		}

		s_fTime += STEP_SECONDS * s_fSpeed;

		float duration = s_Data.GetDuration();
		if (s_fTime > duration + s_Data.m_fInterval)
		{
			Stop();
			return;
		}

		// Latest frame at or before the playback time.
		GRAD_Endgame_ReplayFrame frame = s_Data.m_aFrames[0];
		foreach (GRAD_Endgame_ReplayFrame candidate : s_Data.m_aFrames)
		{
			if (candidate.m_fTime > s_fTime)
				break;
			frame = candidate;
		}
		Show(frame);

		while (s_iNextEvent < s_Data.m_aEventTimes.Count() && s_Data.m_aEventTimes[s_iNextEvent] <= s_fTime)
		{
			SCR_HintManagerComponent.ShowCustomHint(s_Data.m_aEventTexts[s_iNextEvent], "Replay", 4);
			s_iNextEvent++;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void Show(notnull GRAD_Endgame_ReplayFrame frame)
	{
		SCR_MapMarkerManagerComponent manager = SCR_MapMarkerManagerComponent.GetInstance();
		if (!manager)
			return;

		HideAll();

		for (int i = 0; i < frame.m_aIds.Count(); i++)
		{
			int flags = frame.m_aFlags[i];
			bool alive = (flags % 2) == 1;
			string letter = GRAD_Endgame_Replay.FACTION_LETTER_ONE;
			if (flags >= 2)
				letter = GRAD_Endgame_Replay.FACTION_LETTER_TWO;

			string label = letter + " " + s_Data.m_mNames.Get(frame.m_aIds[i]);
			if (!alive)
				label = "x " + label;

			SCR_MapMarkerBase marker = new SCR_MapMarkerBase();
			marker.SetType(SCR_EMapMarkerType.PLACED_CUSTOM);
			marker.SetWorldPos(frame.m_aX[i], frame.m_aZ[i]);
			marker.SetCustomText(label);
			manager.InsertStaticMarker(marker, true);
			s_mMarkers.Set(frame.m_aIds[i], marker);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void HideAll()
	{
		SCR_MapMarkerManagerComponent manager = SCR_MapMarkerManagerComponent.GetInstance();
		if (manager)
		{
			foreach (int id, SCR_MapMarkerBase marker : s_mMarkers)
				manager.RemoveStaticMarker(marker);
		}
		s_mMarkers.Clear();
	}

	//------------------------------------------------------------------------------------------------
	static void Stop()
	{
		GetGame().GetCallqueue().Remove(Step);
		HideAll();

		if (s_bPlaying)
		{
			SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
			if (mapEntity && mapEntity.IsOpen())
				mapEntity.CloseMap();
		}
		s_bPlaying = false;
	}
}
