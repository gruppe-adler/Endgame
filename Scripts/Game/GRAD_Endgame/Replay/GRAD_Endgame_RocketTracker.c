//------------------------------------------------------------------------------------------------
// PORTED from BreakingContact (GRAD_BC_RocketTracker), renamed GRAD_Endgame_*. Not compiled, not run here.
[ComponentEditorProps(category: "Gruppe Adler/Endgame Replay", description: "Records AT rocket impacts for the replay system")]
class GRAD_Endgame_RocketTrackerClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
// Records where an AT rocket ends up, so the replay can animate it along its flight path and burst
// at the impact point.
//
// Attach to the rocket AMMO prefabs (e.g. Ammo_Rocket_PG22.et).
//
// Why this exists rather than relying on SCR_ExplosionAmmoEffect's invoker: the invoker is
// subscribed and confirmed working, but it never fires for these rockets. A trace showed the
// rocket flying normally - 34m in 300ms, about 114 m/s, matching the PG22's real speed - and then
// simply disappearing. So the round detonates without running the scripted ammo effect, and the
// only reliable way to learn where it landed is to ride along on the projectile itself.
//
// The launch position still comes from GRAD_Endgame_WeaponFireTracker via OnProjectileShot; this
// component supplies the other half of the pair.
class GRAD_Endgame_RocketTracker : ScriptComponent
{
	// How often the rocket's position is sampled, in ms. Flights last only a second or two, so
	// this is short - the last sample before the entity vanishes is the impact point.
	protected const int POLL_INTERVAL_MS = 50;

	// Safety cap: a rocket that somehow never resolves stops being polled.
	protected const int MAX_FLIGHT_MS = 30000;

	protected vector m_vLaunchPos;
	protected vector m_vLastPos;
	protected float m_fLaunchTime;
	protected bool m_bReported = false;
	protected int m_iElapsedMs = 0;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

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

		m_vLaunchPos = owner.GetOrigin();
		m_vLastPos = m_vLaunchPos;
		m_fLaunchTime = GetWorldTimeSeconds();

		GetGame().GetCallqueue().CallLater(Poll, POLL_INTERVAL_MS, false, owner);
	}

	//------------------------------------------------------------------------------------------------
	// Samples the rocket until it is gone. The last position held before the entity went null is
	// where it detonated.
	protected void Poll(IEntity owner)
	{
		if (!owner)
		{
			ReportImpact();
			return;
		}

		m_iElapsedMs += POLL_INTERVAL_MS;

		if (m_iElapsedMs >= MAX_FLIGHT_MS)
		{
			ReportImpact();
			return;
		}

		m_vLastPos = owner.GetOrigin();

		GetGame().GetCallqueue().CallLater(Poll, POLL_INTERVAL_MS, false, owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void ReportImpact()
	{
		if (m_bReported)
			return;

		m_bReported = true;

		// A rocket that never travelled was destroyed where it spawned rather than flying - most
		// likely deleted by safestart. Recording that as a flight would draw a marker on the
		// shooter's own position.
		// Most rockets never fly at all: they sit as ammo in inventory or vehicle cargo and are
		// eventually destroyed there. Logging each one buries the real impacts, so this is silent.
		if (vector.Distance(m_vLaunchPos, m_vLastPos) < MIN_FLIGHT_DISTANCE_M)
			return;

		GRAD_Endgame_ReplayManager replayManager = GRAD_Endgame_ReplayManager.GetInstance();
		if (!replayManager)
			return;

		replayManager.RecordExplosiveEvent(
			EGradEndgameExplosiveKind.AT,
			m_vLaunchPos,
			m_vLastPos,
			m_fLaunchTime,
			GetWorldTimeSeconds(),
			0,
			"");

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_RocketTracker: AT impact at %1 after %2m flight",
				m_vLastPos.ToString(), vector.Distance(m_vLaunchPos, m_vLastPos)), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected float GetWorldTimeSeconds()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return 0;

		return world.GetWorldTime() / 1000.0;
	}

	// Minimum distance, in metres, a rocket must cover before its endpoint counts as an impact.
	protected const float MIN_FLIGHT_DISTANCE_M = 5.0;
}
