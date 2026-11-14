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

	//! When the current copy started, on the synced server clock. Lets every client draw the same
	//! progress bar for the copying player and his team without per-frame replication: the bar is
	//! (now - start) / copy duration, computed locally (see GetCopyFraction).
	[RplProp()]
	protected WorldTimestamp m_CopyStartTs;

	//! Hold time of the copy that is running, in seconds: the action's own duration (the prefab's
	//! Duration), as reported by the server when the hold started. The widget bar uses this and
	//! not the config, so it fills in exactly the time the engine's hold takes.
	[RplProp()]
	protected float m_fCopyDuration;

	//! Chosen for this match (GRAD_Endgame_SiteSelection). Inactive sites show no action and their
	//! garrison does not spawn.
	[RplProp(onRplName: "OnActiveChanged")]
	protected bool m_bActive = true;

	//! Put the documents on a table that is already in the world near the placed position, instead
	//! of where the site stands. The server picks the table when the site is chosen for the match.
	//! Off = the documents stay exactly where they were placed (hand-placed on a table).
	[Attribute(defvalue: "1", desc: "Automatic: on a free table near this site at match start. Off: stay where placed.")]
	protected bool m_bAutoPlace = true;

	[Attribute(defvalue: "100", desc: "Metres around the placed position to look for a table.", params: "5 300 1")]
	protected float m_fPlaceRadius = 100;

	//! Where the server put the documents (valid when m_bPlaced); clients move there too.
	[RplProp(onRplName: "OnPlacedChanged")]
	protected vector m_vPlaced;

	[RplProp(onRplName: "OnPlacedChanged")]
	protected bool m_bPlaced;

	protected static ref array<GRAD_Endgame_DocumentSiteComponent> s_aAll = {};

	//! Tables already taken by a site this match (several sites must not share one).
	protected static ref array<IEntity> s_aUsedTables = {};

	protected static const float MAX_TABLE_HEIGHT_DIFF = 2.5;   //!< metres above or below the placed spot
	protected static const float TABLE_DROP_HEIGHT = 0.05;   //!< metres above the table top
	protected static const float TABLE_MIN_SPACING = 3;   //!< two sites on one table spot would overlap

	//! Server: id of this site's map marker ("doc_<index>").
	protected string m_sMarkerId;

	protected const int STALE_LOCK_MS = 30000;   //!< a lock nobody released is dropped after this

	//------------------------------------------------------------------------------------------------
	//! True if a document site already put its documents on this table.
	static bool IsTableUsed(IEntity table)
	{
		return s_aUsedTables.Contains(table);
	}

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
		if (!Replication.IsServer())
			return;

		// Sites start active, so "chosen" is no change of state: place them anyway.
		if (active && m_bAutoPlace)
			PlaceOnTable();

		if (m_bActive == active)
			return;

		m_bActive = active;
		Replication.BumpMe();
		ApplyActive();
	}

	//------------------------------------------------------------------------------------------------
	//! A site that is not part of this match must not lie around as a prop: hide it. (The prop
	//! stays where it was placed; only the rendering is switched off.)
	protected void ApplyActive()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		if (m_bActive)
			owner.SetFlags(EntityFlags.VISIBLE, true);
		else
			owner.ClearFlags(EntityFlags.VISIBLE, true);
	}

	//------------------------------------------------------------------------------------------------
	//! Client: the server switched this site on or off.
	protected void OnActiveChanged()
	{
		ApplyActive();
	}

	//------------------------------------------------------------------------------------------------
	//! Server: pick a RANDOM suitable free table around the placed position and put the documents on
	//! it (the nearest table would be the same one every match). Without a table the documents stay
	//! where they are.
	protected void PlaceOnTable()
	{
		if (m_bPlaced)
			return;

		array<ref GRAD_Endgame_TableHit> hits = {};
		GRAD_Endgame_TableScan.Find(GetOwner().GetOrigin(), m_fPlaceRadius, hits);

		array<ref GRAD_Endgame_TableHit> candidates = {};
		foreach (GRAD_Endgame_TableHit hit : hits)
		{
			if (!GRAD_Endgame_TableScan.IsSuitable(hit))
				continue;
			// Same floor: a table in an upper storey, or on a roof, is no table for this site.
			if (Math.AbsFloat(hit.m_vTop[1] - GetOwner().GetOrigin()[1]) > MAX_TABLE_HEIGHT_DIFF)
				continue;
			if (s_aUsedTables.Contains(hit.m_Entity))
				continue;

			bool crowded;
			foreach (IEntity used : s_aUsedTables)
			{
				if (used && vector.Distance(used.GetOrigin(), hit.m_Entity.GetOrigin()) < TABLE_MIN_SPACING)
					crowded = true;
			}
			if (crowded)
				continue;

			candidates.Insert(hit);
		}

		if (candidates.IsEmpty())
		{
			GRAD_Endgame_MatchLog.Write("doc_placed", "", 0, GetOwner().GetOrigin(), "no free table in reach, documents stay where placed");
			return;
		}

		GRAD_Endgame_TableHit chosen = candidates.GetRandomElement();
		s_aUsedTables.Insert(chosen.m_Entity);
		// A little above the surface: the site is a physics prop, gravity sets it down on the table.
		m_vPlaced = chosen.m_vTop + Vector(0, TABLE_DROP_HEIGHT, 0);
		m_bPlaced = true;
		Replication.BumpMe();
		ApplyPlaced();
		GRAD_Endgame_MatchLog.Write("doc_placed", "", 0, m_vPlaced, string.Format("on %1, %2 m from the placed spot, one of %3 free tables", FilePath.StripPath(chosen.m_sName), Math.Round(chosen.m_fDistance), candidates.Count()));
	}

	protected vector m_vLastOrigin;

	//------------------------------------------------------------------------------------------------
	//! Server: something else moved the site (the Game Master, to correct the placement): the
	//! clients follow, as they do for a table placement.
	protected void WatchMoved()
	{
		vector origin = GetOwner().GetOrigin();
		if (vector.DistanceSq(origin, m_vLastOrigin) < 0.25)
			return;

		m_vLastOrigin = origin;
		m_vPlaced = origin;
		m_bPlaced = true;
		Replication.BumpMe();
		GRAD_Endgame_MatchLog.Write("doc_moved", "", 0, origin, "site moved (Game Master), clients follow");
	}

	//------------------------------------------------------------------------------------------------
	//! Debug marker, for the Game Master while the editor is open: a sphere and a tall line where
	//! the documents are supposed to be. Green = a site of this match, grey = not chosen. If the
	//! documents are not there, the placement or the prop is wrong, not the marker.
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		if (!IsDebugViewer())
			return;

		vector position = owner.GetOrigin();
		if (m_bPlaced)
			position = m_vPlaced;

		int color = Color.GRAY;
		if (m_bActive)
			color = Color.GREEN;

		Shape.CreateSphere(color, ShapeFlags.WIREFRAME | ShapeFlags.NOZBUFFER | ShapeFlags.ONCE, position, 0.35);
		vector points[2];
		points[0] = position;
		points[1] = position + Vector(0, 25, 0);
		Shape.CreateLines(color, ShapeFlags.NOZBUFFER | ShapeFlags.ONCE, points, 2);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsDebugViewer()
	{
		// Only while the Game Master view is open. Having the permission is not enough, and the
		// debug log switch (on in the test config) must not show it to everyone.
		SCR_EditorManagerEntity editorManager = SCR_EditorManagerEntity.GetInstance();
		return editorManager && !editorManager.IsLimited() && editorManager.IsOpened();
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyPlaced()
	{
		ApplyActive();
		if (m_bPlaced)
		{
			GetOwner().SetOrigin(m_vPlaced);
			m_vLastOrigin = m_vPlaced;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Client: the server chose a table.
	protected void OnPlacedChanged()
	{
		ApplyPlaced();
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		GRAD_Endgame_Rpl.Require(owner, "Document site");
		s_aAll.Insert(this);

		// Debug marker for the Game Master (see EOnFrame); not on a dedicated server (no screen).
		if (System.IsConsoleApp() == false)
			SetEventMask(owner, EntityEvent.FRAME);

		// A client that joins late may not get the change callback for the first sync.
		if (!Replication.IsServer())
			GetGame().GetCallqueue().CallLater(ApplyPlaced, 3000, false);
		else
		{
			m_vLastOrigin = owner.GetOrigin();
			GetGame().GetCallqueue().CallLater(WatchMoved, 1000, true);
		}
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
		if (gameMode && !GRAD_Endgame_GameModeComponent.IsDocumentsOpenFor(factionKey))
			return "Take your FOB first";

		int limit = cfg.GetCopiesPerSitePerFaction();
		if (limit > 0 && GetCopyCount(factionKey) >= limit)
			return "Already copied";

		if (m_iUserId != 0 && m_iUserId != playerId)
			return "In use";

		return string.Empty;
	}

	//------------------------------------------------------------------------------------------------
	//! 0..1 progress of the copy running now, or -1 when nobody copies. Any machine.
	float GetCopyFraction()
	{
		if (m_iUserId == 0 || m_CopyStartTs == null)
			return -1;

		ChimeraWorld world = GetGame().GetWorld();
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!world || m_fCopyDuration <= 0)
			return -1;

		return Math.Clamp(world.GetServerTimestamp().DiffSeconds(m_CopyStartTs) / m_fCopyDuration, 0, 1);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: a player started the hold. Returns false if someone else is at it.
	bool Lock(int playerId, float duration)
	{
		if (!Replication.IsServer())
			return false;
		if (m_iUserId != 0 && m_iUserId != playerId)
			return false;
		if (m_iUserId == playerId)
			return true;

		m_iUserId = playerId;
		m_fCopyDuration = duration;
		ChimeraWorld world = GetGame().GetWorld();
		if (world)
			m_CopyStartTs = world.GetServerTimestamp();
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
		GetGame().GetCallqueue().Remove(ApplyPlaced);
		GetGame().GetCallqueue().Remove(WatchMoved);
		s_aAll.RemoveItem(this);
		super.OnDelete(owner);
	}
}
