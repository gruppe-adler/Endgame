//------------------------------------------------------------------------------------------------
// PORTED from BreakingContact (GRAD_BC_ExplosionTracker), renamed GRAD_Endgame_*. Not compiled, not run here.
[ComponentEditorProps(category: "Gruppe Adler/Endgame Replay", description: "Records AT rocket launches and detonations for the replay system")]
class GRAD_Endgame_ExplosionTrackerClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
// Records AT rockets into the replay as launch -> impact pairs, so the map layer can animate the
// projectile along its flight path.
//
// Attach to the GAMEMODE entity (alongside GRAD_Endgame_ReplayManager). Unlike the grenade tracker
// this is a single global listener, because both hooks it uses are global or character-level:
//
//   Launch: GRAD_Endgame_WeaponFireTracker already listens to OnProjectileShot on characters and
//           forwards here via NotifyProjectileLaunched().
//   Impact: SCR_ExplosionAmmoEffect's static invoker fires at detonation. Confirmed working for
//           Ammo_Rocket_PG22; grenades do NOT use it, which is why they have their own tracker.
//
// A rocket's whole flight is ~1-3s while the replay records every 3s, so sampling the trajectory
// would miss it. Recording launch and impact as one timed event lets the client interpolate.
class GRAD_Endgame_ExplosionTracker : ScriptComponent
{
	// Launches awaiting their detonation, oldest first.
	//
	// A plain array keyed by prefab, NOT a map keyed by IEntity: the entity reported as
	// damageSource at detonation is not the same IEntity delivered at launch, so identity lookups
	// never match. Matching is therefore "oldest outstanding launch of this prefab".
	//
	// STATIC, and deliberately so. Several component instances exist in a session (one per world
	// layer) and s_Instance is overwritten by each, so launches are registered against the newest
	// instance while the detonation callback runs on whichever instance subscribed first. With
	// per-instance state the two never meet and every impact is silently dropped.
	protected static ref array<ref GRAD_Endgame_PendingLaunch> s_aPendingLaunches = {};

	// A launch with no matching impact after this long is discarded, so the map cannot grow
	// without bound when a rocket despawns unexploded.
	protected const int LAUNCH_TIMEOUT_MS = 30000;

	protected static GRAD_Endgame_ExplosionTracker s_Instance;

	// The invoker is process-global, and OnPostInit runs once per component instance (observed
	// firing three times in a single session). A per-instance guard would therefore still allow
	// duplicate subscriptions, which would record every AT impact several times - so this is
	// static, matching the lifetime of the invoker it guards.
	protected static bool s_bHookRegistered = false;

	//------------------------------------------------------------------------------------------------
	static GRAD_Endgame_ExplosionTracker GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		s_Instance = this;

		if (!Replication.IsServer())
			return;

