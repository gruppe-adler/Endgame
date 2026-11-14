//------------------------------------------------------------------------------------------------
//! Motorised FIA patrols: a few vehicles with an FIA crew drive from document site to document
//! site, round and round, for the whole match. They fight anyone (FIA is hostile to both sides).
//!
//! Server only. Started when phase 1 begins, from the game-mode component; stopped when the match
//! ends. The vehicle, the crew group and the move waypoint come from the config.
//!
//! Crew: the group prefab spawns its soldiers; they are put into the vehicle's seats (driver,
//! gunner, cargo). Soldiers without a seat are removed. The route is a loop of Move waypoints, one
//! at a time: when the group has no waypoint left, it gets the next site.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-14): not compiled, not run. Vehicles cannot drive until the
//! world has a vehicle navmesh (see docs/current-state.md, open problem 1).
class GRAD_Endgame_FiaPatrol : Managed
{
	protected const int BOARD_DELAY_MS = 2500;      //!< the crew soldiers arrive a moment after their group
	protected const int ROUTE_CHECK_MS = 5000;
	protected const float SPAWN_OFFSET = 10;        //!< metres from the first site, so the vehicle is not inside it

	protected Vehicle m_Vehicle;
	protected SCR_AIGroup m_Group;
	protected ref array<vector> m_aRoute = {};
	protected int m_iNext;
	protected ResourceName m_sWaypointPrefab;
	protected float m_fRadius;
	protected AIWaypoint m_Waypoint;
	protected int m_iIndex;

	//------------------------------------------------------------------------------------------------
	//! Server: spawn the vehicle and its crew near the first stop and start the route.
	bool Start(int index, notnull array<vector> route, int firstStop, ResourceName vehiclePrefab, ResourceName crewPrefab, ResourceName waypointPrefab, float radius)
	{
		m_iIndex = index;
		m_aRoute.Copy(route);
		m_iNext = firstStop;
		m_sWaypointPrefab = waypointPrefab;
		m_fRadius = radius;

		vector start = route[firstStop] + Vector(SPAWN_OFFSET, 0, 0);
		start[1] = GetGame().GetWorld().GetSurfaceY(start[0], start[2]);

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.MatrixIdentity4(params.Transform);
		params.Transform[3] = start;

		Resource vehicleResource = Resource.Load(vehiclePrefab);
		if (!vehicleResource || !vehicleResource.IsValid())
		{
			GRAD_Endgame_MatchLog.Error("FIA patrol: vehicle prefab does not load: " + vehiclePrefab);
			return false;
		}
		m_Vehicle = Vehicle.Cast(GetGame().SpawnEntityPrefab(vehicleResource, GetGame().GetWorld(), params));
		if (!m_Vehicle)
		{
			GRAD_Endgame_MatchLog.Error("FIA patrol: the vehicle prefab is not a Vehicle: " + vehiclePrefab);
			return false;
		}

		Resource crewResource = Resource.Load(crewPrefab);
		if (!crewResource || !crewResource.IsValid())
		{
			GRAD_Endgame_MatchLog.Error("FIA patrol: crew group prefab does not load: " + crewPrefab);
			return false;
		}
		m_Group = SCR_AIGroup.Cast(GetGame().SpawnEntityPrefab(crewResource, GetGame().GetWorld(), params));
		if (!m_Group)
		{
			GRAD_Endgame_MatchLog.Error("FIA patrol: the crew prefab is not an SCR_AIGroup: " + crewPrefab);
			return false;
		}
		m_Group.SetDeleteWhenEmpty(false);

		GRAD_Endgame_ReplayManager replayManager = GRAD_Endgame_ReplayManager.GetInstance();
		if (replayManager)
			replayManager.RegisterTrackedVehicle(m_Vehicle);

		GetGame().GetCallqueue().CallLater(BoardCrew, BOARD_DELAY_MS, false);
		GRAD_Endgame_MatchLog.Write("fia_patrol", "", 0, start, string.Format("patrol %1 spawned, %2 stops", index, route.Count()));
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Put the group's soldiers into the vehicle's seats, then start driving.
	protected void BoardCrew()
	{
		if (!m_Vehicle || !m_Group)
			return;

		SCR_BaseCompartmentManagerComponent compartments = SCR_BaseCompartmentManagerComponent.Cast(m_Vehicle.FindComponent(SCR_BaseCompartmentManagerComponent));
		if (!compartments)
		{
			GRAD_Endgame_MatchLog.Error("FIA patrol: the vehicle has no compartment manager.");
			return;
		}

		array<ECompartmentType> types = {ECompartmentType.PILOT, ECompartmentType.TURRET, ECompartmentType.CARGO};
		array<BaseCompartmentSlot> slots = {};
		compartments.GetCompartmentsOfTypes(slots, types);

		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);

		int seated;
		for (int i = 0; i < agents.Count(); i++)
		{
			SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(agents[i].GetControlledEntity());
			if (!character)
				continue;

			if (i >= slots.Count())
			{
				// no seat left for this one
				m_Group.RemoveAgent(agents[i]);
				SCR_EntityHelper.DeleteEntityAndChildren(character);
				continue;
			}

			SCR_CompartmentAccessComponent access = SCR_CompartmentAccessComponent.Cast(character.GetCompartmentAccessComponent());
			if (!access)
				continue;

			access.MoveInVehicle(m_Vehicle, slots[i].GetType(), true, slots[i]);
			seated++;
		}

		GRAD_Endgame_MatchLog.Write("fia_patrol", "", 0, m_Vehicle.GetOrigin(), string.Format("patrol %1 crew seated %2 of %3 soldiers", m_iIndex, seated, agents.Count()));

		GetGame().GetCallqueue().CallLater(CheckRoute, ROUTE_CHECK_MS, true);
		CheckRoute();
	}

