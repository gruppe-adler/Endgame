//------------------------------------------------------------------------------------------------
class GRAD_Endgame_FobComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! A faction's FOB (M2). Rule decided 2026-11-10: the FOB is taken as soon as its garrison is wiped
//! out. Then, on the server:
//!  - every spawn point of the owning faction is moved to the FOB (the pattern from BreakingContact:
//!    COALITION resolves a spawn location from the spawn point ENTITY at spawn time, so moving the
//!    entity is enough, no re-registration);
//!  - the owning faction gets a free respawn (Sugar Lake);
//!  - the light vehicles on this side's GRAD_Endgame_FobVehicleSpawnerComponent spots appear;
//!  - when both FOBs are taken, the game moves from phase FOB to DOCUMENTS (Sugar Lake).
//!
//! Prefab: this component plus a GRAD_Endgame_GarrisonComponent on the SAME entity. Place one per
//! faction and set the faction key. The FOB position also serves as the reference point for the
//! balanced choice of document sites.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-10): not compiled, not run.
class GRAD_Endgame_FobComponent : ScriptComponent
{
	[Attribute("BLUFOR", desc: "COALITION faction key of the side this FOB belongs to.")]
	protected string m_sFactionKey;

	[Attribute("0 0 0", desc: "Where the moved spawn point stands, relative to this entity. Keep it clear of the garrison.")]
	protected vector m_vSpawnOffset;

	[RplProp()]
	protected bool m_bTaken;

	protected static ref array<GRAD_Endgame_FobComponent> s_aAll = {};

	//------------------------------------------------------------------------------------------------
	static array<GRAD_Endgame_FobComponent> GetAll()
	{
		return s_aAll;
	}

	//------------------------------------------------------------------------------------------------
	//! The FOB of a faction, or null.
	static GRAD_Endgame_FobComponent GetForFaction(string factionKey)
	{
		foreach (GRAD_Endgame_FobComponent fob : s_aAll)
		{
			if (fob && fob.GetFactionKey() == factionKey)
				return fob;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	string GetFactionKey()
	{
		return m_sFactionKey;
	}

	//------------------------------------------------------------------------------------------------
	bool IsTaken()
	{
		return m_bTaken;
	}

	//------------------------------------------------------------------------------------------------
	vector GetSpawnPosition()
	{
		return GetOwner().CoordToParent(m_vSpawnOffset);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_aAll.Insert(this);

		if (!Replication.IsServer())
			return;

		GRAD_Endgame_GarrisonComponent garrison = GRAD_Endgame_GarrisonComponent.Cast(owner.FindComponent(GRAD_Endgame_GarrisonComponent));
		if (!garrison)
		{
			GRAD_Endgame_MatchLog.Error(string.Format("FOB of %1 has no GRAD_Endgame_GarrisonComponent on the same entity; it can never be taken.", m_sFactionKey));
			return;
		}

		garrison.GetOnCleared().Insert(OnGarrisonCleared);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		s_aAll.RemoveItem(this);
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnGarrisonCleared(GRAD_Endgame_GarrisonComponent garrison)
	{
		if (m_bTaken)
			return;

		m_bTaken = true;
		Replication.BumpMe();
		GRAD_Endgame_MatchLog.Write("fob_state", m_sFactionKey, 0, GetOwner().GetOrigin(), "taken");

		MoveSpawnPoints();

		// Light vehicles at this FOB become available now.
		foreach (GRAD_Endgame_FobVehicleSpawnerComponent spawner : GRAD_Endgame_FobVehicleSpawnerComponent.GetAll())
		{
			if (spawner.GetFactionKey() == m_sFactionKey)
				spawner.Enable();
		}

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		COA_RespawnManager respawnManager = COA_RespawnManager.GetInstance();
		if (cfg && cfg.GetFreeRespawnOnFob() && respawnManager)
			respawnManager.EndgameFreeRespawn(m_sFactionKey, "FOB taken");

		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (gameMode)
			gameMode.OnFobTaken(this);
	}

	//------------------------------------------------------------------------------------------------
	//! Move every spawn point of the owning faction to the FOB.
	protected void MoveSpawnPoints()
	{
		COA_RespawnManager respawnManager = COA_RespawnManager.GetInstance();
		if (!respawnManager)
			return;

		vector target = GetSpawnPosition();
		int moved;

		array<COA_SpawnPointData> spawnPoints = respawnManager.GetFactionSpawnpoints(m_sFactionKey);
		foreach (COA_SpawnPointData spawnPointData : spawnPoints)
		{
			if (!spawnPointData)
				continue;

			IEntity spawnPointEntity = COA_EntityHelper.GetEntityFromRplId(spawnPointData.GetSpawnPointEntity());
			if (!spawnPointEntity)
				continue;

			spawnPointEntity.SetOrigin(target);
			moved++;
		}

		GRAD_Endgame_MatchLog.Write("spawn_moved", m_sFactionKey, 0, target, string.Format("%1 spawn point(s)", moved));
		if (moved == 0)
			GRAD_Endgame_MatchLog.Error(string.Format("No spawn point of %1 found to move to its FOB.", m_sFactionKey));
	}
}