		RegisterExplosionHook();
	}

	//------------------------------------------------------------------------------------------------
	protected void RegisterExplosionHook()
	{
		if (s_bHookRegistered)
			return;

		OnExplosionAmmoEffectInvoker invoker = SCR_ExplosionAmmoEffect.GetOnExplosionAmmoEffect();
		if (!invoker)
		{
			Print("GRAD_Endgame_ExplosionTracker: explosion invoker unavailable, AT impacts will not be recorded", LogLevel.WARNING);
			return;
		}

		invoker.Insert(OnExplosionAmmoEffect);
		s_bHookRegistered = true;

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ExplosionTracker: subscribed to explosion invoker", LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! Called by GRAD_Endgame_WeaponFireTracker when a projectile is launched. Only AT-class rounds are
	//! retained; bullets are far too numerous and are not part of this feature.
	void NotifyProjectileLaunched(IEntity projectile, string prefabPath, string factionKey)
	{
		if (!projectile)
			return;

		EGradEndgameExplosiveKind kind;
		if (!GRAD_Endgame_ExplosiveCatalog.Classify(prefabPath, kind))
			return;

		if (kind != EGradEndgameExplosiveKind.AT)
			return;

		GRAD_Endgame_PendingLaunch pending = new GRAD_Endgame_PendingLaunch();
		pending.position = projectile.GetOrigin();
		pending.time = GetWorldTimeSeconds();
		pending.factionKey = factionKey;
		pending.prefabPath = prefabPath;

		s_aPendingLaunches.Insert(pending);

		PruneExpiredLaunches();

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ExplosionTracker: AT launch '%1' at %2", prefabPath, pending.position.ToString()), LogLevel.NORMAL);

		// Follow the rocket until it disappears. The detonation invoker has never once fired for
		// these rockets, so this establishes what actually happens to them: whether they fly and
		// vanish (no explosion effect configured on this ammo), or are destroyed at the muzzle.
		TraceProjectile(projectile, 0, pending.position);
	}

	//------------------------------------------------------------------------------------------------
	// Fired at the moment of detonation for ammo using SCR_ExplosionAmmoEffect.
	//
	// IMPORTANT: outMat is declared [3], so valid indices are 0..2 - there is no outMat[3].
	// outMat[0] is the world position; rows 1 and 2 are orientation vectors. This was verified
	// against damageSource.GetOrigin() during probing.
	protected void OnExplosionAmmoEffect(IEntity pHitEntity, inout vector outMat[3], IEntity damageSource, notnull Instigator instigator, string colliderName, float speed)
	{
		if (!Replication.IsServer())
			return;

		vector impactPos = outMat[0];

		// Match by prefab and recency rather than by entity identity. The entity reported as
		// damageSource at detonation is NOT the same IEntity that OnProjectileShot handed us at
		// launch (verified in-game: the probe saw the explosion while a keyed lookup on the launch
		// entity found nothing), so a map keyed on IEntity never hits.
		string sourcePrefab = GetPrefabPath(damageSource);

		// Every explosion is logged before any early return. Without this a detonation that fails
		// to match is indistinguishable from the invoker never firing at all.
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ExplosionTracker: explosion source='%1' at %2 (pending=%3)",
				sourcePrefab, impactPos.ToString(), s_aPendingLaunches.Count()), LogLevel.NORMAL);

		if (sourcePrefab == "")
			return;

		GRAD_Endgame_PendingLaunch pending = TakeMatchingLaunch(sourcePrefab);
		if (!pending)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ExplosionTracker: no pending launch matched '%1'", sourcePrefab), LogLevel.NORMAL);
			return; // not a rocket we are tracking (e.g. a mine or vehicle explosion)
		}

		GRAD_Endgame_ReplayManager replayManager = GRAD_Endgame_ReplayManager.GetInstance();
		if (!replayManager)
			return;

		replayManager.RecordExplosiveEvent(
			EGradEndgameExplosiveKind.AT,
			pending.position,
			impactPos,
			pending.time,
			GetWorldTimeSeconds(),
			0,
			pending.factionKey);

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ExplosionTracker: AT impact at %1", impactPos.ToString()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Diagnostic: samples a launched rocket's position until the entity is gone, then reports how
	// far it travelled and how long it lived.
	//
	// Distinguishes the two remaining explanations for a launch that never reports a detonation:
	// a rocket deleted at the muzzle dies within a tick or two having moved almost nowhere, while
	// one that flies its full course and vanishes on impact covers real distance first - which
	// would mean the ammo simply does not run SCR_ExplosionAmmoEffect.
	protected void TraceProjectile(IEntity projectile, int elapsedMs, vector lastPos)
	{
		const int pollMs = 100;
		const int maxMs = 10000;

		if (!projectile)
		{
			Print(string.Format("GRAD_Endgame_ExplosionTracker: rocket gone after %1ms, last seen %2",
				elapsedMs, lastPos.ToString()), LogLevel.NORMAL);
			return;
		}

		vector currentPos = projectile.GetOrigin();

		if (elapsedMs >= maxMs)
		{
			Print(string.Format("GRAD_Endgame_ExplosionTracker: rocket still alive after %1ms at %2",
				maxMs, currentPos.ToString()), LogLevel.NORMAL);
			return;
		}

		GetGame().GetCallqueue().CallLater(TraceProjectile, pollMs, false, projectile, elapsedMs + pollMs, currentPos);
	}

	//------------------------------------------------------------------------------------------------
	//! Removes and returns the oldest outstanding launch of the given prefab, or null if there is
	//! none. Oldest-first matching is correct because projectiles detonate in the order fired.
	protected GRAD_Endgame_PendingLaunch TakeMatchingLaunch(string prefabPath)
	{
		for (int i = 0; i < s_aPendingLaunches.Count(); i++)
		{
			GRAD_Endgame_PendingLaunch candidate = s_aPendingLaunches[i];
			if (candidate.prefabPath != prefabPath)
				continue;

			s_aPendingLaunches.Remove(i);
			return candidate;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	// Drops launches that never produced a detonation (rocket despawned, flew out of bounds), so
	// the pending list stays bounded over a long round.
	protected void PruneExpiredLaunches()
	{
		float cutoff = GetWorldTimeSeconds() - (LAUNCH_TIMEOUT_MS / 1000.0);

		for (int i = s_aPendingLaunches.Count() - 1; i >= 0; i--)
		{
			if (s_aPendingLaunches[i].time < cutoff)
				s_aPendingLaunches.Remove(i);
		}
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

	//------------------------------------------------------------------------------------------------
	protected float GetWorldTimeSeconds()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return 0;

		return world.GetWorldTime() / 1000.0;
	}
}

//------------------------------------------------------------------------------------------------
// A launch awaiting its matching detonation.
class GRAD_Endgame_PendingLaunch : Managed
{
	vector position;
	float time;
	string factionKey;
	string prefabPath; // matched against the detonating entity's prefab
}
