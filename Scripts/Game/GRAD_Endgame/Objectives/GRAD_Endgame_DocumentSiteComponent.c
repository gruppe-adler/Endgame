//------------------------------------------------------------------------------------------------
class GRAD_Endgame_DocumentSiteComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! A document site: counts the copies each of the two sides has made here and lets only one
//! player copy at a time.
//!
//! Server rules for a copy (AddCopy):
//!  - the player belongs to one of the two Endgame factions;
//!  - if the game-mode component is in the world, the phase is DOCUMENTS (without the component,
//!    as in the M0 test world, any time is fine);
//!  - the side is below the config's copies per site (0 = unlimited).
//!
//! Replication: the two counts and the "in use by" id are replicated values, so every client and
//! every late joiner reads the current state (only change callbacks do not fire on a late
//! joiner's first sync, so the UI reads the getters directly).
//! TODO (M4, "Network contract" in the plan): with showEnemyProgress off the per-faction counts
//! must not be replicated to everyone; send them by owner RPC instead.
//!
//! Attach to a prefab with an ActionsManagerComponent carrying GRAD_Endgame_CopyDocumentAction.
//!
//! CHANGED WITHOUT WORKBENCH (2026-11-10): not compiled, not run.
class GRAD_Endgame_DocumentSiteComponent : ScriptComponent
{
	[RplProp()]
	protected int m_iCopiesOne;

	[RplProp()]
	protected int m_iCopiesTwo;

	//! Player id of whoever is copying right now, 0 = free.
	[RplProp()]
	protected int m_iUserId;

	//! Chosen for this match (GRAD_Endgame_SiteSelection). Inactive sites show no action and their
	//! garrison does not spawn.
	[RplProp()]
	protected bool m_bActive = true;

	protected static ref array<GRAD_Endgame_DocumentSiteComponent> s_aAll = {};

	//! Server: id of this site's map marker ("doc_<index>").
	protected string m_sMarkerId;

	protected const int STALE_LOCK_MS = 30000;   //!< a lock nobody released is dropped after this

	//------------------------------------------------------------------------------------------------
	static array<GRAD_Endgame_DocumentSiteComponent> GetAll()
	{
		return s_aAll;
	}

	//------------------------------------------------------------------------------------------------
	bool IsSiteActive()
	{
		return m_bActive;
	}

	//------------------------------------------------------------------------------------------------
	string GetMarkerId()
	{
		if (m_sMarkerId.IsEmpty())
			m_sMarkerId = "doc_" + s_aAll.Find(this).ToString();
		return m_sMarkerId;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: switch the site on or off for this match.
	void SetActive(bool active)
	{
		if (!Replication.IsServer() || m_bActive == active)
			return;

		m_bActive = active;
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_aAll.Insert(this);
	}

	//------------------------------------------------------------------------------------------------
	//! Faction key of a player's slot, or empty. Works on server and clients.
	static string GetFactionKeyOfPlayer(int playerId)
	{
		COA_SlottingManager slottingManager = COA_SlottingManager.GetInstance();
		if (!slottingManager)
			return string.Empty;

		Faction faction = slottingManager.GetPlayerSlotFaction(playerId);
		if (!faction)
			return string.Empty;

		return faction.GetFactionKey();
	}

	//------------------------------------------------------------------------------------------------
	//! Copies made here by this faction (0 for any other key).
	int GetCopyCount(string factionKey)
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
			return 0;
		if (factionKey == cfg.GetFactionKeyOne())
			return m_iCopiesOne;
		if (factionKey == cfg.GetFactionKeyTwo())
			return m_iCopiesTwo;
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Player id of the current user, 0 = free.
	int GetUserId()
	{
		return m_iUserId;
	}

	//------------------------------------------------------------------------------------------------
	//! Why this player cannot copy here right now, or empty if he can. Runs on clients (to grey out
	//! the action) and on the server (to decide).
	string GetRefusalReason(int playerId)
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
			return "No config";

		if (!m_bActive)
			return "No documents here";

		string factionKey = GetFactionKeyOfPlayer(playerId);
		if (!cfg.IsPlayerFaction(factionKey))
			return "Not for your side";

		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (gameMode && gameMode.GetPhase() != GRAD_Endgame_EPhase.DOCUMENTS)
			return "Not in this phase";

		int limit = cfg.GetCopiesPerSitePerFaction();
		if (limit > 0 && GetCopyCount(factionKey) >= limit)
			return "Already copied";

		if (m_iUserId != 0 && m_iUserId != playerId)
			return "In use";

		return string.Empty;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: a player started the hold. Returns false if someone else is at it.
	bool Lock(int playerId)
	{
		if (!Replication.IsServer())
			return false;
		if (m_iUserId != 0 && m_iUserId != playerId)
			return false;
		if (m_iUserId == playerId)
			return true;

		m_iUserId = playerId;
		Replication.BumpMe();

		GetGame().GetCallqueue().Remove(DropStaleLock);
		GetGame().GetCallqueue().CallLater(DropStaleLock, STALE_LOCK_MS, false);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: the hold ended (completed or cancelled).
	void Unlock(int playerId)
	{
		if (!Replication.IsServer() || m_iUserId != playerId)
			return;

		m_iUserId = 0;
		Replication.BumpMe();
		GetGame().GetCallqueue().Remove(DropStaleLock);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: a cancel that never arrived (disconnect, death) must not block the site for good.
	protected void DropStaleLock()
	{
		if (m_iUserId == 0)
			return;

		m_iUserId = 0;
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	//! Server only: this player completed a hold. Returns true if the copy counted.
	bool AddCopy(int playerId)
	{
		if (!Replication.IsServer())
			return false;

		string reason = GetRefusalReason(playerId);
		Unlock(playerId);
		if (!reason.IsEmpty())
		{
			GRAD_Endgame_MatchLog.Write("copy_refused", GetFactionKeyOfPlayer(playerId), playerId, GetOwner().GetOrigin(), reason);
			return false;
		}

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		string factionKey = GetFactionKeyOfPlayer(playerId);
		if (factionKey == cfg.GetFactionKeyOne())
			m_iCopiesOne++;
		else
			m_iCopiesTwo++;
		Replication.BumpMe();

		// The game-mode component keeps the faction total, writes the "copy" log line and checks
		// the threshold. Without it (M0 test world) the site logs the copy itself.
		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (gameMode)
			gameMode.AddDocument(factionKey, playerId, GetOwner().GetOrigin(), GetMarkerId());
		else
			GRAD_Endgame_MatchLog.Write("copy", factionKey, playerId, GetOwner().GetOrigin(), string.Format("site count %1", GetCopyCount(factionKey)));

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(DropStaleLock);
		s_aAll.RemoveItem(this);
		super.OnDelete(owner);
	}
}
