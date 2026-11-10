//------------------------------------------------------------------------------------------------
enum GRAD_Endgame_EGarrisonTier
{
	LIGHT,
	MEDIUM,
	HEAVY,
	CUSTOM   //!< use the prefabs set on this component instead of the config's tier list
}

//------------------------------------------------------------------------------------------------
class GRAD_Endgame_GarrisonComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! A garrison at this entity: FIA militia, hostile to both player sides (decided 2026-11-10).
//! The server spawns its groups once and never removes them (no despawning; spawning by script
//! keeps map setup to placing one prefab).
//!
//! What spawns: the group prefabs of the tier (LIGHT, MEDIUM, HEAVY) from the config, or with
//! CUSTOM the prefabs set on this component. Several groups make one garrison, so a heavy garrison
//! can be a rifle squad plus a machine-gun team.
//!
//! When it spawns:
//!  - config m_bGarrisonSpawnOnApproach off (default): at game start, that is when COALITION's
//!    state becomes GAME (or the phase leaves WAITING);
//!  - on: when the first player comes within the spawn radius;
//!  - any time: when something calls SpawnNow().
//! A garrison on a document or radio site that was not chosen for the match never spawns.
//!
//! "Cleared" is reported once, when every group has had members and none is left. FOB capture
//! listens to GetOnCleared().
//!
//! Server only; clients see the AI through normal replication. Spawn pattern from BreakingContact's
//! GRAD_BC_CivilianScoutManager (SCR_AIGroup from a group prefab, optional waypoint).
//!
//! CHANGED WITHOUT WORKBENCH (2026-11-10): not compiled, not run.
class GRAD_Endgame_GarrisonComponent : ScriptComponent
{
	[Attribute("1", UIWidgets.ComboBox, "Size of the garrison. LIGHT, MEDIUM and HEAVY take their group prefabs from the config.", enums: ParamEnumArray.FromEnum(GRAD_Endgame_EGarrisonTier))]
	protected GRAD_Endgame_EGarrisonTier m_eTier;

	[Attribute("", UIWidgets.ResourcePickerThumbnail, "Only with tier CUSTOM: the group prefabs to spawn.", params: "et")]
	protected ref array<ResourceName> m_aCustomGroups;

	[Attribute("", UIWidgets.ResourcePickerThumbnail, "Optional waypoint prefab (for example a defend waypoint), one per group. Without one the groups hold their spawn position.", params: "et")]
	protected ResourceName m_sWaypointPrefab;

	[Attribute("200", UIWidgets.Slider, "A player within this distance (m) spawns the garrison, if spawning on approach is on in the config.", "10 2000 10")]
	protected float m_fSpawnRadius;

	[Attribute("6", desc: "Groups after the first are placed this many metres apart around the entity, so they do not spawn inside each other.")]
	protected float m_fGroupSpacing;

	protected const int CHECK_INTERVAL_MS = 1000;

	protected ref array<SCR_AIGroup> m_aGroups = {};
	protected ref array<AIWaypoint> m_aWaypoints = {};
	protected bool m_bSpawned;      //!< the one spawn has happened (or failed for good)
	protected bool m_bHadAgents;    //!< the groups have had at least one member
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
	bool IsCleared()
	{
		return m_bCleared;
	}

	//------------------------------------------------------------------------------------------------
	GRAD_Endgame_EGarrisonTier GetTier()
	{
		return m_eTier;
	}

