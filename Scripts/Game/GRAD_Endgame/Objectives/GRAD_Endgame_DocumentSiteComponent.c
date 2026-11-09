//------------------------------------------------------------------------------------------------
class GRAD_Endgame_DocumentSiteComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! A document site (T5): holds how many copies have been made here. The count is a replicated
//! property, so every client, and every late joiner, reads the same current value (BreakingContact's
//! JIP notes: replicated properties arrive with their current value; only change callbacks do not
//! fire on that first sync, so the UI reads the value directly instead of waiting for a callback).
//!
//! Attach to a prefab with an ActionsManagerComponent carrying GRAD_Endgame_CopyDocumentAction.
//! Server decides; clients only display the count and send the hold action.
class GRAD_Endgame_DocumentSiteComponent : ScriptComponent
{
	[RplProp(onRplName: "OnCopyCountChanged")]
	protected int m_iCopyCount;

	//------------------------------------------------------------------------------------------------
	int GetCopyCount()
	{
		return m_iCopyCount;
	}

	//------------------------------------------------------------------------------------------------
	//! Server only: one copy completed by this player.
	void AddCopy(int playerId)
	{
		if (!Replication.IsServer())
			return;

		m_iCopyCount++;
		Print(string.Format("[GRAD_Endgame] Document site %1: copy %2 made by player %3", GetOwner().GetName(), m_iCopyCount, playerId), LogLevel.NORMAL);
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	//! Client: the value changed. Not called for the first sync of a late joiner, which is fine
	//! because everything reads GetCopyCount() directly.
	protected void OnCopyCountChanged()
	{
		Print(string.Format("[GRAD_Endgame] Document site %1: count is now %2", GetOwner().GetName(), m_iCopyCount), LogLevel.NORMAL);
	}
}
