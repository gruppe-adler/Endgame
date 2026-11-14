//------------------------------------------------------------------------------------------------
// PORTED from BreakingContact (GRAD_BC_GrenadeTracker), renamed GRAD_Endgame_*. Not compiled, not run here.
[ComponentEditorProps(category: "Gruppe Adler/Endgame Replay", description: "Records grenade and smoke events for the replay system")]
class GRAD_Endgame_GrenadeTrackerClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
// Records thrown grenades (HE and smoke) into the replay.
//
// Attach to the grenade PREFABS, not to characters. Thrown grenades reach neither of the hooks
// the rest of the replay system uses:
//   - OnProjectileShot fires only for muzzle-launched projectiles, not thrown items
//   - SCR_ExplosionAmmoEffect's invoker is not used by grenade prefabs, which carry a plain
//     ExplosionEffect inside TimerTriggerComponent's PROJECTILE_EFFECTS block
// Both were confirmed silent by live probing, so the grenade has to observe itself.
//
// HE:    the trigger fires -> record a detonation at the grenade's position.
// Smoke: never detonates -> record deployment when it comes to rest, and close the event out
//        when the entity is finally removed, which is when the cloud has dispersed.
class GRAD_Endgame_GrenadeTracker : ScriptComponent
{
	// Poll interval while the grenade is in flight / burning, in ms. Grenades live a few seconds
	// (HE) to ~a minute (smoke), so this is cheap; there are only ever a handful in the air.
	protected const int POLL_INTERVAL_MS = 250;

	// A grenade is considered at rest once it moves less than this per poll, in metres.
	protected const float REST_THRESHOLD_M = 0.15;

	// Safety cap so a stuck entity cannot poll forever.
	protected const int MAX_LIFETIME_MS = 180000;

	protected EGradEndgameExplosiveKind m_eKind;
	protected string m_sPrefabPath;
	protected bool m_bClassified = false;

	// Set once the event has been handed to the replay manager, so it is never recorded twice.
	protected bool m_bEventRecorded = false;

	// Non-owning reference to the recorded event, so a smoke cloud's nominal end time can be
	// replaced with the real one when the entity is finally removed. Owned by the replay manager.
	protected GRAD_Endgame_ExplosiveEvent m_RecordedEvent;

	// Smoke only: when the cloud was deployed, so the duration can be closed out on removal.
	protected float m_fDeployTime = -1;
	protected vector m_vDeployPos;

	protected int m_iElapsedMs = 0;
	protected vector m_vLastPos;

	// Guards against a second poll chain being started for the same component instance.
	protected bool m_bPollStarted = false;

	// Latches the transition from "carried in inventory" to "loose in the world".
	protected bool m_bWasDeployed = false;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		// Recording is server-authoritative, matching the rest of the replay system.
		if (!Replication.IsServer())
			return;

		SetEventMask(owner, EntityEvent.INIT);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		if (!Replication.IsServer())
			return;

		m_sPrefabPath = GetPrefabPath(owner);
		m_bClassified = GRAD_Endgame_ExplosiveCatalog.Classify(m_sPrefabPath, m_eKind);

		if (!m_bClassified)
		{
			// An empty prefab path is normal - the component also initialises on entities that
			// have no prefab of their own (e.g. an in-world instance rather than a spawned item),
			// so only a genuinely unrecognised path is worth warning about.
			if (m_sPrefabPath != "" && GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_GrenadeTracker: unclassified explosive '%1', not recording", m_sPrefabPath), LogLevel.WARNING);
			return;
		}

		m_vLastPos = owner.GetOrigin();
		m_vDeployPos = m_vLastPos;

		// EOnInit can run more than once for the same component instance (observed producing
		// duplicate "smoke deployed" lines in the same millisecond). Without this guard a second
		// poll chain starts and every event is recorded twice.
		if (m_bPollStarted)
			return;

		m_bPollStarted = true;

		// Logged for every tracked explosive, not just the ones that produce an event. Without
		// this there is no way to tell "the grenade was never spawned from the tracked prefab"
		// apart from "it spawned but the event was never recorded" - the two look identical.
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_GrenadeTracker: tracking '%1' kind=%2", m_sPrefabPath, m_eKind), LogLevel.NORMAL);

		GetGame().GetCallqueue().CallLater(Poll, POLL_INTERVAL_MS, false, owner);
	}

