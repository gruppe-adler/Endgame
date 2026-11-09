//------------------------------------------------------------------------------------------------
class GRAD_Endgame_GarrisonComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! T7 spike: a garrison at this entity. The server spawns an AI group when a player comes within
//! SpawnRadius and removes it again when nobody is within DespawnRadius or the group is dead.
//! Server only; clients see the AI through normal replication.
//!
//! Pattern for spawning is BreakingContact's GRAD_BC_CivilianScoutManager (SCR_AIGroup from a
//! group prefab, optional waypoint). Spawned groups are counted in s_iActiveGroups so the frame
//! cost of many garrisons at once can be judged (log line on every spawn and despawn).
class GRAD_Endgame_GarrisonComponent : ScriptComponent
{
	[Attribute("{E3545CFEDD312D54}Prefabs/Groups/OPFOR/Group_USSR_RifleSquad_p.et", UIWidgets.ResourcePickerThumbnail, "AI group prefab that forms the garrison.", params: "et")]
	protected ResourceName m_sGroupPrefab;

	[Attribute("", UIWidgets.ResourcePickerThumbnail, "Optional waypoint prefab (for example a defend waypoint). Without one the group holds its spawn position.", params: "et")]
	protected ResourceName m_sWaypointPrefab;

	[Attribute("200", UIWidgets.Slider, "A player within this distance (m) spawns the garrison.", "10 2000 10")]
	protected float m_fSpawnRadius;

	[Attribute("300", UIWidgets.Slider, "No player within this distance (m) removes the garrison. Keep above the spawn radius.", "10 3000 10")]
	protected float m_fDespawnRadius;

	protected const int CHECK_INTERVAL_MS = 1000;

	protected SCR_AIGroup m_Group;
	protected AIWaypoint m_Waypoint;

	//! Number of garrison groups alive on the server, for the frame cost test.
	static int s_iActiveGroups;

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
		Despawn();
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
	protected void Check()
	{
		float nearest = NearestPlayerDistance();

		if (!m_Group)
		{
			if (nearest <= m_fSpawnRadius)
				Spawn();
			return;
		}

		bool cleared = m_Group.GetAgentsCount() == 0;
		if (cleared || nearest > m_fDespawnRadius)
		{
			string reason = "player left";
			if (cleared)
				reason = "cleared";
			Print(string.Format("[GRAD_Endgame] Garrison %1 despawned (%2)", GetOwner().GetOrigin().ToString(), reason), LogLevel.NORMAL);
			Despawn();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void Spawn()
	{
		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		GetOwner().GetWorldTransform(params.Transform);

		IEntity groupEntity = GetGame().SpawnEntityPrefab(Resource.Load(m_sGroupPrefab), GetGame().GetWorld(), params);
		m_Group = SCR_AIGroup.Cast(groupEntity);
		if (!m_Group)
		{
			Print("[GRAD_Endgame] Garrison: group prefab is not an SCR_AIGroup: " + m_sGroupPrefab, LogLevel.ERROR);
			if (groupEntity)
				SCR_EntityHelper.DeleteEntityAndChildren(groupEntity);
			return;
		}

		m_Group.SetDeleteWhenEmpty(false);

		if (!m_sWaypointPrefab.IsEmpty())
		{
			m_Waypoint = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(Resource.Load(m_sWaypointPrefab), GetGame().GetWorld(), params));
			if (m_Waypoint)
				m_Group.AddWaypoint(m_Waypoint);
		}

		s_iActiveGroups++;
		Print(string.Format("[GRAD_Endgame] Garrison %1 spawned (%2 agents), %3 groups active", GetOwner().GetOrigin().ToString(), m_Group.GetAgentsCount(), s_iActiveGroups), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void Despawn()
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
		s_iActiveGroups--;
	}
}