	//------------------------------------------------------------------------------------------------
	//! Once the group has no waypoint left, send it to the next stop.
	protected void CheckRoute()
	{
		if (!m_Group || m_Group.GetAgentsCount() == 0)
			return;
		if (m_Group.GetCurrentWaypoint())
			return;

		if (m_Waypoint)
			SCR_EntityHelper.DeleteEntityAndChildren(m_Waypoint);
		m_Waypoint = null;

		vector stop = m_aRoute[m_iNext];
		m_iNext = (m_iNext + 1) % m_aRoute.Count();

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.MatrixIdentity4(params.Transform);
		params.Transform[3] = stop;

		Resource resource = Resource.Load(m_sWaypointPrefab);
		if (!resource || !resource.IsValid())
		{
			GRAD_Endgame_MatchLog.Error("FIA patrol: waypoint prefab does not load: " + m_sWaypointPrefab);
			GetGame().GetCallqueue().Remove(CheckRoute);
			return;
		}

		m_Waypoint = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params));
		if (!m_Waypoint)
		{
			GRAD_Endgame_MatchLog.Error("FIA patrol: the waypoint prefab is not an AIWaypoint: " + m_sWaypointPrefab);
			GetGame().GetCallqueue().Remove(CheckRoute);
			return;
		}

		m_Waypoint.SetCompletionRadius(m_fRadius);
		m_Group.AddWaypoint(m_Waypoint);
		GRAD_Endgame_MatchLog.Debug(string.Format("FIA patrol %1 heads for %2", m_iIndex, stop));
	}

	//------------------------------------------------------------------------------------------------
	//! Remove the vehicle, its crew and the waypoint.
	void Stop()
	{
		GetGame().GetCallqueue().Remove(BoardCrew);
		GetGame().GetCallqueue().Remove(CheckRoute);

		if (m_Waypoint)
			SCR_EntityHelper.DeleteEntityAndChildren(m_Waypoint);

		if (m_Group)
		{
			array<AIAgent> agents = {};
			m_Group.GetAgents(agents);
			foreach (AIAgent agent : agents)
			{
				if (agent && agent.GetControlledEntity())
					SCR_EntityHelper.DeleteEntityAndChildren(agent.GetControlledEntity());
			}
			SCR_EntityHelper.DeleteEntityAndChildren(m_Group);
		}

		if (m_Vehicle)
			SCR_EntityHelper.DeleteEntityAndChildren(m_Vehicle);
	}
}

//------------------------------------------------------------------------------------------------
//! Starts and stops the patrols of a match.
class GRAD_Endgame_FiaPatrols
{
	protected static ref array<ref GRAD_Endgame_FiaPatrol> s_aPatrols = {};

	//------------------------------------------------------------------------------------------------
	//! Server: start the configured patrols over the document sites chosen for this match.
	static void Start(notnull GRAD_Endgame_Config cfg)
	{
		if (!Replication.IsServer())
			return;

		Stop();

		int count = cfg.GetFiaPatrolCount();
		if (count <= 0 || cfg.GetFiaPatrolVehicle().IsEmpty() || cfg.GetFiaPatrolCrewGroup().IsEmpty())
			return;

		array<vector> route = {};
		foreach (GRAD_Endgame_DocumentSiteComponent site : GRAD_Endgame_DocumentSiteComponent.GetAll())
		{
			if (site.IsSiteActive())
				route.Insert(site.GetOwner().GetOrigin());
		}

		if (route.IsEmpty())
		{
			GRAD_Endgame_MatchLog.Error("FIA patrols: no document sites to patrol.");
			return;
		}

		// Spread the patrols over the route: each starts at a different stop.
		for (int i = 0; i < count; i++)
		{
			int firstStop = (i * route.Count()) / count;
			GRAD_Endgame_FiaPatrol patrol = new GRAD_Endgame_FiaPatrol();
			if (patrol.Start(i, route, firstStop, cfg.GetFiaPatrolVehicle(), cfg.GetFiaPatrolCrewGroup(), cfg.GetFiaPatrolWaypoint(), cfg.GetFiaPatrolRadius()))
				s_aPatrols.Insert(patrol);
			else
				patrol.Stop();
		}
	}

	//------------------------------------------------------------------------------------------------
	static void Stop()
	{
		foreach (GRAD_Endgame_FiaPatrol patrol : s_aPatrols)
			patrol.Stop();
		s_aPatrols.Clear();
	}
}
