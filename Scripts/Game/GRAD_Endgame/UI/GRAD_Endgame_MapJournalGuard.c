//------------------------------------------------------------------------------------------------
//! Guard for a vanilla crash: clicking a task icon on the map calls
//! SCR_TaskManagerUIComponent.SetTaskListVisibility, which calls SCR_MapJournalUI.SetJournalVisibility,
//! and that throws "NULL pointer to instance" when the map configuration in use did not give the
//! journal UI its widgets (seen with COALITION's map, 2026-11-11, SCR_MapJournalUI.c line 164).
//!
//! This only skips the call when the journal has no frame or task list to show, so the click does
//! nothing instead of throwing. The real fix is in the map configuration: it must contain the
//! journal map UI component with its layout, like the vanilla gadget map.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-11): not compiled. Which member is null at line 164 is a
//! guess from the member list in the API docs (m_wJournalFrame, m_MapTaskList).
modded class SCR_MapJournalUI
{
	protected bool m_bGradEndgameWarned;

	//------------------------------------------------------------------------------------------------
	override void SetJournalVisibility(bool visibility)
	{
		if (!m_wJournalFrame || !m_MapTaskList)
		{
			if (!m_bGradEndgameWarned)
			{
				m_bGradEndgameWarned = true;
				Print("[GRAD_Endgame] Map journal UI has no frame or task list in this map configuration; task click ignored.", LogLevel.WARNING);
			}
			return;
		}

		super.SetJournalVisibility(visibility);
	}
}
