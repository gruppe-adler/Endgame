//------------------------------------------------------------------------------------------------
class GRAD_Endgame_FobVehicleSpawnerComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! A parking spot for one light vehicle at a FOB (decided 2026-11-10: FOBs provide light vehicles
//! that respawn when taken away).
//!
//! Rules (Sugar Lake's transport respawn):
//!  - The vehicle is there from game start (decided 2026-11-10: the FOB's AI guards it anyway).
//!  - When the vehicle is destroyed, or has been driven more than m_fTransportRespawnRadius (500 m)
//!    from the spot, a timer of m_fTransportRespawnSeconds (400 s) starts.
//!  - When the timer is up and the spot is clear, a new vehicle spawns. A wreck of the spot's own
//!    vehicle standing on the spot is removed first. The old one, if it still
//!    exists, stays where it is: whoever took it keeps it.
//!  - A vehicle brought back within the radius before the timer is up cancels the respawn.
//!
//! Place one entity with this component per parking spot near a FOB and set the faction key to
//! the FOB's side. Its position and direction are the vehicle's.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-10): not compiled, not run.
class GRAD_Endgame_FobVehicleSpawnerComponent : ScriptComponent
{
	[Attribute("BLUFOR", desc: "COALITION faction key of the FOB this spot belongs to.")]
	protected string m_sFactionKey;

	[Attribute("", UIWidgets.ResourcePickerThumbnail, "Vehicle prefab, e.g. a UAZ for the Soviets or an M151 for the US.", params: "et")]
	protected ResourceName m_sVehiclePrefab;

	[Attribute("5", desc: "The spot counts as clear when no vehicle is within this many metres.")]
	protected float m_fClearRadius;

	protected static ref array<GRAD_Endgame_FobVehicleSpawnerComponent> s_aAll = {};

	protected IEntity m_Vehicle;
	protected bool m_bEnabled;
	protected float m_fRespawnAt = -1;   //!< world time in seconds, -1 = no respawn pending

	protected const int TICK_MS = 5000;

	//------------------------------------------------------------------------------------------------
	static array<GRAD_Endgame_FobVehicleSpawnerComponent> GetAll()
	{
		return s_aAll;
	}

	//------------------------------------------------------------------------------------------------
	string GetFactionKey()
	{
		return m_sFactionKey;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_aAll.Insert(this);

		if (Replication.IsServer())
			GetGame().GetCallqueue().CallLater(WaitForGameStart, 2000, true);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: spawn the first vehicle once the match runs (same moment the garrisons spawn).
	protected void WaitForGameStart()
	{
		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		bool running;
		if (gameMode)
			running = gameMode.GetPhase() != GRAD_Endgame_EPhase.WAITING;
		else
			running = COA_Gamemode.GetInstance() && COA_Gamemode.GetInstance().m_GamemodeState == COA_EGamemodeState.GAME;

		if (!running)
			return;

		GetGame().GetCallqueue().Remove(WaitForGameStart);
		Enable();
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(ServerTick);
		GetGame().GetCallqueue().Remove(WaitForGameStart);
		s_aAll.RemoveItem(this);
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: spawn the first vehicle and start watching it. Called once at game start.
	void Enable()
	{
		if (!Replication.IsServer() || m_bEnabled)
			return;

		m_bEnabled = true;
		SpawnVehicle();
		GetGame().GetCallqueue().CallLater(ServerTick, TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	protected float Now()
	{
		return GetGame().GetWorld().GetWorldTime() * 0.001;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsDestroyed(notnull IEntity vehicle)
	{
		SCR_VehicleDamageManagerComponent damage = SCR_VehicleDamageManagerComponent.Cast(vehicle.FindComponent(SCR_VehicleDamageManagerComponent));
		return damage && damage.GetState() == EDamageState.DESTROYED;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsGone(notnull GRAD_Endgame_Config cfg)
	{
		if (!m_Vehicle)
			return true;

		if (IsDestroyed(m_Vehicle))
			return true;

		return vector.Distance(m_Vehicle.GetOrigin(), GetOwner().GetOrigin()) > cfg.GetTransportRespawnRadius();
	}

	//------------------------------------------------------------------------------------------------
	//! Server, every 5 s while enabled.
	protected void ServerTick()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
			return;

		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (gameMode && gameMode.GetPhase() == GRAD_Endgame_EPhase.ENDED)
		{
			GetGame().GetCallqueue().Remove(ServerTick);
			return;
		}

		if (!IsGone(cfg))
		{
			// Brought back in time: no new vehicle.
			m_fRespawnAt = -1;
			return;
		}

		if (m_fRespawnAt < 0)
		{
			m_fRespawnAt = Now() + cfg.GetTransportRespawnSeconds();
			return;
		}

		if (Now() < m_fRespawnAt)
			return;

		// A wreck of our own vehicle on the spot is cleared away; anything else blocks the spot.
		if (m_Vehicle && IsDestroyed(m_Vehicle) && vector.Distance(m_Vehicle.GetOrigin(), GetOwner().GetOrigin()) < m_fClearRadius)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(m_Vehicle);
			m_Vehicle = null;
		}

		if (!IsSpotClear())
			return;

		m_fRespawnAt = -1;
		SpawnVehicle();
	}

	//------------------------------------------------------------------------------------------------
	//! No vehicle stands on the spot (a wreck, or one parked there by players).
	protected bool IsSpotClear()
	{
		vector spot = GetOwner().GetOrigin();
		foreach (GRAD_Endgame_FobVehicleSpawnerComponent other : s_aAll)
		{
			if (other.m_Vehicle && vector.Distance(other.m_Vehicle.GetOrigin(), spot) < m_fClearRadius)
				return false;
		}
		// TODO (Workbench): also check for vehicles that no spawner owns (a captured enemy jeep
		// parked on the spot), with a world query around the spot.
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void SpawnVehicle()
	{
		if (m_sVehiclePrefab.IsEmpty())
		{
			GRAD_Endgame_MatchLog.Error(string.Format("FOB vehicle spot of %1 at %2 has no vehicle prefab.", m_sFactionKey, GetOwner().GetOrigin()));
			return;
		}

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		GetOwner().GetWorldTransform(params.Transform);

		m_Vehicle = GetGame().SpawnEntityPrefab(Resource.Load(m_sVehiclePrefab), GetGame().GetWorld(), params);
		if (!m_Vehicle)
		{
			GRAD_Endgame_MatchLog.Error("FOB vehicle did not spawn: " + m_sVehiclePrefab);
			return;
		}

		GRAD_Endgame_MatchLog.Write("vehicle_spawn", m_sFactionKey, 0, GetOwner().GetOrigin(), m_sVehiclePrefab);
	}
}
