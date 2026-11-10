//------------------------------------------------------------------------------------------------
//! Typewriter sounds for the field-log HUD (decided 2026-11-10): one key strike per character that
//! appears in a widget, and the carriage-return bell ("ding") when a typed bar reaches 100 %.
//!
//! Client only; the sounds are local, nobody else hears them.
//!
//! The sounds come from an audio project (.acp) with two events, named in the config:
//!   m_sTypewriterSoundProject, m_sTypewriterKeyEvent ("GRAD_Endgame_TypeKey"),
//!   m_sTypewriterBellEvent ("GRAD_Endgame_TypeBell").
//! TODO (Workbench): create Sounds/GRAD_Endgame_Typewriter.acp with those two events. The key
//! event should pick at random from 4 to 6 short strike samples with a little pitch variation, so a
//! long line does not sound like a machine gun. Use samples you are allowed to ship (recorded
//! yourself, or CC0).
//!
//! The widgets use the two helper classes below:
//!   GRAD_Endgame_TypedText  reveals a line character by character, one strike per character
//!   GRAD_Endgame_TypedBar   draws "[######....]  60 %", one strike per new #, a bell at 100 %
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-10): not compiled, not run. AudioSystem.PlayEvent with a
//! transform is the call BreakingContact uses; playing it at the camera position is the guess.
class GRAD_Endgame_Typewriter
{
	protected static const float MIN_KEY_INTERVAL = 0.045;   //!< at most ~22 strikes a second
	protected static float s_fLastKey = -1;

	//------------------------------------------------------------------------------------------------
	//! One key strike. Throttled, so a widget that types fast does not stack sounds.
	static void PlayKey()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg || !cfg.GetTypewriterSound())
			return;

		float now = System.GetTickCount() * 0.001;
		if (s_fLastKey >= 0 && now - s_fLastKey < MIN_KEY_INTERVAL)
			return;
		s_fLastKey = now;

		Play(cfg.GetTypewriterSoundProject(), cfg.GetTypewriterKeyEvent());
	}

	//------------------------------------------------------------------------------------------------
	//! The bell at the end of a line: a bar reached 100 %.
	static void PlayBell()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg || !cfg.GetTypewriterSound())
			return;

		Play(cfg.GetTypewriterSoundProject(), cfg.GetTypewriterBellEvent());
	}

	//------------------------------------------------------------------------------------------------
	//! Play an event at the listener, so it sounds like the HUD, not like the world.
	protected static void Play(ResourceName project, string eventName)
	{
		if (project.IsEmpty() || eventName.IsEmpty() || System.IsConsoleApp())
			return;

		vector transform[4];
		Math3D.MatrixIdentity4(transform);

		CameraManager cameraManager = GetGame().GetCameraManager();
		if (cameraManager && cameraManager.CurrentCamera())
			transform[3] = cameraManager.CurrentCamera().GetOrigin();

		AudioSystem.PlayEvent(project, eventName, transform);
	}
}

//------------------------------------------------------------------------------------------------
//! A line that types itself. Give it the text, call Update every frame, show GetShown().
//! Setting a new text starts typing again from the first character that differs, so a value that
//! changes from "02:15" to "02:14" only types the last character.
class GRAD_Endgame_TypedText
{
	protected string m_sTarget;
	protected int m_iShown;           //!< characters of m_sTarget already visible
	protected float m_fBuffer;
	protected float m_fCharsPerSecond = 30;
	protected bool m_bSound = true;

	//------------------------------------------------------------------------------------------------
	void GRAD_Endgame_TypedText(float charsPerSecond = 30, bool sound = true)
	{
		m_fCharsPerSecond = Math.Max(charsPerSecond, 1);
		m_bSound = sound;
	}

	//------------------------------------------------------------------------------------------------
	void SetText(string text)
	{
		if (text == m_sTarget)
			return;

		// Keep the part that is unchanged; type from the first difference.
		int same;
		int limit = Math.Min(Math.Min(text.Length(), m_sTarget.Length()), m_iShown);
		while (same < limit && text.Get(same) == m_sTarget.Get(same))
			same++;

		m_sTarget = text;
		m_iShown = same;
	}

	//------------------------------------------------------------------------------------------------
	//! Show everything at once, silently (for example when the field log is reopened).
	void Complete()
	{
		m_iShown = m_sTarget.Length();
	}

	//------------------------------------------------------------------------------------------------
	bool IsComplete()
	{
		return m_iShown >= m_sTarget.Length();
	}

	//------------------------------------------------------------------------------------------------
	void Update(float timeSlice)
	{
		if (IsComplete())
			return;

		m_fBuffer += timeSlice * m_fCharsPerSecond;
		while (m_fBuffer >= 1 && !IsComplete())
		{
			m_fBuffer -= 1;
			string c = m_sTarget.Get(m_iShown);
			m_iShown++;

			// Spaces and line breaks make no strike, as on a real machine.
			if (m_bSound && c != " " && c != "\n")
				GRAD_Endgame_Typewriter.PlayKey();
		}
	}

	//------------------------------------------------------------------------------------------------
	string GetShown()
	{
		return m_sTarget.Substring(0, m_iShown);
	}
}

//------------------------------------------------------------------------------------------------
//! A typed progress bar: "[######....]  60 %". One strike for every new #, the bell once when it
//! reaches 100 %. Going backwards (a reset transmission) is silent.
class GRAD_Endgame_TypedBar
{
	protected int m_iWidth;
	protected int m_iFilled = -1;     //!< -1 = nothing drawn yet
	protected bool m_bRang;

	//------------------------------------------------------------------------------------------------
	void GRAD_Endgame_TypedBar(int width = 20)
	{
		m_iWidth = Math.Max(width, 1);
	}

	//------------------------------------------------------------------------------------------------
	//! \param[in] fraction progress 0..1
	//! \param[in] sound false for the first draw after opening a widget, so it does not rattle off
	//!            every # at once
	//! \return the bar as text
	string Update(float fraction, bool sound = true)
	{
		fraction = Math.Clamp(fraction, 0, 1);
		int filled = Math.Floor(fraction * m_iWidth);

		if (m_iFilled >= 0 && sound)
		{
			for (int i = m_iFilled; i < filled; i++)
				GRAD_Endgame_Typewriter.PlayKey();
		}

		if (fraction >= 1)
		{
			if (!m_bRang && m_iFilled >= 0 && sound)
				GRAD_Endgame_Typewriter.PlayBell();
			m_bRang = true;
		}
		else
		{
			m_bRang = false;
		}

		m_iFilled = filled;

		string bar = "[";
		for (int j = 0; j < m_iWidth; j++)
		{
			if (j < filled)
				bar += "#";
			else
				bar += ".";
		}
		return string.Format("%1]  %2 \%", bar, Math.Round(fraction * 100));
	}

	//------------------------------------------------------------------------------------------------
	//! Forget the state, e.g. when the bar starts tracking a new transmission.
	void Reset()
	{
		m_iFilled = -1;
		m_bRang = false;
	}
}
