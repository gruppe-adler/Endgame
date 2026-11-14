//------------------------------------------------------------------------------------------------
// PORTED from BreakingContact (GRAD_BC_WeaponFireTracker), renamed GRAD_Endgame_*. Not compiled, not run here.
[ComponentEditorProps(category: "Gruppe Adler/Endgame Replay", description: "Tracks weapon fire events for replay system")]
class GRAD_Endgame_WeaponFireTrackerClass : ScriptGameComponentClass
{
}

//------------------------------------------------------------------------------------------------
// Tracks weapon firing events and records projectile data for replay system
// Attach this component to character entities to automatically track all weapon fire
// Uses EventHandlerManagerComponent to hook into OnProjectileShot event
class GRAD_Endgame_WeaponFireTracker : ScriptGameComponent
{
	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		
		// Only track on server
		if (!Replication.IsServer())
			return;
		
		SetEventMask(owner, EntityEvent.INIT);
	}
	
	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		
		// Only track on server
		if (!Replication.IsServer())
			return;
		
		// Get EventHandlerManagerComponent
		EventHandlerManagerComponent eventHandlerManager = EventHandlerManagerComponent.Cast(owner.FindComponent(EventHandlerManagerComponent));
		if (!eventHandlerManager)
		{
			Print("GRAD_Endgame_WeaponFireTracker: No EventHandlerManagerComponent found on entity", LogLevel.WARNING);
			return;
		}
		
		// Register for weapon fire events
		eventHandlerManager.RegisterScriptHandler("OnProjectileShot", this, OnWeaponFired);
		
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_WeaponFireTracker: Registered for OnProjectileShot events", LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	// Called automatically when weapon fires
	protected void OnWeaponFired(int playerID, BaseWeaponComponent weapon, IEntity entity)
	{
		if (!weapon)
			return;
		
		GRAD_Endgame_ReplayManager replayManager = GRAD_Endgame_ReplayManager.GetInstance();
		if (!replayManager)
			return;
		
		// Get weapon owner for position
		IEntity weaponOwner = weapon.GetOwner();
		if (!weaponOwner)
			return;
		
		// Get firing position from weapon/character
		vector firingPos = weaponOwner.GetOrigin();
		
		// Get weapon direction (use owner's forward direction)
		vector transform[4];
		weaponOwner.GetTransform(transform);
		vector firingDir = transform[2]; // Forward vector
		
		// Get initial projectile speed
		float muzzleVelocity = weapon.GetInitialProjectileSpeed();
		if (muzzleVelocity <= 0)
			muzzleVelocity = 800.0; // Fallback
		
		// Calculate velocity vector
		vector velocity = firingDir * muzzleVelocity;
		
		// Get ammo type name from weapon type
		string ammoType = "Projectile";
		EWeaponType weaponType = weapon.GetWeaponType();
		if (weaponType == EWeaponType.WT_RIFLE)
			ammoType = "Rifle";
		else if (weaponType == EWeaponType.WT_MACHINEGUN)
			ammoType = "MG";
		else if (weaponType == EWeaponType.WT_SNIPERRIFLE)
			ammoType = "Sniper";
		else if (weaponType == EWeaponType.WT_ROCKETLAUNCHER)
			ammoType = "Rocket";
		else if (weaponType == EWeaponType.WT_HANDGUN)
			ammoType = "Pistol";
		
		// Record projectile to replay system
		replayManager.RecordProjectileFired(firingPos, velocity, ammoType);

		// AT rockets are additionally recorded as launch->impact events so the replay can animate
		// the projectile. The tracker filters out everything that is not AT-class ammo, and pairs
		// this launch with the detonation reported by SCR_ExplosionAmmoEffect.
		GRAD_Endgame_ExplosionTracker explosionTracker = GRAD_Endgame_ExplosionTracker.GetInstance();
		if (explosionTracker && entity)
			explosionTracker.NotifyProjectileLaunched(entity, GetProjectilePrefabPath(entity), GetFactionKeyOfCharacter(weaponOwner));
		
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_WeaponFireTracker: Recorded projectile - %1 at %2 with velocity %3 m/s", 
				ammoType, firingPos.ToString(), velocity.Length()), LogLevel.VERBOSE);
	}
	
	//------------------------------------------------------------------------------------------------
	// Prefab path of a launched projectile, used to classify it as AT / HE / smoke.
	protected string GetProjectilePrefabPath(IEntity projectile)
	{
		if (!projectile)
			return "";

		EntityPrefabData prefabData = projectile.GetPrefabData();
		if (!prefabData)
			return "";

		return prefabData.GetPrefabName();
	}

	//------------------------------------------------------------------------------------------------
	// Faction of whoever fired, so AT markers can be coloured by side. Empty string when unknown.
	protected string GetFactionKeyOfCharacter(IEntity character)
	{
		if (!character)
			return "";

		FactionAffiliationComponent factionComponent = FactionAffiliationComponent.Cast(character.FindComponent(FactionAffiliationComponent));
		if (!factionComponent)
			return "";

		Faction faction = factionComponent.GetAffiliatedFaction();
		if (!faction)
			return "";

		return faction.GetFactionKey();
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		// Cleanup: unregister event handler
		if (Replication.IsServer())
		{
			EventHandlerManagerComponent eventHandlerManager = EventHandlerManagerComponent.Cast(owner.FindComponent(EventHandlerManagerComponent));
			if (eventHandlerManager)
			{
				eventHandlerManager.RemoveScriptHandler("OnProjectileShot", this, OnWeaponFired);
			}
		}
		
		super.OnDelete(owner);
	}
}
