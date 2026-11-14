//------------------------------------------------------------------------------------------------
//! Motorised FIA patrols: a few vehicles with an FIA crew drive from document site to document
//! site, round and round, for the whole match. They fight anyone (FIA is hostile to both sides).
//!
//! Server only. Started when phase 1 begins, from the game-mode component; stopped when the match
//! ends. The vehicle, the crew group and the move waypoint come from the config.
//!
//! At each document site the crew gets out, walks to the site to check it, walks back, gets in and
//! drives on (states DRIVE, DISMOUNT, CHECK, RETURN). Getting out and in are vanilla waypoints
//! (GetOut, GetInNearest). While the crew is out the vehicle is locked for every player
//! (SCR_VehicleSpawnProtectionComponent, owner -2 = nobody); it is unlocked before the crew boards.
//! The first boarding at spawn and stragglers are put in by script (MoveInVehicle).
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
	protected const float ROAD_SEARCH_MIN = 60;     //!< metres: first search square around the first stop
	protected const float ROAD_SEARCH_MAX = 400;    //!< widest search
	protected const float SPAWN_OFFSET = 10;        //!< metres from the first site, so the vehicle is not inside it
	protected const int BOARD_ROUNDS = 5;           //!< the soldiers of a group appear one after the other: board again
	protected const float NEAR_STOP_METRES = 60;    //!< a vehicle cannot enter a building: this close counts as arrived
	protected const int STATE_DRIVE = 0;
	protected const int STATE_DISMOUNT = 1;
	protected const int STATE_CHECK = 2;
	protected const int STATE_RETURN = 3;
	protected const float DISMOUNT_SECONDS = 4;     //!< at least this long to climb out
	protected const float DISMOUNT_MAX_SECONDS = 25; //!< at most, if the waypoint never ends
	protected const float CHECK_MIN_SECONDS = 20;   //!< at least this long on foot at the site
	protected const float CHECK_MAX_SECONDS = 60;   //!< and at most, if they never get there
	protected const float FOOT_RADIUS = 1.5;        //!< completion radius of the walk to the documents
	protected const float AT_SITE_METRES = 3;
	protected const float RETURN_MAX_SECONDS = 90;
	protected const float AT_VEHICLE_METRES = 10;
	protected const float STOP_TIMEOUT_BASE_S = 120;   //!< a stop not reached in this time plus the distance allowance is skipped
	protected const float STOP_TIMEOUT_PER_M = 0.4;    //!< seconds allowed per metre of the leg (about 9 km/h: slow but sure)
	protected const float PARKED_SECONDS = 10;      //!< stood still this long ...
	protected const float PARKED_NEAR_METRES = 150; //!< ... within this of the stop: it is as close as the road gets, get out
	protected const float STUCK_SECONDS = 90;       //!< stood still this long far from the stop: stuck, give the stop up
	protected const float STILL_SPEED = 0.5;        //!< m/s

	protected Vehicle m_Vehicle;
	protected SCR_AIGroup m_Group;
	protected ref array<vector> m_aRoute = {};
	protected int m_iNext;
	protected ResourceName m_sWaypointPrefab;
	protected ResourceName m_sGetOutPrefab;
	protected ResourceName m_sGetInPrefab;
	protected float m_fRadius;
	protected AIWaypoint m_Waypoint;
	protected int m_iIndex;
	protected int m_iBoardRound;
	protected int m_iState;
	protected float m_fStateAt;         //!< world time (s) the state began
	protected bool m_bFirstStop = true; //!< the first stop is the spawn spot: no dismount there
	protected vector m_vStop;           //!< the stop the vehicle is driving to
	protected float m_fStillSince = -1;   //!< world time (s) the vehicle stopped moving, -1 while it moves
	protected float m_fLegTimeout;        //!< seconds allowed for the current leg
	protected bool m_bVehicleLost;        //!< destroyed: the crew goes on on foot
	protected float m_fAssignedAt;      //!< world time (s) when it got that stop

	//------------------------------------------------------------------------------------------------
	//! Server: spawn the vehicle and its crew near the first stop and start the route.
	bool Start(int index, notnull array<vector> route, int firstStop, ResourceName vehiclePrefab, ResourceName crewPrefab, ResourceName waypointPrefab, ResourceName getOutPrefab, ResourceName getInPrefab, float radius)
	{
		m_sGetOutPrefab = getOutPrefab;
		m_sGetInPrefab = getInPrefab;
		m_iIndex = index;
		m_aRoute.Copy(route);
		m_iNext = firstStop;
		m_sWaypointPrefab = waypointPrefab;
		m_fRadius = radius;

		// On the road nearest to the first stop, facing along it. Without a road in reach the vehicle
		// stands beside the stop, as before (logged, so a bad spot is easy to find).
		vector start;
		vector roadDirection;
		bool onRoad = FindRoadSpot(route[firstStop], start, roadDirection);
		if (!onRoad)
		{
			start = route[firstStop] + Vector(SPAWN_OFFSET, 0, 0);
			start[1] = GetGame().GetWorld().GetSurfaceY(start[0], start[2]);
			GRAD_Endgame_MatchLog.Write("fia_patrol", "", 0, route[firstStop], string.Format("patrol %1: no road within %2 m of the first stop, spawns beside it", index, ROAD_SEARCH_MAX));
		}

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.MatrixIdentity4(params.Transform);
		if (onRoad)
		{
			vector angles = roadDirection.VectorToAngles();
			Math3D.AnglesToMatrix(Vector(angles[0], 0, 0), params.Transform);
		}
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
	//! The point of the nearest road to a position, and the direction of the road there. Searches
	//! in growing squares up to ROAD_SEARCH_MAX metres. False if there is no road (or no road
	//! network) in reach.
	protected bool FindRoadSpot(vector position, out vector spot, out vector direction)
	{
		SCR_AIWorld aiWorld = SCR_AIWorld.Cast(GetGame().GetAIWorld());
		if (!aiWorld)
			return false;
		RoadNetworkManager roadManager = aiWorld.GetRoadNetworkManager();
		if (!roadManager)
			return false;

		array<float> radii = {ROAD_SEARCH_MIN, ROAD_SEARCH_MIN * 3, ROAD_SEARCH_MAX};
		foreach (float radius : radii)
		{
			array<BaseRoad> roads = {};
			roadManager.GetRoadsInAABB(position - Vector(radius, radius, radius), position + Vector(radius, radius, radius), roads);
			if (roads.IsEmpty())
				continue;

			// the closest road point of all roads found
			float bestDistance = float.MAX;
			foreach (BaseRoad road : roads)
			{
				array<vector> points = {};
				road.GetPoints(points);
				if (points.Count() < 2)
					continue;

				for (int i = 0; i < points.Count(); i++)
				{
					float distance = vector.DistanceXZ(position, points[i]);
					if (distance >= bestDistance)
						continue;

					bestDistance = distance;
					spot = points[i];
					if (i < points.Count() - 1)
						direction = vector.Direction(points[i], points[i + 1]);
					else
						direction = vector.Direction(points[i - 1], points[i]);
				}
			}

			if (bestDistance < float.MAX)
			{
				direction.Normalize();
				return true;
			}
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Put the group's soldiers into the vehicle's free seats. The soldiers of a group do not all
	//! exist at once, so this runs several times; the route starts after the first round.
	protected void BoardCrew()
	{
		if (!m_Vehicle || !m_Group)
			return;

		int seated, removed, size;
		SeatCrew(seated, removed, size);

		GRAD_Endgame_MatchLog.Write("fia_patrol", "", 0, m_Vehicle.GetOrigin(), string.Format("patrol %1 boarding round %2: seated %3, no seat %4, group size %5", m_iIndex, m_iBoardRound + 1, seated, removed, size));

		m_iBoardRound++;
		if (m_iBoardRound == 1)
		{
			GetGame().GetCallqueue().CallLater(CheckRoute, ROUTE_CHECK_MS, true);
			CheckRoute();
		}
		if (m_iBoardRound < BOARD_ROUNDS)
			GetGame().GetCallqueue().CallLater(BoardCrew, BOARD_DELAY_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Move every soldier who is not in a vehicle into a free seat; soldiers with no seat are removed.
	protected void SeatCrew(out int seated, out int removed, out int size)
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

		size = agents.Count();
		foreach (AIAgent agent : agents)
		{
			SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(agent.GetControlledEntity());
			if (!character || character.IsInVehicle())
				continue;

			BaseCompartmentSlot freeSlot;
			foreach (BaseCompartmentSlot slot : slots)
			{
				if (!slot.GetOccupant())
				{
					freeSlot = slot;
					break;
				}
			}

			if (!freeSlot)
			{
				// no seat left for this one
				m_Group.RemoveAgent(agent);
				SCR_EntityHelper.DeleteEntityAndChildren(character);
				removed++;
				continue;
			}

			SCR_CompartmentAccessComponent access = SCR_CompartmentAccessComponent.Cast(character.GetCompartmentAccessComponent());
			if (!access)
				continue;

			access.MoveInVehicle(m_Vehicle, freeSlot.GetType(), true, freeSlot);
			seated++;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! True once the vehicle is gone or wrecked.
	protected bool IsVehicleLost()
	{
		if (!m_Vehicle)
			return true;

		DamageManagerComponent damage = DamageManagerComponent.Cast(m_Vehicle.FindComponent(DamageManagerComponent));
		return damage && damage.GetState() == EDamageState.DESTROYED;
	}

	//------------------------------------------------------------------------------------------------
	protected float GetVehicleSpeed()
	{
		if (!m_Vehicle)
			return 0;

		Physics physics = m_Vehicle.GetPhysics();
		if (!physics)
			return 0;

		return physics.GetVelocity().Length();
	}

	//------------------------------------------------------------------------------------------------
	protected float Now()
	{
		return GetGame().GetWorld().GetWorldTime() * 0.001;
	}

	//------------------------------------------------------------------------------------------------
	//! Every few seconds: drive to the stop; at the stop get out, check the site on foot, come back,
	//! get in, drive to the next.
	protected void CheckRoute()
	{
		if (!m_Group || m_Group.GetAgentsCount() == 0)
			return;

		float now = Now();
		float inState = now - m_fStateAt;

		// A wrecked vehicle ends the driving: the survivors walk the route (no dismount, no boarding).
		if (!m_bVehicleLost && IsVehicleLost())
		{
			m_bVehicleLost = true;
			GRAD_Endgame_MatchLog.Write("fia_patrol", "", 0, vector.Zero, string.Format("patrol %1: vehicle destroyed, the crew goes on on foot", m_iIndex));
			RemoveCurrentWaypoint();
			DriveToNextStop(now);
			return;
		}
		if (m_bVehicleLost)
		{
			// on foot: next stop when the group has none or got there
			if (!m_Group.GetCurrentWaypoint() || now - m_fAssignedAt > STOP_TIMEOUT_BASE_S + m_fLegTimeout)
			{
				RemoveCurrentWaypoint();
				DriveToNextStop(now);
			}
			return;
		}

		switch (m_iState)
		{
			case STATE_DRIVE:
			{
				if (m_Group.GetCurrentWaypoint())
				{
					// Still driving. A vehicle cannot enter a building: near enough counts as arrived;
					// too long on the way and the stop is skipped.
					float toStop = vector.Distance(m_Vehicle.GetOrigin(), m_vStop);
					bool near = toStop < NEAR_STOP_METRES;

					// Standing still: close to the stop it has parked as near as the road gets (the
					// crew walks the rest); far from it, it is stuck and the stop is given up.
					if (GetVehicleSpeed() < STILL_SPEED)
					{
						if (m_fStillSince < 0)
							m_fStillSince = now;
					}
					else
					{
						m_fStillSince = -1;
					}
					float still = 0;
					if (m_fStillSince >= 0)
						still = now - m_fStillSince;

					bool parked = still >= PARKED_SECONDS && toStop < PARKED_NEAR_METRES;
					bool stuck = still >= STUCK_SECONDS && !parked;
					bool late = now - m_fAssignedAt > m_fLegTimeout;
					if (!near && !parked && !late && !stuck)
						return;

					string reason = "too long on the way";
					if (stuck)
						reason = "stuck";
					if (stuck || late)
						GRAD_Endgame_MatchLog.Write("fia_patrol", "", 0, m_Vehicle.GetOrigin(), string.Format("patrol %1 gives up the stop %2: %3, %4 m away", m_iIndex, m_vStop, reason, Math.Round(toStop)));
					near = near || parked;

					RemoveCurrentWaypoint();
					if (near && !m_bFirstStop)
					{
						StartDismount();
						return;
					}
				}

				m_bFirstStop = false;
				DriveToNextStop(now);
				break;
			}

			case STATE_DISMOUNT:
			{
				// the GetOut waypoint is done when the group has no waypoint left
				if (m_Group.GetCurrentWaypoint() && inState < DISMOUNT_MAX_SECONDS)
					return;
				if (inState < DISMOUNT_SECONDS)
					return;

				// on foot to the site
				RemoveCurrentWaypoint();
				GoTo(m_vStop, FOOT_RADIUS, m_sWaypointPrefab);
				m_iState = STATE_CHECK;
				m_fStateAt = now;
				GRAD_Endgame_MatchLog.Debug(string.Format("FIA patrol %1 checks the site at %2", m_iIndex, m_vStop));
				break;
			}

			case STATE_CHECK:
			{
				bool atSite = !m_Group.GetCurrentWaypoint() || AllNear(m_vStop, AT_SITE_METRES);   // waypoint done (1.5 m), or all within 3 m
				if ((atSite && inState >= CHECK_MIN_SECONDS) || inState >= CHECK_MAX_SECONDS)
				{
					RemoveCurrentWaypoint();
					SetLocked(false);
					GoTo(m_Vehicle.GetOrigin(), 5, m_sGetInPrefab);
					m_iState = STATE_RETURN;
					m_fStateAt = now;
				}
				break;
			}

			case STATE_RETURN:
			{
				bool boarded = !m_Group.GetCurrentWaypoint() && inState >= DISMOUNT_SECONDS;
				if (boarded || inState >= RETURN_MAX_SECONDS)
				{
					RemoveCurrentWaypoint();
					int seated, removed, size;
					SeatCrew(seated, removed, size);
					GRAD_Endgame_MatchLog.Write("fia_patrol", "", 0, m_Vehicle.GetOrigin(), string.Format("patrol %1 back in the vehicle: seated %2 of %3, no seat %4", m_iIndex, seated, size, removed));
					DriveToNextStop(now);
				}
				break;
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Lock the vehicle for players and send the GetOut waypoint to the crew.
	protected void StartDismount()
	{
		SetLocked(true);
		GoTo(m_Vehicle.GetOrigin(), 5, m_sGetOutPrefab);

		m_iState = STATE_DISMOUNT;
		m_fStateAt = Now();
		GRAD_Endgame_MatchLog.Write("fia_patrol", "", 0, m_Vehicle.GetOrigin(), string.Format("patrol %1 gets out at the site (vehicle locked)", m_iIndex));
	}

	//------------------------------------------------------------------------------------------------
	//! Lock the vehicle for every player (all seats) or release it. Needs the vanilla
	//! SCR_VehicleSpawnProtectionComponent on the vehicle prefab; without it nothing is locked.
	protected void SetLocked(bool locked)
	{
		if (!m_Vehicle)
			return;

		SCR_VehicleSpawnProtectionComponent protection = SCR_VehicleSpawnProtectionComponent.Cast(m_Vehicle.FindComponent(SCR_VehicleSpawnProtectionComponent));
		if (!protection)
		{
			if (locked)
				GRAD_Endgame_MatchLog.Error("FIA patrol: the vehicle prefab has no SCR_VehicleSpawnProtectionComponent, so players can take it while the crew is out.");
			return;
		}

		if (locked)
		{
			protection.SetProtectOnlyDriverSeat(false);
			protection.SetVehicleOwner(-2);
		}
		else
		{
			protection.ReleaseProtection();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! True if every living soldier of the group is within the distance of the point.
	protected bool AllNear(vector point, float metres)
	{
		array<AIAgent> agents = {};
		m_Group.GetAgents(agents);
		foreach (AIAgent agent : agents)
		{
			IEntity entity = agent.GetControlledEntity();
			if (entity && vector.Distance(entity.GetOrigin(), point) > metres)
				return false;
		}
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void RemoveCurrentWaypoint()
	{
		AIWaypoint current = m_Group.GetCurrentWaypoint();
		if (current)
			m_Group.RemoveWaypoint(current);
		if (m_Waypoint)
			SCR_EntityHelper.DeleteEntityAndChildren(m_Waypoint);
		m_Waypoint = null;
	}

	//------------------------------------------------------------------------------------------------
	//! Give the group one move waypoint (replaces the previous one).
	protected bool GoTo(vector position, float radius, ResourceName prefab)
	{
		if (m_Waypoint)
			SCR_EntityHelper.DeleteEntityAndChildren(m_Waypoint);
		m_Waypoint = null;

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.MatrixIdentity4(params.Transform);
		params.Transform[3] = position;

		Resource resource = Resource.Load(prefab);
		if (!resource || !resource.IsValid())
		{
			GRAD_Endgame_MatchLog.Error("FIA patrol: waypoint prefab does not load: " + prefab);
			GetGame().GetCallqueue().Remove(CheckRoute);
			return false;
		}

		m_Waypoint = AIWaypoint.Cast(GetGame().SpawnEntityPrefab(resource, GetGame().GetWorld(), params));
		if (!m_Waypoint)
		{
			GRAD_Endgame_MatchLog.Error("FIA patrol: the waypoint prefab is not an AIWaypoint: " + prefab);
			GetGame().GetCallqueue().Remove(CheckRoute);
			return false;
		}

		m_Waypoint.SetCompletionRadius(radius);
		m_Group.AddWaypoint(m_Waypoint);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void DriveToNextStop(float now)
	{
		m_iState = STATE_DRIVE;
		m_fStateAt = now;
		m_vStop = m_aRoute[m_iNext];
		m_fAssignedAt = now;
		m_fStillSince = -1;
		m_fLegTimeout = STOP_TIMEOUT_BASE_S;
		if (m_Vehicle)
			m_fLegTimeout += STOP_TIMEOUT_PER_M * vector.Distance(m_Vehicle.GetOrigin(), m_vStop);
		m_iNext = (m_iNext + 1) % m_aRoute.Count();

		if (GoTo(m_vStop, m_fRadius, m_sWaypointPrefab))
			GRAD_Endgame_MatchLog.Debug(string.Format("FIA patrol %1 heads for %2", m_iIndex, m_vStop));
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
			if (patrol.Start(i, route, firstStop, cfg.GetFiaPatrolVehicle(), cfg.GetFiaPatrolCrewGroup(), cfg.GetFiaPatrolWaypoint(), cfg.GetFiaPatrolGetOut(), cfg.GetFiaPatrolGetIn(), cfg.GetFiaPatrolRadius()))
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
