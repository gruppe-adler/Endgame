//------------------------------------------------------------------------------------------------
//! One piece of map information for one side: a document area, a radio site, a cipher location or
//! a direction-finding fix. Radius 0 = an exact point.
class GRAD_Endgame_MarkerData
{
	string m_sId;
	vector m_vPosition;
	float m_fRadius;
	string m_sLabel;

	//------------------------------------------------------------------------------------------------
	void GRAD_Endgame_MarkerData(string id, vector position, float radius, string label)
	{
		m_sId = id;
		m_vPosition = position;
		m_fRadius = radius;
		m_sLabel = label;
	}
}

//------------------------------------------------------------------------------------------------
//! Server side of the information rules ("Network contract" in the plan). Everything a side may
//! know but the other may not goes through here and travels by owner RPC, never as a replicated
//! value, so a modified client cannot read the enemy's information.
//!
//! The service remembers what each side currently knows (markers per faction, the decoded order
//! per faction) and SyncPlayer() sends it again to a player who joins or respawns, because owner
//! RPCs are not replayed for late joiners.
//!
//! Server only. The client end is the modded COA_PlayerRplToOwnerManager in
//! GRAD_Endgame_InfoClient.c.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-10): not compiled, not run.
class GRAD_Endgame_InfoService
{
	//! faction key -> marker id -> marker
	protected static ref map<string, ref map<string, ref GRAD_Endgame_MarkerData>> s_mMarkers = new map<string, ref map<string, ref GRAD_Endgame_MarkerData>>();

	//! faction key -> decoded order (title, text and the cipher location as a marker)
	protected static ref map<string, string> s_mOrderText = new map<string, string>();

	//------------------------------------------------------------------------------------------------
	//! Forget everything; call at the start of a new round.
	static void Reset()
	{
		s_mMarkers.Clear();
		s_mOrderText.Clear();
		GRAD_Endgame_TaskService.Reset();
	}

	//------------------------------------------------------------------------------------------------
	//! Show (or move) an objective for every player of a faction, as a native task
	//! (GRAD_Endgame_TaskService), which replicates and survives join in progress by itself.
	//! \param[in] radius > 0: the position is an area centre (the task sits at the blurred centre)
	//! \param[in] parentId our id of a parent task of the same side, or empty
	static void SetMarker(string factionKey, string id, vector position, float radius, string label, string description = "", string parentId = "")
	{
		if (!Replication.IsServer() || factionKey.IsEmpty())
			return;

		if (description.IsEmpty() && radius > 0)
			description = string.Format("Somewhere within %1 m of the marked point.", Math.Round(radius));

		GRAD_Endgame_TaskService.Upsert(factionKey, id, label, description, position, parentId);
	}

	//------------------------------------------------------------------------------------------------
	//! \param[in] done true: the side achieved it (task completed); false: it no longer applies
	//!            (task removed)
	static void RemoveMarker(string factionKey, string id, bool done = false)
	{
		if (!Replication.IsServer())
			return;

		if (done)
			GRAD_Endgame_TaskService.Complete(factionKey, id);
		else
			GRAD_Endgame_TaskService.Remove(factionKey, id);
	}

	//------------------------------------------------------------------------------------------------
	//! A short on-screen message for every player of a faction. Not remembered for late joiners.
	static void HintFaction(string factionKey, string title, string text)
	{
		if (!Replication.IsServer())
			return;

		array<int> players = {};
		GetFactionPlayers(factionKey, players);
		foreach (int playerId : players)
			HintPlayer(playerId, title, text);
	}

	//------------------------------------------------------------------------------------------------
	static void HintPlayer(int playerId, string title, string text)
	{
		COA_PlayerRplToOwnerManager owner = COA_PlayerRplToOwnerManager.GetForPlayer(playerId);
		if (owner)
			owner.EndgameShowHint(title, text);
	}

