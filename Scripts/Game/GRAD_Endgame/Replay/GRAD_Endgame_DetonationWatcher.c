//------------------------------------------------------------------------------------------------
// PORTED from BreakingContact (GRAD_BC_DetonationWatcher), renamed GRAD_Endgame_*. Not compiled, not run here.
[ComponentEditorProps(category: "Gruppe Adler/Endgame Replay", description: "Reports fuse detonations to the replay system")]
class GRAD_Endgame_DetonationWatcherClass : ScriptComponentClass
{
	//------------------------------------------------------------------------------------------------
	//! Needs a fuse to watch. Declared the same way SCR_InstantTriggerComponent declares it, so the
	//! editor refuses to place this on an entity that has no trigger.
	static override array<typename> Requires(IEntityComponentSource src)
	{
		array<typename> requires = {};

		requires.Insert(BaseTriggerComponent);

		return requires;
	}
}

//------------------------------------------------------------------------------------------------
// Reports the moment an explosive's fuse goes off, so the replay can mark the detonation point.
//
// Sits ALONGSIDE the entity's BaseTriggerComponent rather than modding it. That is the pattern the
// base game itself uses (see SCR_InstantTriggerComponent, which reaches the trigger the same way):
// the interesting methods on BaseTriggerComponent are either `proto external` - engine-implemented,
// callable but not overridable - or `protected` events, so a sibling component is the supported way
// to interact with a fuse.
//
// Why not poll from GRAD_Endgame_GrenadeTracker: that sampled WasTriggered() every 250ms and never
// caught a single detonation across many test rounds, because the flag is set and the grenade
// destroyed inside one frame. Checking on EntityEvent.FRAME samples every frame instead, which is
// the finest granularity available and cannot miss the window.
class GRAD_Endgame_DetonationWatcher : ScriptComponent
{
	protected BaseTriggerComponent m_Trigger;
	protected bool m_bReported = false;

	// Last position seen while the fuse was still live. Read every frame because the entity is
	// usually destroyed the instant it detonates, and by then its origin is no longer available.
	protected vector m_vLastPos;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		// Recording is server-authoritative, matching the rest of the replay system.
		if (!Replication.IsServer())
			return;

		SetEventMask(owner, EntityEvent.INIT | EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		m_Trigger = BaseTriggerComponent.Cast(owner.FindComponent(BaseTriggerComponent));
		m_vLastPos = owner.GetOrigin();
	}

	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		if (m_bReported || !m_Trigger || !owner)
			return;

		if (!m_Trigger.WasTriggered())
		{
			// Not yet detonated - keep the position current so it is still known once it is.
			m_vLastPos = owner.GetOrigin();
			return;
		}

		ReportDetonation(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void ReportDetonation(IEntity owner)
	{
		if (m_bReported)
			return;

		string prefabPath = "";
		EntityPrefabData prefabData = owner.GetPrefabData();
		if (prefabData)
			prefabPath = prefabData.GetPrefabName();

		// Only explosives the replay draws as a point detonation. Smoke never detonates and is
		// handled separately as a lingering cloud.
		EGradEndgameExplosiveKind kind;
		if (!GRAD_Endgame_ExplosiveCatalog.Classify(prefabPath, kind))
			return;

		if (kind == EGradEndgameExplosiveKind.SMOKE)
			return;

		GRAD_Endgame_ReplayManager replayManager = GRAD_Endgame_ReplayManager.GetInstance();
		if (!replayManager)
			return;

		m_bReported = true;

		BaseWorld world = GetGame().GetWorld();
		float now = 0;
		if (world)
			now = world.GetWorldTime() / 1000.0;

		replayManager.RecordExplosiveEvent(
			kind,
			m_vLastPos,
			m_vLastPos,
			now,
			now + GRAD_Endgame_ExplosiveCatalog.HE_FLASH_DURATION,
			0,
			"");

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_DetonationWatcher: detonation '%1' at %2", prefabPath, m_vLastPos.ToString()), LogLevel.NORMAL);
	}
}
