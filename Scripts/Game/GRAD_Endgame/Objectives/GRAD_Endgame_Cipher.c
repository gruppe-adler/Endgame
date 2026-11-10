//------------------------------------------------------------------------------------------------
class GRAD_Endgame_CipherComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! The cipher material (codebook). Put this component on an inventory item prefab and name that
//! prefab in the config (m_sCipherPrefab). The server spawns exactly one per match, at the start
//! of the radio phase, at one of the placed cipher sites.
//!
//! Who carries it is never sent anywhere: the server and the carrier's own machine can see it
//! through the item hierarchy, nobody gets a marker (concept: no carrier marker). When the carrier
//! dies the item stays in his inventory, so it can be taken from the body (to verify in Workbench).
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-10): not compiled, not run.
class GRAD_Endgame_CipherComponent : ScriptComponent
{
	protected static GRAD_Endgame_CipherComponent s_Instance;
	protected int m_iLastCarrierId;   //!< server: for the "cipher" log lines

	//------------------------------------------------------------------------------------------------
	static GRAD_Endgame_CipherComponent GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_Instance = this;

		if (Replication.IsServer())
			GetGame().GetCallqueue().CallLater(WatchCarrier, 1000, true);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(WatchCarrier);
		if (s_Instance == this)
			s_Instance = null;
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	//! The character holding the cipher, alive or dead, or null if it lies on the ground. Walks up
	//! the hierarchy because an item in a backpack sits below the backpack, not the character.
	IEntity GetCarrier()
	{
		IEntity parent = GetOwner().GetParent();
		while (parent)
		{
			if (SCR_ChimeraCharacter.Cast(parent))
				return parent;
			parent = parent.GetParent();
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! True if this character holds the cipher.
	bool IsCarriedBy(IEntity character)
	{
		return character && GetCarrier() == character;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: log pickups and drops, once per second.
	protected void WatchCarrier()
	{
		IEntity carrier = GetCarrier();
		int carrierId;
		if (carrier)
			carrierId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(carrier);

		if (carrierId == m_iLastCarrierId)
			return;

		if (m_iLastCarrierId > 0)
			GRAD_Endgame_MatchLog.Write("cipher", GRAD_Endgame_InfoService.GetFactionKey(m_iLastCarrierId), m_iLastCarrierId, GetOwner().GetOrigin(), "dropped or carrier died");
		if (carrierId > 0)
			GRAD_Endgame_MatchLog.Write("cipher", GRAD_Endgame_InfoService.GetFactionKey(carrierId), carrierId, GetOwner().GetOrigin(), "picked up");

		m_iLastCarrierId = carrierId;
	}
}

//------------------------------------------------------------------------------------------------
class GRAD_Endgame_CipherSiteComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! A place where the cipher may lie. Place several on a map; at the start of the radio phase the
//! server picks one at random and spawns the cipher there (GRAD_Endgame_GameModeComponent).
class GRAD_Endgame_CipherSiteComponent : ScriptComponent
{
	protected static ref array<GRAD_Endgame_CipherSiteComponent> s_aAll = {};

	//------------------------------------------------------------------------------------------------
	static array<GRAD_Endgame_CipherSiteComponent> GetAll()
	{
		return s_aAll;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		GRAD_Endgame_Terrain.Snap(owner);
		s_aAll.Insert(this);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		s_aAll.RemoveItem(this);
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: spawn the cipher item here. Returns the new item or null.
	IEntity SpawnCipher(ResourceName prefab)
	{
		if (!Replication.IsServer() || prefab.IsEmpty())
			return null;

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		GetOwner().GetWorldTransform(params.Transform);

		IEntity item = GetGame().SpawnEntityPrefab(Resource.Load(prefab), GetGame().GetWorld(), params);
		if (!item || !item.FindComponent(GRAD_Endgame_CipherComponent))
		{
			GRAD_Endgame_MatchLog.Error("Cipher prefab is missing or has no GRAD_Endgame_CipherComponent: " + prefab);
			return item;
		}

		GRAD_Endgame_MatchLog.Write("cipher", "", 0, GetOwner().GetOrigin(), "spawned");
		return item;
	}
}
