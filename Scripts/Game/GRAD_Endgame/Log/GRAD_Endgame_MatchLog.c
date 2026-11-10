//------------------------------------------------------------------------------------------------
//! Server-side match log: one line per event, semicolon separated, one file per match in
//! $profile:GRAD_Endgame/. Opens in a spreadsheet.
//!
//! Line format:  seconds since match start ; event ; faction key ; player id ; position ; details
//!
//! Every system writes through Write(). With the config's debug log on, each line is also printed
//! to the console; Debug() prints only then and never writes to the file.
//!
//! NOT COMPILED YET (written without Workbench). The file calls (FileIO.MakeDirectory,
//! FileIO.OpenFile, FileHandle.WriteLine, FileHandle.Close) are the ones to check first.
class GRAD_Endgame_MatchLog
{
	static const string DIRECTORY = "$profile:GRAD_Endgame";
	static const string PREFIX = "[GRAD_Endgame] ";

	protected static string s_sFilePath;
	protected static float s_fMatchStart = -1;   //!< world time in seconds at match_start
	protected static bool s_bFileFailed;

	//------------------------------------------------------------------------------------------------
	//! Start a new log file. Call once when the match begins (server only).
	static void StartMatch(string details = "")
	{
		if (!Replication.IsServer())
			return;

		int year, month, day, hour, minute, second;
		System.GetYearMonthDay(year, month, day);
		System.GetHourMinuteSecond(hour, minute, second);

		s_sFilePath = string.Format("%1/match_%2-%3-%4_%5-%6-%7.log", DIRECTORY, year, Pad(month), Pad(day), Pad(hour), Pad(minute), Pad(second));
		s_fMatchStart = Now();
		s_bFileFailed = false;

		FileIO.MakeDirectory(DIRECTORY);
		WriteRaw("seconds;event;faction;player;position;details");
		GRAD_Endgame_Replay.ResetRecording();
		Write("match_start", "", 0, vector.Zero, details);

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (cfg && cfg.GetRunSelfTests())
		{
			GRAD_Endgame_FactionWaveTest.Run();
			GRAD_Endgame_SiteSelectionTest.Run();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Write one event line. Server only; silently ignored on clients.
	//! \param[in] eventName short name from the "Match log" table in the plan
	//! \param[in] factionKey faction the event belongs to, or empty
	//! \param[in] playerId player the event belongs to, or 0
	//! \param[in] position world position, or vector.Zero
	//! \param[in] details free text; semicolons are replaced so the columns stay intact
	static void Write(string eventName, string factionKey = "", int playerId = 0, vector position = vector.Zero, string details = "")
	{
		if (!Replication.IsServer())
			return;

		// Events before StartMatch (for example in a test world without the game-mode component)
		// still get a file, so nothing is lost.
		if (s_sFilePath.IsEmpty())
			StartMatch("implicit start: no game-mode component called StartMatch");

		string positionText = "";
		if (position != vector.Zero)
			positionText = string.Format("%1 %2 %3", Math.Round(position[0]), Math.Round(position[1]), Math.Round(position[2]));

		details.Replace(";", ",");

		string playerText = "";
		if (playerId > 0)
			playerText = playerId.ToString();

		GRAD_Endgame_Replay.OnLogEvent(eventName, factionKey, details);

		string line = string.Format("%1;%2;%3;%4;%5;%6", Math.Round(Now() - s_fMatchStart), eventName, factionKey, playerText, positionText, details);
		WriteRaw(line);

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (cfg && cfg.GetDebugLog())
			Print(PREFIX + line, LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! Console-only line, printed when the config's debug log is on. Any machine.
	static void Debug(string message)
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (cfg && cfg.GetDebugLog())
			Print(PREFIX + message, LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! Always printed, any machine. For configuration mistakes that must not go unnoticed.
	static void Error(string message)
	{
		Print(PREFIX + message, LogLevel.ERROR);
	}

	//------------------------------------------------------------------------------------------------
	protected static float Now()
	{
		return GetGame().GetWorld().GetWorldTime() * 0.001;
	}

	//------------------------------------------------------------------------------------------------
	protected static string Pad(int value)
	{
		if (value < 10)
			return "0" + value.ToString();
		return value.ToString();
	}

	//------------------------------------------------------------------------------------------------
	protected static void WriteRaw(string line)
	{
		if (s_bFileFailed)
			return;

		FileHandle file = FileIO.OpenFile(s_sFilePath, FileMode.APPEND);
		if (!file)
		{
			// Report once, then stop trying; the console still gets the lines with debug log on.
			s_bFileFailed = true;
			Print(PREFIX + "Cannot open match log file: " + s_sFilePath, LogLevel.ERROR);
			return;
		}

		file.WriteLine(line);
		file.Close();
	}
}