	//------------------------------------------------------------------------------------------------
	//! The decoded order: goes to the leaders of the faction (concept: "the leader receives a
	//! decoded order"), and is sent again to anyone who takes a leader slot later.
	static void SetOrder(string factionKey, string text)
	{
		if (!Replication.IsServer())
			return;

		s_mOrderText.Set(factionKey, text);

		array<int> players = {};
		GetFactionPlayers(factionKey, players);
		foreach (int playerId : players)
		{
			if (IsLeader(playerId))
				SendOrder(playerId, text);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void SendOrder(int playerId, string text)
	{
		COA_PlayerRplToOwnerManager owner = COA_PlayerRplToOwnerManager.GetForPlayer(playerId);
		if (owner)
			owner.EndgameReceiveOrder(text);
	}

	//------------------------------------------------------------------------------------------------
	//! Send a player everything his side currently knows. Called after every spawn, respawn,
	//! reconnect and spectator assignment (modded COA_GamemodeManager.OnPlayerInitialized).
	static void SyncPlayer(int playerId)
	{
		if (!Replication.IsServer())
			return;

		string factionKey = GetFactionKey(playerId);
		if (factionKey.IsEmpty())
			return;

		COA_PlayerRplToOwnerManager owner = COA_PlayerRplToOwnerManager.GetForPlayer(playerId);
		if (!owner)
			return;

		string order;
		if (s_mOrderText.Find(factionKey, order) && IsLeader(playerId))
			SendOrder(playerId, order);
	}

	//------------------------------------------------------------------------------------------------
	protected static map<string, ref GRAD_Endgame_MarkerData> GetMarkers(string factionKey)
	{
		map<string, ref GRAD_Endgame_MarkerData> markers = s_mMarkers.Get(factionKey);
		if (!markers)
		{
			markers = new map<string, ref GRAD_Endgame_MarkerData>();
			s_mMarkers.Set(factionKey, markers);
		}
		return markers;
	}

	//------------------------------------------------------------------------------------------------
	//! Faction key of a player's COALITION slot, or empty.
	static string GetFactionKey(int playerId)
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
	//! Connected players in a slot of this faction, dead or alive (the dead read the map too).
	static void GetFactionPlayers(string factionKey, notnull array<int> players)
	{
		players.Clear();
		array<int> all = {};
		GetGame().GetPlayerManager().GetPlayers(all);
		foreach (int playerId : all)
		{
			if (GetFactionKey(playerId) == factionKey)
				players.Insert(playerId);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Platoon leader, platoon sergeant or squad lead by COALITION slot role.
	static bool IsLeader(int playerId)
	{
		COA_SlottingManager slottingManager = COA_SlottingManager.GetInstance();
		if (!slottingManager)
			return false;

		COA_SlotData slot = slottingManager.GetPlayerSlotData(playerId);
		if (!slot)
			return false;

		COA_EGearRole role = slot.GetSlotRole();
		return role == COA_EGearRole.PLATOON_LEADER || role == COA_EGearRole.PLATOON_SERGEANT || role == COA_EGearRole.SQUAD_LEAD;
	}

	//------------------------------------------------------------------------------------------------
	//! A point within radius of the true position, so an "area" marker does not give the spot away.
	static vector Blur(vector position, float radius)
	{
		if (radius <= 0)
			return position;

		float angle = Math.RandomFloat(0, Math.PI2);
		float distance = Math.RandomFloat(0, radius * 0.7);
		return position + Vector(Math.Cos(angle) * distance, 0, Math.Sin(angle) * distance);
	}
}

//------------------------------------------------------------------------------------------------
//! Re-send each side's information after every spawn, respawn, reconnect and spectator assignment.
//! OnPlayerInitialized is COALITION's documented extension point for mods.
modded class COA_GamemodeManager
{
	override protected void OnPlayerInitialized(int playerId, IEntity playerCharacter, RplComponent playerRplComp, bool isSpectator)
	{
		super.OnPlayerInitialized(playerId, playerCharacter, playerRplComp, isSpectator);
		GRAD_Endgame_InfoService.SyncPlayer(playerId);
	}
}
