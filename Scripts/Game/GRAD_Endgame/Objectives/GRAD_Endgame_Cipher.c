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
		{
			string carrierKey = GRAD_Endgame_InfoService.GetFactionKey(carrierId);
			GRAD_Endgame_MatchLog.Write("cipher", carrierKey, carrierId, GetOwner().GetOrigin(), "picked up");
			// The side that went for it has it now: its "Recover the cipher" objective is done.
			GRAD_Endgame_InfoService.RemoveMarker(carrierKey, "cipher", true);
		}

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

	[Attribute(defvalue: "1", desc: "Automatic: the cipher lies on a free table near this site. Off: it lies exactly where the site is placed.")]
	protected bool m_bAutoPlace;

	[Attribute(defvalue: "100", desc: "Metres around the site to look for a table.", params: "5 300 1")]
	protected float m_fPlaceRadius;

	protected static const float MAX_TABLE_HEIGHT_DIFF = 2.5;   //!< metres above or below the site
	protected static const float TABLE_DROP_HEIGHT = 0.05;      //!< metres above the table top

	//------------------------------------------------------------------------------------------------
	//! Server: top of the nearest suitable table that no document site uses, on the site's floor.
	//! Returns false if there is none (the cipher then lies at the site).
	protected bool FindTable(out vector position, out string description)
	{
		array<ref GRAD_Endgame_TableHit> hits = {};
		GRAD_Endgame_TableScan.Find(GetOwner().GetOrigin(), m_fPlaceRadius, hits);

		foreach (GRAD_Endgame_TableHit hit : hits)
		{
			if (!GRAD_Endgame_TableScan.IsSuitable(hit))
				continue;
			if (Math.AbsFloat(hit.m_vTop[1] - GetOwner().GetOrigin()[1]) > MAX_TABLE_HEIGHT_DIFF)
				continue;
			if (GRAD_Endgame_DocumentSiteComponent.IsTableUsed(hit.m_Entity))
				continue;

			position = hit.m_vTop + Vector(0, TABLE_DROP_HEIGHT, 0);
			description = string.Format("on %1, %2 m from the site", FilePath.StripPath(hit.m_sName), Math.Round(hit.m_fDistance));
			return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	static array<GRAD_Endgame_CipherSiteComponent> GetAll()
	{
		return s_aAll;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_aAll.Insert(this);

		if (!System.IsConsoleApp())
			SetEventMask(owner, EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	//! Debug marker for the Game Master while the editor is open: an orange sphere and a tall line
	//! where the cipher may lie. (The site prefab is not a Game Master editable: adding that
	//! component moved the site to 0 0 0 in the 2026-11-14 test.)
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		SCR_EditorManagerEntity editorManager = SCR_EditorManagerEntity.GetInstance();
		if (!editorManager || editorManager.IsLimited() || !editorManager.IsOpened())
			return;

		vector position = owner.GetOrigin();
		Shape.CreateSphere(Color.ORANGE, ShapeFlags.WIREFRAME | ShapeFlags.NOZBUFFER | ShapeFlags.ONCE, position, 0.35);
		vector points[2];
		points[0] = position;
		points[1] = position + Vector(0, 25, 0);
		Shape.CreateLines(Color.ORANGE, ShapeFlags.NOZBUFFER | ShapeFlags.ONCE, points, 2);
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
		params.Transform[3][1] = params.Transform[3][1] + 0.5;   // let physics drop it, so it cannot start below the surface

		string where = "at the site";
		if (m_bAutoPlace)
		{
			vector onTable;
			string description;
			if (FindTable(onTable, description))
			{
				params.Transform[3] = onTable;
				where = description;
			}
			else
				where = "no free table in reach, at the site";
		}

		IEntity item = GetGame().SpawnEntityPrefab(Resource.Load(prefab), GetGame().GetWorld(), params);
		if (!item || !item.FindComponent(GRAD_Endgame_CipherComponent))
		{
			GRAD_Endgame_MatchLog.Error("Cipher prefab is missing or has no GRAD_Endgame_CipherComponent: " + prefab);
			return item;
		}

		GRAD_Endgame_MatchLog.Write("cipher", "", 0, item.GetOrigin(), "spawned " + where);
		return item;
	}
}
