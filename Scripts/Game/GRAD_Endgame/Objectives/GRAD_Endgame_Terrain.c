//------------------------------------------------------------------------------------------------
//! Puts placed objects on the ground. Hand-placed prefabs hover or sink when the Y value in the
//! world file does not match the terrain; every machine runs this at init, so server and clients
//! agree. Only in play mode: in the editor the entity is left where the designer put it.
class GRAD_Endgame_Terrain
{
	//------------------------------------------------------------------------------------------------
	//! Set the entity's height to the terrain surface plus an offset.
	static void Snap(IEntity entity, float offset = 0)
	{
		if (!entity || !GetGame().InPlayMode())
			return;

		World world = GetGame().GetWorld();
		if (!world)
			return;

		vector origin = entity.GetOrigin();
		float y = world.GetSurfaceY(origin[0], origin[2]) + offset;
		if (Math.AbsFloat(origin[1] - y) < 0.01)
			return;

		origin[1] = y;
		entity.SetOrigin(origin);
	}
}

//------------------------------------------------------------------------------------------------
class GRAD_Endgame_SnapToTerrainComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Put this on any prop that has no Endgame component of its own (ammo crates, signs) so it
//! stands on the terrain.
class GRAD_Endgame_SnapToTerrainComponent : ScriptComponent
{
	[Attribute("0", desc: "Height above the terrain surface in metres.")]
	protected float m_fOffset;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		GRAD_Endgame_Terrain.Snap(owner, m_fOffset);
	}
}