	//------------------------------------------------------------------------------------------------
	// Watches the grenade until it detonates (HE) or is removed (smoke).
	protected void Poll(IEntity owner)
	{
		// Entity gone. For smoke this is the cloud dispersing, which closes out the event with a
		// real measured duration. For HE it means the trigger already fired and we recorded it.
		if (!owner)
		{
			OnEntityGone();
			return;
		}

		m_iElapsedMs += POLL_INTERVAL_MS;

		if (m_iElapsedMs >= MAX_LIFETIME_MS)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_GrenadeTracker: giving up on '%1' after %2ms", m_sPrefabPath, MAX_LIFETIME_MS), LogLevel.WARNING);
			return;
		}

		vector currentPos = owner.GetOrigin();
		float moved = vector.Distance(currentPos, m_vLastPos);
		m_vLastPos = currentPos;

		// A grenade sitting in a player's inventory runs this component too - a full loadout means
		// dozens of them per player. Until it is actually thrown it has no independent presence in
		// the world, and because it never moves relative to itself, the "came to rest" test would
		// otherwise fire immediately and record a phantom cloud on the carrier.
		//
		// Skip the recording work while it is still carried, but KEEP POLLING: this is the state
		// every grenade starts in, so returning early here without rescheduling stops the chain
		// after a single tick and the grenade is never seen once it is actually thrown.
		if (IsDeployedInWorld(owner))
		{
			// First frame in the world - reset the movement baseline so the throw is not measured
			// against the carrier's last position.
			if (!m_bWasDeployed)
			{
				m_bWasDeployed = true;
				moved = REST_THRESHOLD_M + 1.0; // treat as moving; it is mid-flight, not at rest
			}

			if (m_eKind == EGradEndgameExplosiveKind.SMOKE)
				PollSmoke(currentPos, moved);
			else
				PollExplosive(owner, currentPos);
		}