	//------------------------------------------------------------------------------------------------
	//! The group prefabs this garrison spawns.
	void GetGroupPrefabs(notnull array<ResourceName> prefabs)
	{
		prefabs.Clear();

		if (m_eTier == GRAD_Endgame_EGarrisonTier.CUSTOM)
		{
			if (m_aCustomGroups)
				prefabs.InsertAll(m_aCustomGroups);
			return;
		}

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (cfg)
			cfg.GetGarrisonGroups(m_eTier, prefabs);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		GRAD_Endgame_Terrain.Snap(owner);
		if (!Replication.IsServer())
			return;

		GetGame().GetCallqueue().CallLater(Check, CHECK_INTERVAL_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(Check);
		RemoveGroups();
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
		if (site && !site.IsSiteActive())
			return false;

		GRAD_Endgame_RadioSiteComponent radio = GRAD_Endgame_RadioSiteComponent.Cast(GetOwner().FindComponent(GRAD_Endgame_RadioSiteComponent));
		if (radio)
		{
			if (!radio.IsSiteActive())
				return false;
			GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
			if (cfg && !cfg.GetMilitiaAtRadioSites())
				return false;
		}
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Once per second on the server: spawn when due, then watch for "cleared".
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

		if (m_aGroups.IsEmpty())
		{
			// The spawn failed: nothing to watch.
			GetGame().GetCallqueue().Remove(Check);
			return;
		}

		// Groups may create their soldiers over several frames, so "no members" only counts as
		// cleared after the garrison has had members.
		int agents;
		foreach (SCR_AIGroup group : m_aGroups)
		{
			if (group)
				agents += group.GetAgentsCount();
		}

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
		s_iActiveGroups -= m_aGroups.Count();
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

		array<ResourceName> prefabs = {};
		GetGroupPrefabs(prefabs);
		if (prefabs.IsEmpty())
		{
			GRAD_Endgame_MatchLog.Error(string.Format("Garrison at %1 has no group prefabs for tier %2 (config or component).", GetOwner().GetOrigin(), SCR_Enum.GetEnumName(GRAD_Endgame_EGarrisonTier, m_eTier)));
			return;
		}

		vector transform[4];
		GetOwner().GetWorldTransform(transform);
		vector center = transform[3];

		for (int i = 0; i < prefabs.Count(); i++)
		{
			EntitySpawnParams params = new EntitySpawnParams();
			params.TransformMode = ETransformMode.WORLD;
			params.Transform[0] = transform[0];
			params.Transform[1] = transform[1];
			params.Transform[2] = transform[2];
			params.Transform[3] = center;

			// First group on the entity, the others on a ring around it.
			if (i > 0)
			{
				float angle = Math.PI2 * (i - 1) / Math.Max(prefabs.Count() - 1, 1);
				params.Transform[3] = center + Vector(Math.Cos(angle) * m_fGroupSpacing, 0, Math.Sin(angle) * m_fGroupSpacing);
			}

			SpawnGroup(prefabs[i], params);
		}

		s_iActiveGroups += m_aGroups.Count();
		foreach (SCR_AIGroup group : m_aGroups)
			m_iLastAgentCount += group.GetAgentsCount();
		s_iActiveAgents += m_iLastAgentCount;
		if (m_iLastAgentCount > 0)
			m_bHadAgents = true;

		GRAD_Endgame_MatchLog.Write("garrison_spawn", "", 0, center, string.Format("tier %1, groups %2, agents at spawn %3, groups active %4", SCR_Enum.GetEnumName(GRAD_Endgame_EGarrisonTier, m_eTier), m_aGroups.Count(), m_iLastAgentCount, s_iActiveGroups));
	}

	//------------------------------------------------------------------------------------------------
	protected void SpawnGroup(ResourceName prefab, EntitySpawnParams params)
	{
		IEntity groupEntity = GetGame().SpawnEntityPrefab(Resource.Load(prefab), GetGame().GetWorld(), params);
		SCR_AIGroup group = SCR_AIGroup.Cast(groupEntity);
		if (!group)
		{
			string got = "nothing spawned (prefab not found?)";
			if (groupEntity)
				got = "spawned a " + groupEntity.ClassName();
			GRAD_Endgame_MatchLog.Error(string.Format("Garrison: group prefab is not an SCR_AIGroup: %1 (%2)", prefab, got));
			if (groupEntity)
				SCR_EntityHelper.DeleteEntityAndChildren(groupEntity);
			return;
		}

		// Keep the empty group entity, so "cleared" can be read from it.
		group.SetDeleteWhenEmpty(false);
		m_aGroups.Insert(group);

		if (m_sWaypointPrefab.IsEmpty())
			return;

		AIWaypoint waypoint = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(Resource.Load(m_sWaypointPrefab), GetGame().GetWorld(), params));
		if (waypoint)
		{
			group.AddWaypoint(waypoint);
			m_aWaypoints.Insert(waypoint);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Only for OnDelete (world shutdown, entity removed in the editor). Not used during a match.
	protected void RemoveGroups()
	{
		foreach (AIWaypoint waypoint : m_aWaypoints)
		{
			if (waypoint)
				SCR_EntityHelper.DeleteEntityAndChildren(waypoint);
		}
		m_aWaypoints.Clear();

		foreach (SCR_AIGroup group : m_aGroups)
		{
			if (!group)
				continue;

			// Deleting a group alone leaves its soldiers behind, so remove them first.
			array<AIAgent> agents = {};
			group.GetAgents(agents);
			foreach (AIAgent agent : agents)
			{
				if (agent && agent.GetControlledEntity())
					SCR_EntityHelper.DeleteEntityAndChildren(agent.GetControlledEntity());
			}
			SCR_EntityHelper.DeleteEntityAndChildren(group);
		}

		if (!m_bCleared)
			s_iActiveGroups -= m_aGroups.Count();
		m_aGroups.Clear();

		s_iActiveAgents -= m_iLastAgentCount;
		m_iLastAgentCount = 0;
	}
}
