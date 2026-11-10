//------------------------------------------------------------------------------------------------
class GRAD_Endgame_GarrisonComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! A garrison at this entity. The server spawns ONE AI group, once, and never removes it again
//! (decided 2026-11-10: no despawning, the engine carries a round's worth of AI; spawning by
//! script keeps map setup to placing this prefab).
//!
//! When it spawns:
//!  - config m_bGarrisonSpawnOnApproach off (default, decided 2026-11-10): at game start, that is
//!    when COALITION's state becomes GAME (or the phase leaves WAITING);
//!  - on: when the first player comes within the spawn radius;
//!  - any time: when something calls SpawnNow().
//! A garrison on a document site that was not chosen for the match never spawns.
//!
//! "Cleared" is reported once, when the group has had members and has none left. FOB capture (M2)
//! and the document sites (M4) listen to GetOnCleared().
//!
//! Server only; clients see the AI through normal replication. The spawn pattern is
//! BreakingContact's GRAD_BC_CivilianScoutManager (SCR_AIGroup from a group prefab, optional
//! waypoint).
//!
//! CHANGED WITHOUT WORKBENCH (2026-11-10): not compiled, not run.
class GRAD_Endgame_GarrisonComponent : ScriptComponent
{
	[Attribute("{E3545CFEDD312D54}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad_p.et", UIWidgets.ResourcePickerThumbnail, "AI group prefab that forms the garrison. TODO: a militia group, chosen by tier from the config.", params: "et")]
	protected ResourceName m_sGroupPrefab;

	[Attribute("", UIWidgets.ResourcePickerThumbnail, "Optional waypoint prefab (for example a defend waypoint). Without one the group holds its spawn position.", params: "et")]
	protected ResourceName m_sWaypointPrefab;

	[Attribute("200", UIWidgets.Slider, "A player within this distance (m) spawns the garrison, if spawning on approach is on in the config.", "10 2000 10")]
	protected float m_fSpawnRadius;

	protected const int CHECK_INTERVAL_MS = 1000;

	protected SCR_AIGroup m_Group;
	protected AIWaypoint m_Waypoint;
	protected bool m_bSpawned;      //!< the one spawn has happened (or failed for good)
	protected bool m_bHadAgents;    //!< the group has had at least one member
	protected bool m_bCleared;
	protected int m_iLastAgentCount;

	protected ref ScriptInvoker m_OnCleared = new ScriptInvoker();   //!< (GRAD_Endgame_GarrisonComponent garrison)

	//! Garrison groups and soldiers alive on the server, for the AI budget (match log: server_load).
	static int s_iActiveGroups;
	static int s_iActiveAgents;

	//------------------------------------------------------------------------------------------------
	//! Server: fired once when the garrison has been wiped out.
	ScriptInvoker GetOnCleared()
	{
		return m_OnCleared;
	}