		GetGame().GetCallqueue().CallLater(Poll, POLL_INTERVAL_MS, false, owner);
	}

	//------------------------------------------------------------------------------------------------
	// Smoke is recorded as soon as it comes to rest - that is where the cloud forms and where the
	// circle should be drawn.
	//
	// The event is recorded IMMEDIATELY with a nominal duration rather than waiting for the entity
	// to be removed. Smoke entities frequently outlive the round (observed persisting for 3.5+
	// minutes), so deferring the record until removal loses the event entirely. If the entity is
	// removed later, OnEntityGone() refines the end time to the real measured value.
	protected void PollSmoke(vector currentPos, float moved)
	{
		if (m_bEventRecorded)
			return; // already in the replay

		if (m_fDeployTime < 0)
		{
			if (moved > REST_THRESHOLD_M)
				return; // still rolling

			m_fDeployTime = GetWorldTimeSeconds();
			m_vDeployPos = currentPos;

			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_GrenadeTracker: smoke deployed '%1' at %2", m_sPrefabPath, currentPos.ToString()), LogLevel.NORMAL);
		}

		// Retried every poll until it takes. A smoke deployed before recording starts (during the
		// staging phase) would otherwise never enter the replay even though the cloud is still
		// visibly burning once the round begins.
		int smokeColor = GRAD_Endgame_ExplosiveCatalog.GetSmokeColor(m_sPrefabPath);
		RecordEvent(
			EGradEndgameExplosiveKind.SMOKE,
			m_vDeployPos,
			m_vDeployPos,
			m_fDeployTime,
			m_fDeployTime + GRAD_Endgame_ExplosiveCatalog.SMOKE_FALLBACK_DURATION,
			smokeColor);
	}

	//------------------------------------------------------------------------------------------------
	// HE detonates on a fuse timer. TimerTriggerComponent.WasTriggered() tells us the moment it
	// goes off, which is the detonation point we want.
	protected void PollExplosive(IEntity owner, vector currentPos)
	{
		if (m_bEventRecorded)
			return;

		TimerTriggerComponent trigger = TimerTriggerComponent.Cast(owner.FindComponent(TimerTriggerComponent));
		if (!trigger)
			return;

		if (!trigger.WasTriggered())
			return;

		float now = GetWorldTimeSeconds();
		RecordEvent(m_eKind, currentPos, currentPos, now, now + GRAD_Endgame_ExplosiveCatalog.HE_FLASH_DURATION, 0);

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_GrenadeTracker: HE detonation '%1' at %2", m_sPrefabPath, currentPos.ToString()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// The entity has been removed from the world.
	protected void OnEntityGone()
	{
		float now = GetWorldTimeSeconds();

		if (m_eKind == EGradEndgameExplosiveKind.SMOKE)
		{
			// The cloud was already recorded at deployment with a nominal duration. Now that the
			// entity is actually gone we know the real end time, so refine it.
			if (m_bEventRecorded && m_RecordedEvent)
			{
				m_RecordedEvent.endTime = now;

				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print(string.Format("GRAD_Endgame_GrenadeTracker: smoke ended '%1', measured duration %2s",
						m_sPrefabPath, now - m_RecordedEvent.startTime), LogLevel.NORMAL);
				return;
			}

			// Removed before it ever came to rest - record at the last known position.
			int smokeColor = GRAD_Endgame_ExplosiveCatalog.GetSmokeColor(m_sPrefabPath);
			RecordEvent(EGradEndgameExplosiveKind.SMOKE, m_vLastPos, m_vLastPos,
				now - GRAD_Endgame_ExplosiveCatalog.SMOKE_FALLBACK_DURATION, now, smokeColor);
			return;
		}

		if (m_bEventRecorded)
			return;

		// Only a grenade that actually reached the world can have detonated. Without this, every
		// grenade consumed from inventory or despawned unthrown - hundreds per round - would be
		// recorded as a blast on whoever was carrying it.
		if (!m_bWasDeployed)
			return;

		// HE removed without WasTriggered() ever being observed - the fuse fired between polls.
		// Record it anyway at the last known position rather than losing the event.
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_GrenadeTracker: HE gone without observed trigger '%1' at %2",
				m_sPrefabPath, m_vLastPos.ToString()), LogLevel.NORMAL);

		RecordEvent(m_eKind, m_vLastPos, m_vLastPos, now, now + GRAD_Endgame_ExplosiveCatalog.HE_FLASH_DURATION, 0);
	}

	//------------------------------------------------------------------------------------------------
	protected void RecordEvent(EGradEndgameExplosiveKind kind, vector pos, vector endPos, float start, float end, int color)
	{
		if (m_bEventRecorded)
			return;

		GRAD_Endgame_ReplayManager replayManager = GRAD_Endgame_ReplayManager.GetInstance();
		if (!replayManager)
			return;

		// Keep the created event so a smoke cloud's end time can be corrected once the entity is
		// actually removed. The replay manager owns it; this is a non-owning back-reference.
		GRAD_Endgame_ExplosiveEvent evt = replayManager.RecordExplosiveEvent(kind, pos, endPos, start, end, color, "");

		// Null means recording was not active yet - a smoke thrown during the staging phase, for
		// instance. Leave m_bEventRecorded clear so the still-burning cloud is picked up on a
		// later poll, once recording has begun, rather than being lost for the whole round.
		if (!evt)
			return;

		m_bEventRecorded = true;
		m_RecordedEvent = evt;
	}

	//------------------------------------------------------------------------------------------------
	//! Whether the grenade is loose in the world rather than sitting in someone's inventory.
	//!
	//! A full loadout means dozens of tracked grenades per player, none of which should record
	//! anything until actually thrown. An item held in a container is parented to that container,
	//! so having no parent is the signal that it has been released into the world.
	protected bool IsDeployedInWorld(IEntity entity)
	{
		if (!entity)
			return false;

		return (entity.GetParent() == null);
	}

	//------------------------------------------------------------------------------------------------
	protected float GetWorldTimeSeconds()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return 0;

		return world.GetWorldTime() / 1000.0;
	}

	//------------------------------------------------------------------------------------------------
	protected string GetPrefabPath(IEntity entity)
	{
		if (!entity)
			return "";

		EntityPrefabData prefabData = entity.GetPrefabData();
		if (!prefabData)
			return "";

		return prefabData.GetPrefabName();
	}
}