	//------------------------------------------------------------------------------------------------
	bool IsSpawned()
	{
		return m_bSpawned;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: true once the garrison existed and has no member left.
	bool IsCleared()
	{
		return m_bCleared;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!Replication.IsServer())
			return;

		GetGame().GetCallqueue().CallLater(Check, CHECK_INTERVAL_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(Check);
		RemoveGroup();
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected float NearestPlayerDistance()
	{
		vector origin = GetOwner().GetOrigin();
		float best = float.MAX;

		array<int> players = {};
		GetGame().GetPlayerManager().GetPlayers(players);
		foreach (int playerId : players)
		{
			IEntity entity = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
			if (!entity)
				continue;
			float distance = vector.Distance(origin, entity.GetOrigin());
			if (distance < best)
				best = distance;
		}
		return best;
	}

	//------------------------------------------------------------------------------------------------
	//! True once the match runs: the phase has left WAITING, or (without the game-mode component)
	//! COALITION is in its GAME state. Site selection happens at that moment too, so a garrison
	//! never spawns at a site that is about to be switched off.
	protected bool IsGameRunning()
	{
		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (gameMode)
			return gameMode.GetPhase() != GRAD_Endgame_EPhase.WAITING;

		COA_Gamemode coalition = COA_Gamemode.GetInstance();
		return coalition && coalition.m_GamemodeState == COA_EGamemodeState.GAME;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsOwnerSiteActive()
	{
		GRAD_Endgame_DocumentSiteComponent site = GRAD_Endgame_DocumentSiteComponent.Cast(GetOwner().FindComponent(GRAD_Endgame_DocumentSiteComponent));
		return !site || site.IsActive();
	}

	//------------------------------------------------------------------------------------------------
	//! Once per second on the server: spawn on approach, then watch for "cleared".
	protected void Check()
	{
		if (!m_bSpawned)
		{
			if (!IsGameRunning() || !IsOwnerSiteActive())
				return;

			GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
			bool onApproach = cfg && cfg.GetGarrisonSpawnOnApproach();
			if (!onApproach || NearestPlayerDistance() <= m_fSpawnRadius)
				SpawnNow();
			return;
		}

		if (!m_Group)
		{
			// The spawn failed, or the group entity is gone: nothing left to watch.
			GetGame().GetCallqueue().Remove(Check);
			return;
		}

		// The group may create its soldiers over several frames, so an empty group only means
		// "cleared" after it has had members (finding G2).
		int agents = m_Group.GetAgentsCount();
		s_iActiveAgents += agents - m_iLastAgentCount;
		m_iLastAgentCount = agents;

		if (agents > 0)
		{
			m_bHadAgents = true;
			return;
		}

		if (!m_bHadAgents)
			return;

		m_bCleared = true;
		s_iActiveGroups--;
		GetGame().GetCallqueue().Remove(Check);
		GRAD_Endgame_MatchLog.Write("garrison_cleared", "", 0, GetOwner().GetOrigin(), string.Format("groups left %1, agents left %2", s_iActiveGroups, s_iActiveAgents));
		m_OnCleared.Invoke(this);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: spawn the garrison now. Does nothing if it has already been spawned.
	void SpawnNow()
	{
		if (!Replication.IsServer() || m_bSpawned || !IsOwnerSiteActive())
			return;

		m_bSpawned = true;

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		GetOwner().GetWorldTransform(params.Transform);

		IEntity groupEntity = GetGame().SpawnEntityPrefab(Resource.Load(m_sGroupPrefab), GetGame().GetWorld(), params);
		m_Group = SCR_AIGroup.Cast(groupEntity);
		if (!m_Group)
		{
			GRAD_Endgame_MatchLog.Error("Garrison: group prefab is not an SCR_AIGroup: " + m_sGroupPrefab);
			if (groupEntity)
				SCR_EntityHelper.DeleteEntityAndChildren(groupEntity);
			return;
		}

		// Keep the empty group entity, so "cleared" can be read from it.
		m_Group.SetDeleteWhenEmpty(false);

		if (!m_sWaypointPrefab.IsEmpty())
		{
			m_Waypoint = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(Resource.Load(m_sWaypointPrefab), GetGame().GetWorld(), params));
			if (m_Waypoint)
				m_Group.AddWaypoint(m_Waypoint);
		}

		s_iActiveGroups++;
		m_iLastAgentCount = m_Group.GetAgentsCount();
		s_iActiveAgents += m_iLastAgentCount;
		if (m_iLastAgentCount > 0)
			m_bHadAgents = true;

		GRAD_Endgame_MatchLog.Write("garrison_spawn", "", 0, GetOwner().GetOrigin(), string.Format("agents at spawn %1, groups active %2", m_iLastAgentCount, s_iActiveGroups));
	}

	//------------------------------------------------------------------------------------------------
	//! Only for OnDelete (world shutdown, entity removed in the editor). Not used during a match.
	protected void RemoveGroup()
	{
		if (m_Waypoint)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(m_Waypoint);
			m_Waypoint = null;
		}

		if (!m_Group)
			return;

		// Deleting the group alone leaves its soldiers behind, so remove them first.
		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			if (agent && agent.GetControlledEntity())
				SCR_EntityHelper.DeleteEntityAndChildren(agent.GetControlledEntity());
		}

		SCR_EntityHelper.DeleteEntityAndChildren(m_Group);
		m_Group = null;

		s_iActiveAgents -= m_iLastAgentCount;
		m_iLastAgentCount = 0;
		if (!m_bCleared)
			s_iActiveGroups--;
	}
}
