[ComponentEditorProps(category: "GRAD/Breaking Contact", description: "Manages replay recording and playback for Breaking Contact gamemode")]
class GRAD_Endgame_ReplayManagerClass : ScriptComponentClass
{
}

// Replay data structures moved to GRAD_Endgame_ReplayData.c

// Main replay manager component
class GRAD_Endgame_ReplayManager : ScriptComponent
{
	[Attribute("3.0", UIWidgets.EditBox, "Recording interval in seconds")]
	protected float m_fRecordingInterval;
	
	[Attribute("1", UIWidgets.CheckBox, "Record projectiles")]
	protected bool m_bRecordProjectiles;
	
	[Attribute("500.0", UIWidgets.EditBox, "Max projectile recording distance")]
	protected float m_fMaxProjectileDistance;
	
	// Recording state
	protected bool m_bIsRecording = false;
	protected bool m_bIsPlayingBack = false;
	protected ref GRAD_Endgame_ReplayData m_replayData;
	protected float m_fLastRecordTime = 0;
	
	// Playback state
	protected float m_fPlaybackStartTime = 0;
	protected float m_fCurrentPlaybackTime = 0;
	protected int m_iCurrentFrameIndex = 0;
	protected bool m_bPlaybackPaused = false;
	protected float m_fPlaybackSpeed = 5.0; // 1.0 = normal speed, 0.5 = half speed, 2.0 = double speed, 5.0 = 5x speed
	
	// Adaptive replay settings - max 2 minutes replay time
	protected float m_fAdaptiveSpeed = 1.0; // Calculated speed to fit in max duration
	protected float m_fMaxReplayDuration = 60.0; // 2 minutes max replay time
	
	// Projectile data pending recording
	protected ref array<ref GRAD_Endgame_ProjectileData> m_pendingProjectiles = {};

	// Latches once COALITION's safestart has actually switched on, so that "safestart has not
	// started yet" is not mistaken for "safestart is over". See IsSafestartOver().
	protected bool m_bSafestartWasActive = false;

	// CheckGameState ticks once a second, so this is a ~15s grace period before concluding that
	// safestart is simply disabled for this mission and recording should proceed anyway.
	protected int m_iSafestartWaitTicks = 0;
	protected const int SAFESTART_WAIT_MAX_TICKS = 15;

	// Real seconds a detonation stays visible on the replay map. Scaled by playback speed at use,
	// so it is perceptual duration rather than simulated duration - what matters is that a viewer
	// can see it, not that it matches how long the real blast lasted.
	protected const float DETONATION_VISIBLE_SECONDS = 1.0;

	// Tolerances for rejecting a duplicate explosive event. Generous enough to catch the same
	// grenade reported by two entities, tight enough that two players throwing smoke together
	// still register separately.
	protected const float EXPLOSIVE_DEDUPE_SECONDS = 1.0;
	protected const float EXPLOSIVE_DEDUPE_METERS = 2.0;
	
	// Tracked vehicles
	protected ref array<IEntity> m_trackedVehicles;
	
	// Vehicles that have been used by players at least once
	protected ref array<RplId> m_usedVehicleIds = {};
	
	// RPC for client communication
	protected RplComponent m_RplComponent;
	
	// Static instance for global access
	protected static GRAD_Endgame_ReplayManager s_Instance;
	
	// Loading progress tracking
	protected int m_iTotalChunksToSend = 0;
	protected int m_iChunksSent = 0;

	// First frame tracking - loading screen stays until first frame renders
	protected bool m_bFirstFrameDisplayed = false;

	// Acknowledged-playback-start state (server only). Each connected client
	// reports back when it has finished receiving replay data; each ack resets
	// a rolling cooldown so playback only begins once the room has gone quiet
	// for REPLAY_ACK_COOLDOWN_MS. The hard cap protects against acks that
	// keep arriving forever (or never arriving at all).
	protected bool m_bWaitingForAcks = false;
	protected ref array<int> m_aAckedPlayerIds = {};

	// TriggerEndscreen is scheduled from two independent replay-start flows
	// (StartActualPlayback for local/host playback, BeginReplayForAll for the
	// multi-client broadcast path) with different wait times. If both flows run
	// for the same match, TriggerEndscreen fires twice a few seconds apart, each
	// call re-running EndGameMode()/MoveAllPlayersToSpectator() and re-spawning
	// COA_Spectator/COA_SpectatorCamera for already-spectating players - this was
	// observed to corrupt spectator camera physics state badly enough to crash
	// the client on the second pass. Guard it fire-once like BeginReplayForAll.
	protected bool m_bEndscreenTriggered = false;

	const int REPLAY_ACK_COOLDOWN_MS = 3000;
	const int REPLAY_ACK_HARDCAP_MS  = 30000;
	
	//------------------------------------------------------------------------------------------------
	static GRAD_Endgame_ReplayManager GetInstance()
	{
		return s_Instance;
	}
	
	//------------------------------------------------------------------------------------------------
	// Public method to register a vehicle for replay tracking
	void RegisterTrackedVehicle(Vehicle vehicle)
	{
		if (!vehicle)
			return;

		// Wait for RplComponent to be available before registering
		WaitForVehicleRplComponent(vehicle, 0);
	}

	// Helper to wait for RplComponent initialization
	void WaitForVehicleRplComponent(Vehicle vehicle, int attempt)
	{
		if (!vehicle)
			return;

		RplComponent rpl = RplComponent.Cast(vehicle.FindComponent(RplComponent));
		if (rpl)
		{
			if (!m_trackedVehicles)
				m_trackedVehicles = new array<IEntity>();

			if (m_trackedVehicles.Find(vehicle) == -1)
			{
				m_trackedVehicles.Insert(vehicle);
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print(string.Format("GRAD_Endgame_ReplayManager: Registered vehicle for tracking: %1 (total: %2)", vehicle.GetPrefabData().GetPrefabName(), m_trackedVehicles.Count()), LogLevel.NORMAL);
			}
			else
			{
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print(string.Format("GRAD_Endgame_ReplayManager: Vehicle already tracked: %1", vehicle.GetPrefabData().GetPrefabName()), LogLevel.VERBOSE);
			}
			return;
		}

		// Retry up to 20 times (2 seconds total if called every 100ms)
		if (attempt < 20)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: Waiting for RplComponent on vehicle %1 (attempt %2)", vehicle.GetPrefabData().GetPrefabName(), attempt+1), LogLevel.VERBOSE);
			GetGame().GetCallqueue().CallLater(WaitForVehicleRplComponent, 100, false, vehicle, attempt+1);
		}
		else
		{
			Print(string.Format("GRAD_Endgame_ReplayManager: ERROR - RplComponent not found for vehicle %1 after 2s, not tracking", vehicle.GetPrefabData().GetPrefabName()), LogLevel.ERROR);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		
		// VERSION IDENTIFIER - This log will change when code updates
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: VERSION 2025-08-20-14:30 - Single-player detection enabled", LogLevel.NORMAL);
		
		// Force initialization on both server and client
		if (!s_Instance)
		{
			s_Instance = this;
			string isServer = "Client";
			if (Replication.IsServer()) { isServer = "Server"; }
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: Force initialized via EOnInit on %1", isServer), LogLevel.NORMAL);
		}
		
		SetEventMask(owner, EntityEvent.INIT);
	}
	
	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		// Clean up all CallLater callbacks
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(CheckGameState);
			GetGame().GetCallqueue().Remove(RecordFrame);
			GetGame().GetCallqueue().Remove(UpdatePlayback);
			GetGame().GetCallqueue().Remove(InitializeReplaySystem);
			GetGame().GetCallqueue().Remove(WaitForVehicleRplComponent);
			GetGame().GetCallqueue().Remove(TriggerEndscreen);
			GetGame().GetCallqueue().Remove(SendFrameChunksWithDelay);
			GetGame().GetCallqueue().Remove(SendReplayComplete);
			GetGame().GetCallqueue().Remove(StartPlaybackForAllClients);
			GetGame().GetCallqueue().Remove(OpenMapAndStartLocalPlayback);
			GetGame().GetCallqueue().Remove(StartActualPlayback);
			GetGame().GetCallqueue().Remove(StartClientReplayPlayback);
			GetGame().GetCallqueue().Remove(WaitForReplayMapAndStartPlayback);
			GetGame().GetCallqueue().Remove(WaitForReplayMapAndStartClientPlayback);
			GetGame().GetCallqueue().Remove(BeginReplayForAll);
			GetGame().GetCallqueue().Remove(BeginReplayForAll_HardCap);
		}

		// Unsubscribe from vehicle spawn events
		if (GetGame() && GetGame().GetGameMode())
		{
			SCR_CampaignBuildingManagerComponent buildingManager = SCR_CampaignBuildingManagerComponent.Cast(GetGame().GetGameMode().FindComponent(SCR_CampaignBuildingManagerComponent));
			if (buildingManager)
				buildingManager.GetOnEntitySpawnedByProvider().Remove(OnVehicleSpawned);
		}

		// Clear replay data
		m_replayData = null;
		if (m_trackedVehicles)
			m_trackedVehicles.Clear();
		m_usedVehicleIds.Clear();
		m_pendingProjectiles.Clear();
		if (m_aAckedPlayerIds)
			m_aAckedPlayerIds.Clear();
		m_bWaitingForAcks = false;

		if (s_Instance == this)
			s_Instance = null;

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		// Always initialize on both server and client
		super.OnPostInit(owner);
		
		s_Instance = this;
		
		m_trackedVehicles = new array<IEntity>();
		
		string isServer = "Client";
		if (Replication.IsServer()) { isServer = "Server"; }
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Instance created and initialized on %1", isServer), LogLevel.NORMAL);
		
		m_RplComponent = RplComponent.Cast(owner.FindComponent(RplComponent));
		if (!m_RplComponent)
		{
			Print(string.Format("GRAD_Endgame_ReplayManager: Warning - No RplComponent found on %1", isServer), LogLevel.WARNING);
		}
		else
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: RplComponent found on %1", isServer), LogLevel.NORMAL);
		}
		
		// Only record on server
		if (Replication.IsServer())
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: Running on server, setting up recording", LogLevel.NORMAL);
			
			// Delay initialization to ensure world is ready
			GetGame().GetCallqueue().CallLater(InitializeReplaySystem, 100, false);
		}
		else
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: Running on client, ready to receive RPCs", LogLevel.NORMAL);
			// Initialize client-side replay manager to receive RPCs
			s_Instance = this;
			
			// Add debug info
			if (m_RplComponent)
			{
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print("GRAD_Endgame_ReplayManager: Client has RplComponent, RPCs should work", LogLevel.NORMAL);
			}
			else
			{
				Print("GRAD_Endgame_ReplayManager: Client missing RplComponent, RPCs will not work!", LogLevel.ERROR);
			}
		}
	}
	
	//------------------------------------------------------------------------------------------------
	void InitializeReplaySystem()
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Initializing replay system", LogLevel.NORMAL);
		
		// Initialize replay data now that world should be ready
		m_replayData = GRAD_Endgame_ReplayData.Create();
		
		// Watch the Endgame phase: record while a match runs, play back when it ends.
		GetGame().GetCallqueue().CallLater(CheckGameState, 1000, true);

		SCR_CampaignBuildingManagerComponent buildingManager = SCR_CampaignBuildingManagerComponent.Cast(GetGame().GetGameMode().FindComponent(SCR_CampaignBuildingManagerComponent));
		if (buildingManager)
		{
			buildingManager.GetOnEntitySpawnedByProvider().Insert(OnVehicleSpawned);
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: Hooked into vehicle spawn event", LogLevel.NORMAL);
		}
		else
		{
			Print("GRAD_Endgame_ReplayManager: Building manager not found, cannot track spawned vehicles", LogLevel.WARNING);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	void OnVehicleSpawned(int prefabID, SCR_EditableEntityComponent editableEntity, int playerId, SCR_CampaignBuildingProviderComponent provider)
	{
		if (!editableEntity)
			return;
	
		IEntity spawnedEntity = editableEntity.GetOwner();
		if (!spawnedEntity)
			return;

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Entity spawned: %1", spawnedEntity.GetPrefabData().GetPrefabName()), LogLevel.NORMAL);

		Vehicle vehicle = Vehicle.Cast(spawnedEntity);
		if (vehicle)
		{
			if (!m_trackedVehicles)
				m_trackedVehicles = new array<IEntity>();
			
			if (m_trackedVehicles.Find(vehicle) == -1)
			{
				m_trackedVehicles.Insert(vehicle);
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print(string.Format("GRAD_Endgame_ReplayManager: Tracked new vehicle: %1 (total: %2)", vehicle.GetPrefabData().GetPrefabName(), m_trackedVehicles.Count()), LogLevel.NORMAL);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	// Called by grad-traffic when a civilian traffic vehicle is spawned
	// Parameter is IEntity to match SCR_TrafficEvents.OnTrafficVehicleSpawned invoker signature
	void OnTrafficVehicleSpawned(IEntity entity)
	{
		Vehicle vehicle = Vehicle.Cast(entity);
		if (!vehicle)
			return;

		RegisterTrackedVehicle(vehicle);
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Registered grad-traffic vehicle: %1", vehicle.GetPrefabData().GetPrefabName()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Called by grad-traffic when a civilian traffic vehicle is despawned
	// Parameter is IEntity to match SCR_TrafficEvents.OnTrafficVehicleDespawned invoker signature
	void OnTrafficVehicleDespawned(IEntity entity)
	{
		Vehicle vehicle = Vehicle.Cast(entity);
		if (!vehicle || !m_trackedVehicles)
			return;

		int idx = m_trackedVehicles.Find(vehicle);
		if (idx != -1)
		{
			m_trackedVehicles.Remove(idx);
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: Unregistered grad-traffic vehicle (total: %1)", m_trackedVehicles.Count()), LogLevel.NORMAL);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Whether COALITION's safestart has finished, i.e. weapons are live.
	//!
	//! Returns true when COALITION is absent or never enabled safestart, so BC still records
	//! normally standalone rather than never starting at all.
	protected bool IsSafestartOver()
	{
		COA_SafestartManager safestartManager = COA_SafestartManager.GetInstance();
		if (!safestartManager)
			return true; // no COALITION safestart in this session - nothing to wait for

		// Safestart must actually have begun before its ending means anything. COA enables it a
		// moment after the game state flips, so an unlatched check would see "not active yet",
		// read that as "already over", and start recording during the freeze after all.
		if (!m_bSafestartWasActive)
		{
			if (safestartManager.GetSafestartStatus())
			{
				m_bSafestartWasActive = true;
				return false;
			}

			// Safestart may simply be switched off for this mission, in which case it will never
			// latch. Waiting forever would mean never recording at all, so give up after a grace
			// period and treat it as "no safestart to wait for".
			m_iSafestartWaitTicks++;
			if (m_iSafestartWaitTicks >= SAFESTART_WAIT_MAX_TICKS)
			{
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print("GRAD_Endgame_ReplayManager: safestart never became active, recording without waiting", LogLevel.NORMAL);
				return true;
			}

			return false;
		}

		return !safestartManager.GetSafestartStatus();
	}

	//------------------------------------------------------------------------------------------------
	void CheckGameState()
	{
		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (!gameMode)
			return;

		GRAD_Endgame_EPhase currentPhase = gameMode.GetPhase();
		bool matchRunning = currentPhase == GRAD_Endgame_EPhase.FOB || currentPhase == GRAD_Endgame_EPhase.DOCUMENTS || currentPhase == GRAD_Endgame_EPhase.RADIO;

		// Record from the first phase on, once COALITION's safestart has lifted.
		if (!m_bIsRecording && !m_bPlaybackScheduled && matchRunning && IsSafestartOver())
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: match running and safestart over, starting recording", LogLevel.NORMAL);
			StartRecording();
		}

		// The match is over: stop recording and play the replay, then the after-action screen.
		if (currentPhase == GRAD_Endgame_EPhase.ENDED && !m_bPlaybackScheduled)
		{
			m_bPlaybackScheduled = true;
			if (m_bIsRecording)
				StopRecording();
			GetGame().GetCallqueue().Remove(CheckGameState);
			GetGame().GetCallqueue().CallLater(StartPlaybackForAllClients, 500, false);
		}
	}

	//! True once the end of the match has been seen and the replay (or its absence) is being handled.
	protected bool m_bPlaybackScheduled;

	//! The game mode asks: will this manager move COALITION on to the after-action screen itself?
	bool HandlesAfterAction()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		return Replication.IsServer() && cfg && cfg.GetReplayEnabled();
	}

	//------------------------------------------------------------------------------------------------
	void StartRecording()
	{
		if (m_bIsRecording || !Replication.IsServer())
			return;
			
		// Ensure replay data is initialized
		if (!m_replayData)
		{
			Print("GRAD_Endgame_ReplayManager: Replay data not initialized, creating now", LogLevel.WARNING);
			m_replayData = GRAD_Endgame_ReplayData.Create();
		}
		
		// Ensure world is available
		BaseWorld world = GetGame().GetWorld();
		if (!world)
		{
			Print("GRAD_Endgame_ReplayManager: World not available, cannot start recording", LogLevel.ERROR);
			return;
		}
			
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Starting replay recording", LogLevel.NORMAL);
		m_bIsRecording = true;
		m_fLastRecordTime = world.GetWorldTime() / 1000.0; // Convert milliseconds to seconds
		
		// Start recording loop
		GetGame().GetCallqueue().CallLater(RecordFrame, m_fRecordingInterval * 1000, true);
	}
	
	//------------------------------------------------------------------------------------------------
	void StopRecording()
	{
		if (!m_bIsRecording)
			return;
			
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Stopping replay recording", LogLevel.NORMAL);
		m_bIsRecording = false;
		GetGame().GetCallqueue().Remove(RecordFrame);
		GetGame().GetCallqueue().Remove(CheckGameState);
		
		// Finalize replay data
		if (m_replayData && m_replayData.frames.Count() > 0)
		{
			// Remove empty frames (frames with no players/markers)
			CleanupEmptyFrames();

			if (m_replayData.frames.Count() > 0)
			{
				// Update startTime to first remaining frame to eliminate pre-game time gap
				// (startTime was set at component init, which can be minutes before GAME phase)
				GRAD_Endgame_ReplayFrame firstFrame = m_replayData.frames[0];
				float oldStartTime = m_replayData.startTime;
				m_replayData.startTime = firstFrame.timestamp;

				GRAD_Endgame_ReplayFrame lastFrame = m_replayData.frames[m_replayData.frames.Count() - 1];
				m_replayData.totalDuration = lastFrame.timestamp - m_replayData.startTime;

				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print(string.Format("GRAD_Endgame_ReplayManager: Recorded %1 frames over %2 seconds (startTime adjusted from %.2f to %.2f)",
						m_replayData.frames.Count(), m_replayData.totalDuration, oldStartTime, m_replayData.startTime), LogLevel.NORMAL);
			}
		}
	}
	
	//------------------------------------------------------------------------------------------------
	void CleanupEmptyFrames()
	{
		if (!m_replayData || m_replayData.frames.Count() == 0)
			return;
		
		int originalCount = m_replayData.frames.Count();
		array<ref GRAD_Endgame_ReplayFrame> cleanedFrames = new array<ref GRAD_Endgame_ReplayFrame>();
		
		foreach (GRAD_Endgame_ReplayFrame frame : m_replayData.frames)
		{
			// Keep frame if it has any data
			if (frame.players.Count() > 0 || frame.projectiles.Count() > 0 || frame.transmissions.Count() > 0 || frame.vehicles.Count() > 0)
			{
				cleanedFrames.Insert(frame);
			}
		}
		
		// Replace with cleaned frames
		m_replayData.frames = cleanedFrames;
		
		int removedCount = originalCount - cleanedFrames.Count();
		if (removedCount > 0)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: Removed %1 empty frames (%2 -> %3)", 
					removedCount, originalCount, cleanedFrames.Count()), LogLevel.NORMAL);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	void RecordFrame()
	{
		if (!m_bIsRecording || !Replication.IsServer())
			return;
			
		// Ensure replay data exists
		if (!m_replayData)
		{
			Print("GRAD_Endgame_ReplayManager: Replay data missing during recording, stopping", LogLevel.ERROR);
			StopRecording();
			return;
		}
		
		BaseWorld world = GetGame().GetWorld();
		if (!world)
		{
			Print("GRAD_Endgame_ReplayManager: World not available during recording", LogLevel.ERROR);
			return;
		}
		
		float currentTime = world.GetWorldTime() / 1000.0; // Convert milliseconds to seconds
		GRAD_Endgame_ReplayFrame frame = GRAD_Endgame_ReplayFrame.Create(currentTime);
		
		// Record player positions
		RecordPlayers(frame);
		RecordGarrisons(frame);
		
		RecordTrackedVehicles(frame);
		
		// Record projectiles if enabled
		if (m_bRecordProjectiles)
		{
			RecordProjectiles(frame);
		}
		
		// Record transmission states
		RecordTransmissions(frame);
		
		// Add frame to replay data
		m_replayData.frames.Insert(frame);
		m_fLastRecordTime = currentTime;
	}
	
	//------------------------------------------------------------------------------------------------
	void RecordTrackedVehicles(GRAD_Endgame_ReplayFrame frame)
	{
		if (!m_trackedVehicles)
			return;
			
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Recording %1 tracked vehicles", m_trackedVehicles.Count()), LogLevel.NORMAL);
	
		foreach (IEntity entity : m_trackedVehicles)
		{
			if (!entity)
				continue;
			
			Vehicle vehicle = Vehicle.Cast(entity);
			if (!vehicle)
				continue;
	
			// Record all vehicles - no exceptions
	
			// Now we have a vehicle. Record it.
			vector position = vehicle.GetOrigin();
			vector angles = vehicle.GetYawPitchRoll();
			string vehicleType = vehicle.GetPrefabData().GetPrefabName();
			
			FactionAffiliationComponent factionComponent = FactionAffiliationComponent.Cast(vehicle.FindComponent(FactionAffiliationComponent));
			string factionKey = "";
			if (factionComponent && factionComponent.GetAffiliatedFaction())
			{
				factionKey = factionComponent.GetAffiliatedFaction().GetFactionKey();
			}
			else
			{
				factionKey = "Empty";
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print("GRAD_Endgame_ReplayManager: Vehicle has no FactionAffiliationComponent or AffiliatedFaction, using 'Empty'", LogLevel.NORMAL);
			}
				
			RplComponent rpl = RplComponent.Cast(vehicle.FindComponent(RplComponent));
			if (!rpl)
				continue;

			// Check if vehicle is empty
			BaseCompartmentManagerComponent compartmentManager = BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(BaseCompartmentManagerComponent));
			bool isEmpty = true;
			string occupantFactionKey;
			if (compartmentManager)
			{
				array<BaseCompartmentSlot> compartments = {};
				compartmentManager.GetCompartments(compartments);
				isEmpty = true;
				foreach (BaseCompartmentSlot slot : compartments)
				{
					IEntity occupant = slot.GetOccupant();
					if (occupant)
					{
						isEmpty = false;
						SCR_ChimeraCharacter occupantCharacter = SCR_ChimeraCharacter.Cast(occupant);
						if (occupantCharacter)
							occupantFactionKey = occupantCharacter.GetFactionKey();
						break;
					}
				}
			}

			// A vehicle with no faction of its own (an FIA patrol UAZ) takes the faction of whoever
			// sits in it. Otherwise it is recorded as "Empty" and shows as an empty (yellow) vehicle
			// while it is driving.
			if (!isEmpty && (factionKey == "Empty" || factionKey.IsEmpty()) && !occupantFactionKey.IsEmpty())
				factionKey = occupantFactionKey;

			// Track vehicles that have ever been used by players
			RplId vehicleRplId = rpl.Id();
			bool wasUsed = (m_usedVehicleIds.Find(vehicleRplId) != -1);
			if (!isEmpty && !wasUsed)
			{
				m_usedVehicleIds.Insert(vehicleRplId);
				wasUsed = true;
			}

			// Print(string.Format("GRAD_Endgame_ReplayManager: Creating snapshot for vehicle: %1", vehicleType), LogLevel.NORMAL);
			
			GRAD_Endgame_VehicleSnapshot snapshot = GRAD_Endgame_VehicleSnapshot.Create(
				vehicleRplId,
				vehicleType,
				factionKey,
				position,
				angles,
				isEmpty,
				wasUsed
			);
			frame.vehicles.Insert(snapshot);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	//! The FIA garrison soldiers, as player snapshots with negative ids (BreakingContact records
	//! players only). They take the neutral icon in the map layer.
	void RecordGarrisons(GRAD_Endgame_ReplayFrame frame)
	{
		int aiId = 0;
		array<IEntity> soldiers = {};
		foreach (GRAD_Endgame_GarrisonComponent garrison : GRAD_Endgame_GarrisonComponent.GetAll())
		{
			soldiers.Clear();
			garrison.GetSoldiers(soldiers);
			foreach (IEntity soldier : soldiers)
			{
				SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(soldier);
				if (!character || character.GetCharacterController().IsDead())
					continue;

				aiId--;
				RplId noVehicle;
				frame.players.Insert(GRAD_Endgame_PlayerSnapshot.Create(aiId, "FIA", "FIA", soldier.GetOrigin(), soldier.GetYawPitchRoll(), true, false, "", "Rifleman", noVehicle));
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	void RecordPlayers(GRAD_Endgame_ReplayFrame frame)
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return;
			
		array<int> playerIds = {};
		playerManager.GetPlayers(playerIds);
		
		foreach (int playerId : playerIds)
		{
			IEntity controlledEntity = playerManager.GetPlayerControlledEntity(playerId);
			if (!controlledEntity)
				continue;

			SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(controlledEntity);
			if (!character)
				continue;
			
			string playerName = playerManager.GetPlayerName(playerId);
			string factionKey = character.GetFactionKey();
			vector position = controlledEntity.GetOrigin();
			vector angles = controlledEntity.GetYawPitchRoll();
			bool isAlive = !character.GetCharacterController().IsDead();

			// COALITION parks a spectator on a dummy character far above the map (40, 100000, 0).
			// That is not a position on the replay map.
			if (position[1] > 20000)
				continue;

			// vehicle info
			bool isInVehicle = character.IsInVehicle();
			string vehicleType = "";
			RplId vehicleId;
			if (isInVehicle)
			{
				Vehicle vehicle = Vehicle.Cast(character.GetParent());
				if (vehicle)
				{
					vehicleType = vehicle.GetPrefabData().GetPrefabName();
					RplComponent rpl = RplComponent.Cast(vehicle.FindComponent(RplComponent));
					if (rpl)
						vehicleId = rpl.Id();
				}
			}
			
			// Determine unit role based on equipment
			string unitRole = DetermineUnitRole(character);
			
			GRAD_Endgame_PlayerSnapshot snapshot = GRAD_Endgame_PlayerSnapshot.Create(
				playerId, playerName, factionKey, position, angles, isAlive, isInVehicle, vehicleType, unitRole, vehicleId
			);

			// Debug: Log vehicle state for each player (first 20 only)
			static int vehicleRecordDebugCount = 0;
			vehicleRecordDebugCount++;
			if (vehicleRecordDebugCount <= 20)
			{
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print(string.Format(
						"GRAD_Endgame_ReplayManager: PlayerRecord id=%1 name=%2 isInVehicle=%3 vehicleType='%4' unitRole='%5'",
						playerId, playerName, isInVehicle, vehicleType, unitRole
					), LogLevel.NORMAL);
			}
			// Debug: Log recording positions and angles for first few frames
			static int recordLogCount = 0;
			recordLogCount++;
			if (recordLogCount <= 10)
			{
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print(string.Format("GRAD_Endgame_ReplayManager: Recording player %1 (%2) at position [%3, %4, %5], yaw=%6°", 
						playerId, playerName, position[0], position[1], position[2], angles[0]));
			}
			
			frame.players.Insert(snapshot);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Determine unit role using SCR_ECharacterTypeUI from SCR_CharacterUIComponent
	string DetermineUnitRole(SCR_ChimeraCharacter character)
	{
		if (!character)
			return "Rifleman";

		SCR_CharacterUIComponent charUI = SCR_CharacterUIComponent.Cast(character.FindComponent(SCR_CharacterUIComponent));
		if (charUI)
		{
			SCR_ECharacterTypeUI typeUI = charUI.GetCharacterTypes();
			switch (typeUI)
			{
				case SCR_ECharacterTypeUI.RIFLEMAN:
					return "Rifleman";
				case SCR_ECharacterTypeUI.MEDIC:
					return "Medic";
				case SCR_ECharacterTypeUI.ENGINEER:
					return "Engineer";
				case SCR_ECharacterTypeUI.GRENADIER:
					return "Grenadier";
				case SCR_ECharacterTypeUI.MACHINEGUNNER:
					return "MachineGunner";
				case SCR_ECharacterTypeUI.ANTI_TANK:
					return "AntiTank";
				case SCR_ECharacterTypeUI.SHARSHOOTER:
					return "Sharpshooter";
				case SCR_ECharacterTypeUI.PLATOON_SERGEANT:
					return "TeamLeader";
				case SCR_ECharacterTypeUI.CREW_MAN:
					return "Crew";
				case SCR_ECharacterTypeUI.SAPPER:
					return "ExplosiveSpecialist";
				case SCR_ECharacterTypeUI.OFFICER:
					return "Squadleader";
				case SCR_ECharacterTypeUI.PLATOON_LEADER:
					return "Squadleader";
				default:
					return "Rifleman";
			}
		}

		// Fallback if no UI component or unknown type
		return "Rifleman";
	}
	
	//------------------------------------------------------------------------------------------------
	void RecordProjectiles(GRAD_Endgame_ReplayFrame frame)
	{
		// Process pending projectiles fired since last frame
		for (int i = 0; i < m_pendingProjectiles.Count(); i++)
		{
			GRAD_Endgame_ProjectileData projData = m_pendingProjectiles[i];
			
			// Calculate projectile position at current frame time using ballistics
			float deltaTime = frame.timestamp - projData.fireTime;
			if (deltaTime < 0) deltaTime = 0; // Safety check
			
			// Calculate current position and velocity
			vector currentPos = projData.position + (projData.velocity * deltaTime);
			currentPos[1] = currentPos[1] - (9.81 * deltaTime * deltaTime * 0.5);
			
			vector currentVel = projData.velocity;
			currentVel[1] = currentVel[1] - (9.81 * deltaTime);
			
			// Create projectile snapshot with firing position and current/impact position
			GRAD_Endgame_ProjectileSnapshot snapshot = GRAD_Endgame_ProjectileSnapshot.Create(
				projData.ammoType, 
				projData.position, // firing position
				currentPos, // current/impact position
				currentVel,
				10.0 - deltaTime // Approximate TTL
			);
			
			frame.projectiles.Insert(snapshot);
		}
		
		// Clear pending projectiles after processing
		m_pendingProjectiles.Clear();
		
	}
	
	//------------------------------------------------------------------------------------------------
	void RecordTransmissions(GRAD_Endgame_ReplayFrame frame)
	{
		// The radio sites of this match; progress is the fraction of the transmission done.
		foreach (GRAD_Endgame_RadioSiteComponent site : GRAD_Endgame_RadioSiteComponent.GetAll())
		{
			if (!site || !site.IsSiteActive())
				continue;

			IEntity owner = site.GetOwner();
			if (!owner)
				continue;

			frame.transmissions.Insert(GRAD_Endgame_TransmissionSnapshot.Create(owner.GetOrigin(), site.GetState(), site.GetProgressFraction()));
		}
	}

	//------------------------------------------------------------------------------------------------
	void StartPlaybackForAllClients()
	{
		if (!Replication.IsServer())
			return;
			
		if (!m_replayData || m_replayData.frames.Count() == 0)
		{
			Print("GRAD_Endgame_ReplayManager: No replay data to send, going straight to the after-action screen", LogLevel.WARNING);
			TriggerEndscreen();
			return;
		}
		
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Starting replay transmission, %1 frames", m_replayData.frames.Count()), LogLevel.NORMAL);
	
	// Check if we have a local human player (absent on dedicated server).
	// GetPlayerController() can return a non-null server object on dedicated,
	// so use GetLocalPlayerId() which returns 0 when there is no local player.
	int localPlayerId = SCR_PlayerController.GetLocalPlayerId();
	bool isDedicatedServer = (localPlayerId <= 0);

	if (GRAD_Endgame_ReplayDebug.IsDebugMode())
		PrintFormat("GRAD_Endgame_ReplayManager: LocalPlayerId=%1, IsDedicated=%2", localPlayerId, isDedicatedServer);
	
	if (!isDedicatedServer)
	{
		// We have a local player - use direct local playback
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Local player detected, starting direct local playback", LogLevel.NORMAL);
		StartLocalReplayPlayback();
	}
	else
	{
		// No local player - dedicated server - use RPC to send replay to clients
			
			// Check if we have RPC component for multiplayer
			if (!m_RplComponent)
			{
				Print("GRAD_Endgame_ReplayManager: No RPC component available for transmission", LogLevel.ERROR);
				return;
			}
			
			// Send basic replay info first
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: Sending replay initialization RPC", LogLevel.NORMAL);
			Rpc(RpcAsk_StartReplayPlayback, m_replayData.totalDuration, m_replayData.missionName, m_replayData.mapName, m_replayData.startTime);
			
		// Send frames in chunks to avoid network limits with delays
		const int chunkSize = 30;
		m_iTotalChunksToSend = Math.Ceil(m_replayData.frames.Count() / 30.0);
		m_iChunksSent = 0;
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Starting chunked replay data transmission - %1 total chunks", m_iTotalChunksToSend), LogLevel.NORMAL);
		SendFrameChunksWithDelay(0);
		// NOTE: Endscreen is now scheduled in SendReplayComplete() after all data is sent,
		// so clients have time to receive data and initialize before the timer starts.
		}
	}

//------------------------------------------------------------------------------------------------
void StartLocalReplayPlayback()
{
	if (GRAD_Endgame_ReplayDebug.IsDebugMode())
		Print("GRAD_Endgame_ReplayManager: ========== Starting local single-player replay playback ==========", LogLevel.NORMAL);
		
		if (!m_replayData)
		{
			Print("GRAD_Endgame_ReplayManager: CRITICAL ERROR - No replay data available!", LogLevel.ERROR);
			return;
		}
		
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Replay has %1 frames, duration: %.2f seconds", 
				m_replayData.frames.Count(), m_replayData.totalDuration), LogLevel.NORMAL);
		
		// Calculate adaptive playback speed to fit within 2 minutes
		CalculateAdaptiveSpeed();

		// Show loading text in gamestate HUD first (map opens later after loading is done)
		GRAD_Endgame_Gamestate gamestateDisplay = FindGamestateDisplay();
		if (gamestateDisplay)
		{
			ShowOutcomeHeadline(gamestateDisplay);
			gamestateDisplay.ShowPersistentText("Preparing replay...");
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: Showing replay preparation text in gamestate HUD", LogLevel.NORMAL);
		}

		// Set up VoN for cross-faction voice
		if (Replication.IsServer())
		{
			SetAllPlayersToGlobalVoN();
		}

		// Initialize playback state
		BaseWorld world = GetGame().GetWorld();
		if (world)
		{
			m_fPlaybackStartTime = world.GetWorldTime() / 1000.0;
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: Playback start time set to %.2f", m_fPlaybackStartTime), LogLevel.NORMAL);
		}
		else
		{
			Print("GRAD_Endgame_ReplayManager: WARNING - World not available yet, will initialize timing later", LogLevel.WARNING);
			m_fPlaybackStartTime = 0;
		}

		m_fCurrentPlaybackTime = 0;
		m_iCurrentFrameIndex = 0;
		m_bPlaybackPaused = false;
		m_bFirstFrameDisplayed = false;

		// Delay opening the map so the gamestate loading screen is visible first
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Scheduling map open and playback start in 500ms", LogLevel.NORMAL);
		GetGame().GetCallqueue().CallLater(OpenMapAndStartLocalPlayback, 500, false);

		// NOTE: Endscreen timer is now scheduled in StartActualPlayback() when playback truly begins

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Local playback initialization complete", LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	void StartActualPlayback()
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: ===== StartActualPlayback CALLED =====", LogLevel.NORMAL);
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Starting actual playback sequence", LogLevel.NORMAL);
		
		// Verify world is available
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Checking if world is available...", LogLevel.NORMAL);
		BaseWorld world = GetGame().GetWorld();
		if (!world)
		{
			Print("GRAD_Endgame_ReplayManager: World not available for playback, retrying in 500ms", LogLevel.WARNING);
			GetGame().GetCallqueue().CallLater(StartActualPlayback, 500, false);
			return;
		}
		
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: World is available, proceeding...", LogLevel.NORMAL);
		
		// Reset timing
		m_fPlaybackStartTime = world.GetWorldTime() / 1000.0;
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Playback start time set to: %.2f", m_fPlaybackStartTime), LogLevel.NORMAL);
		
		// Enable playback flag - CRITICAL for UpdatePlayback to run
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Setting m_bIsPlayingBack to TRUE", LogLevel.NORMAL);
		m_bIsPlayingBack = true;
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: m_bIsPlayingBack is now: %1", m_bIsPlayingBack), LogLevel.NORMAL);
		
		// Skip empty frames at start
		SkipEmptyFrames();
		
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: About to start playback loop", LogLevel.NORMAL);
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Starting playback loop", LogLevel.NORMAL);
		
		// Start playback loop
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Calling GetGame().GetCallqueue().CallLater(UpdatePlayback, 100, true)", LogLevel.NORMAL);
		GetGame().GetCallqueue().CallLater(UpdatePlayback, 100, true);
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: CallLater executed successfully", LogLevel.NORMAL);

		// Schedule endscreen now that playback has truly started
		if (m_replayData)
		{
			float replayDuration = m_replayData.totalDuration;
			float effectiveDuration = replayDuration / m_fPlaybackSpeed;
			float waitTime = (effectiveDuration + 2.0) * 1000;
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: Scheduling endscreen in %.1f seconds (%.0fms)", waitTime / 1000, waitTime), LogLevel.NORMAL);
			GetGame().GetCallqueue().CallLater(TriggerEndscreen, waitTime, false);
		}

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: ===== StartActualPlayback COMPLETE =====", LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	// Called after loading completes in local/singleplayer mode to open map and start playback
	void OpenMapAndStartLocalPlayback()
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Opening map and starting local playback", LogLevel.NORMAL);

		// Keep loading text visible - it will be hidden when first frame renders
		// Open the map
		OpenMapForLocalPlayback();

		// Wait for map + replay layer to be ready before starting playback
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Waiting for map and replay layer to be ready...", LogLevel.NORMAL);
		WaitForReplayMapAndStartPlayback(0);
	}

	//------------------------------------------------------------------------------------------------
	void OpenMapForLocalPlayback()
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Setting up debriefing screen for replay", LogLevel.NORMAL);

		// Clear all live markers before starting replay
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Clearing live transmission and player markers", LogLevel.NORMAL);

		// Clear live transmission markers
		SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
		if (mapEntity)
		{
			GRAD_Endgame_MapMarkerManager markerMgr = GRAD_Endgame_MapMarkerManager.Cast(mapEntity.GetMapModule(GRAD_Endgame_MapMarkerManager));
			if (markerMgr)
			{
				markerMgr.SetReplayMode(true);
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print("GRAD_Endgame_ReplayManager: Enabled replay mode on marker manager", LogLevel.NORMAL);
			}
		}

		// Open map fullscreen with replay config so GRAD_Endgame_ReplayMapLayer is loaded
		if (mapEntity)
		{
			const ResourceName replayMapConfig = "{1B8AC767E06A0ACD}Configs/Map/MapFullscreen.conf";
			// If spectator menu exists (dedicated/listen spectator UI), route through it
			if (COA_SpectatorMenu.s_EndgameSpectatorMenu)
			{
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print("GRAD_Endgame_ReplayManager: Routing local map open through COA_SpectatorMenu.OpenMapWithConfig", LogLevel.NORMAL);
				COA_SpectatorMenu.s_EndgameSpectatorMenu.OpenMapWithConfig(replayMapConfig);
			}
			else
			{
				// No spectator menu (a living player): open the vanilla map menu. It activates the map
				// input (pan, zoom) and hides the HUD; our layer is part of its config (MapFullscreen.conf).
				GetGame().GetMenuManager().OpenMenu(ChimeraMenuPreset.MapMenu);
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print("GRAD_Endgame_ReplayManager: Map opened with replay config", LogLevel.NORMAL);
			}
		}
		else
		{
			// mapEntity not available at this point; WaitForReplayMapAndStartPlayback will
			// poll until the map entity appears after the map menu opens naturally.
			GetGame().GetMenuManager().OpenMenu(ChimeraMenuPreset.MapMenu);
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: Map entity not ready, fell back to vanilla MapMenu", LogLevel.NORMAL);
		}

		// Map readiness is now gated by WaitForReplayMapAndStartPlayback() called from the caller
	}
	
	//------------------------------------------------------------------------------------------------
	// Polls until map entity + replay layer are ready, then starts playback
	void WaitForReplayMapAndStartPlayback(int attempt)
	{
		const int maxAttempts = 50; // 50 * 200ms = 10 seconds max

		SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
		if (!mapEntity)
		{
			if (attempt < maxAttempts)
			{
				Print("GRAD_Endgame_ReplayManager: Map entity not ready yet, retrying...", LogLevel.WARNING);
				GetGame().GetCallqueue().CallLater(WaitForReplayMapAndStartPlayback, 200, false, attempt + 1);
				return;
			}
			Print("GRAD_Endgame_ReplayManager: Map entity not ready after max retries, forcing playback start", LogLevel.ERROR);
		}

		GRAD_Endgame_ReplayMapLayer replayLayer;
		if (mapEntity)
		{
			replayLayer = GRAD_Endgame_ReplayMapLayer.Cast(mapEntity.GetMapModule(GRAD_Endgame_ReplayMapLayer));
		}

		if (!replayLayer && attempt < maxAttempts)
		{
			Print("GRAD_Endgame_ReplayManager: Replay map layer not found yet, retrying...", LogLevel.WARNING);
			GetGame().GetCallqueue().CallLater(WaitForReplayMapAndStartPlayback, 200, false, attempt + 1);
			return;
		}

		if (!replayLayer)
		{
			Print("GRAD_Endgame_ReplayManager: Replay map layer not ready after max retries, forcing playback start", LogLevel.ERROR);
		}

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Map and replay layer verified ready after %1 attempts", attempt), LogLevel.NORMAL);

		// Set replay mode on the layer BEFORE first frame arrives
		if (replayLayer)
		{
			replayLayer.SetReplayMode(true);
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: Enabled replay mode on map layer", LogLevel.NORMAL);
		}

		// Now start actual playback
		StartActualPlayback();
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	void RpcAsk_SetupReplayVoN()
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Server received VoN setup request", LogLevel.NORMAL);
		SetAllPlayersToGlobalVoN();
	}

	//------------------------------------------------------------------------------------------------
	// Client signals "I have all chunks and am ready to play". Each new ack
	// resets the rolling cooldown so the server starts playback only after
	// the room has been quiet for REPLAY_ACK_COOLDOWN_MS.
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_Server_ReplayLoaded(int callerPlayerId)
	{
		if (!Replication.IsServer())
			return;

		if (!m_bWaitingForAcks)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: Late/spurious ack from player %1 ignored", callerPlayerId), LogLevel.NORMAL);
			return;
		}

		if (m_aAckedPlayerIds.Find(callerPlayerId) != -1)
			return;

		m_aAckedPlayerIds.Insert(callerPlayerId);

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Ack received from player %1 (total acks: %2). Resetting %3ms cooldown.",
				callerPlayerId, m_aAckedPlayerIds.Count(), REPLAY_ACK_COOLDOWN_MS), LogLevel.NORMAL);

		// Reschedule only the cooldown timer; the hard-cap continues unaffected.
		GetGame().GetCallqueue().Remove(BeginReplayForAll);
		GetGame().GetCallqueue().CallLater(BeginReplayForAll, REPLAY_ACK_COOLDOWN_MS, false);
	}
	
	//------------------------------------------------------------------------------------------------
	// NOTE: PSCore's PS_VoNRoomsManager had a simple MoveToRoom(playerId, faction, room) call for
	// this. CVON (Coalition-VON) has no equivalent "global room" concept - its API
	// (CVON_VONGameModeComponent) is frequency/radio-based instead, so cross-faction spectator
	// voice during replay has no direct port yet. Left as a no-op, same as when PSCore's manager
	// was previously unavailable.
	void SetAllPlayersToGlobalVoN()
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Moving all players to global VoN room", LogLevel.NORMAL);

		if (!CVON_VONGameModeComponent.GetInstance())
		{
			Print("GRAD_Endgame_ReplayManager: CVON_VONGameModeComponent not found - cross-faction voice may not work", LogLevel.WARNING);
			return;
		}

		Print("GRAD_Endgame_ReplayManager: No CVON equivalent for global VoN room yet - cross-faction voice may not work", LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	// Send frame chunks with delays to ensure proper data arrival on clients
	void SendFrameChunksWithDelay(int currentChunkStart)
	{
		const int chunkSize = 30; // Send 30 frames at a time
		const int delayBetweenChunks = 200; // 200ms delay between chunks (paced to avoid client-side hitches)
		
		if (currentChunkStart >= m_replayData.frames.Count())
		{
			// Explosive events are a single small payload covering the whole replay, so they go
			// out once here rather than being split across the per-frame chunks.
			SendExplosiveEvents();

			// All chunks sent, send completion signal after final delay
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: All chunks sent, sending completion RPC in 500ms", LogLevel.NORMAL);
			GetGame().GetCallqueue().CallLater(SendReplayComplete, 500, false);
			return;
		}
		
		int endIndex = Math.Min(currentChunkStart + chunkSize, m_replayData.frames.Count());
		m_iChunksSent++;
		float progress = m_iChunksSent / (float)m_iTotalChunksToSend;
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Sending frame chunk %1-%2 (%3/%4 chunks, %.1f%%)", currentChunkStart, endIndex-1, m_iChunksSent, m_iTotalChunksToSend, progress * 100), LogLevel.NORMAL);
		SendFrameChunk(currentChunkStart, endIndex);
		
		// Update client loading progress
		Rpc(RpcAsk_UpdateLoadingProgress, progress);
		
		// Schedule next chunk
		GetGame().GetCallqueue().CallLater(SendFrameChunksWithDelay, delayBetweenChunks, false, currentChunkStart + chunkSize);
	}
	
	//------------------------------------------------------------------------------------------------
	void SendReplayComplete()
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Sending completion RPC", LogLevel.NORMAL);
		Rpc(RpcAsk_ReplayDataComplete);

		// Calculate adaptive speed on server so we use the same speed clients will use
		CalculateAdaptiveSpeed();

		// Arm the ack/cooldown machine. Clients respond to the completion RPC
		// with RpcAsk_Server_ReplayLoaded; each ack resets the cooldown. Once
		// the cooldown elapses with no further acks, BeginReplayForAll fires
		// once and broadcasts the actual playback start. Endscreen scheduling
		// moves into BeginReplayForAll so its timer is anchored to playback
		// start, not to data-send completion.
		m_aAckedPlayerIds.Clear();
		m_bWaitingForAcks = true;

		GetGame().GetCallqueue().Remove(BeginReplayForAll);
		GetGame().GetCallqueue().Remove(BeginReplayForAll_HardCap);

		GetGame().GetCallqueue().CallLater(BeginReplayForAll,         REPLAY_ACK_COOLDOWN_MS, false);
		GetGame().GetCallqueue().CallLater(BeginReplayForAll_HardCap, REPLAY_ACK_HARDCAP_MS,  false);

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Awaiting client acks. Cooldown=%1ms, HardCap=%2ms",
				REPLAY_ACK_COOLDOWN_MS, REPLAY_ACK_HARDCAP_MS), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Single fire-once entry point. Reached either when the rolling cooldown
	// elapses with no further acks, or via BeginReplayForAll_HardCap after the
	// absolute timeout. m_bWaitingForAcks makes this idempotent against a
	// cooldown/hard-cap race.
	void BeginReplayForAll()
	{
		if (!Replication.IsServer())
			return;

		if (!m_bWaitingForAcks)
			return;
		m_bWaitingForAcks = false;

		GetGame().GetCallqueue().Remove(BeginReplayForAll);
		GetGame().GetCallqueue().Remove(BeginReplayForAll_HardCap);

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: BeginReplayForAll firing. Acks received: %1", m_aAckedPlayerIds.Count()), LogLevel.NORMAL);

		Rpc(RpcDo_Broadcast_BeginPlayback);

		// Schedule endscreen, measured from now + buffer for the client-side
		// 500ms CallLater, map/replay-layer wait, and stabilisation delay.
		if (m_replayData)
		{
			float replayDuration = m_replayData.totalDuration;
			float effectiveDuration = replayDuration / m_fPlaybackSpeed;
			float waitTime = (effectiveDuration + 15.0) * 1000;
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: Scheduling endscreen in %.1fs (replay %.2fs @ %.1fx)",
					waitTime / 1000, replayDuration, m_fPlaybackSpeed), LogLevel.NORMAL);
			GetGame().GetCallqueue().CallLater(TriggerEndscreen, waitTime, false);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Distinct callqueue handle so the cooldown reschedule in
	// RpcAsk_Server_ReplayLoaded only re-arms the cooldown, never the hard cap.
	void BeginReplayForAll_HardCap()
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: HARD-CAP fired (%1ms elapsed). Acks: %2",
				REPLAY_ACK_HARDCAP_MS, m_aAckedPlayerIds.Count()), LogLevel.NORMAL);
		BeginReplayForAll();
	}
	
	//------------------------------------------------------------------------------------------------
	void SendFrameChunk(int startIndex, int endIndex)
	{
		// Convert frame data to simple arrays for transmission
		ref array<float> timestamps = {};
		ref array<string> playerIds = {};
		ref array<vector> positions = {};
		ref array<vector> rotations = {};
		ref array<string> factions = {};
		ref array<bool> inVehicles = {};
		ref array<RplId> playerVehicleIds = {};
		
		// Projectile data arrays
		ref array<float> projTimestamps = {};
		ref array<string> projTypes = {};
		ref array<vector> projFiringPos = {};
		ref array<vector> projImpactPos = {};
		ref array<vector> projVelocities = {};
		
		// Transmission data arrays
		ref array<float> transTimestamps = {};
		ref array<vector> transPositions = {};
		ref array<int> transStates = {};
		ref array<float> transProgress = {};

		// Vehicle data arrays
		ref array<float> vehicleTimestamps = {};
		ref array<RplId> vehicleIds = {};
		// Packed as "<role>|<0|1 alive>" so the player chunk stays within the argument limit
		// the engine's Rpc() accepts - two extra parallel arrays exceeded it.
		ref array<string> playerRoleAlive = {};
		ref array<string> vehicleTypes = {};
		ref array<string> vehicleFactions = {};
		ref array<vector> vehiclePositions = {};
		ref array<vector> vehicleRotations = {};
		ref array<bool> vehicleWasUsed = {};
		ref array<bool> vehicleIsEmpty = {};
		
		for (int i = startIndex; i < endIndex; i++)
		{
			GRAD_Endgame_ReplayFrame frame = m_replayData.frames[i];
			
			foreach (GRAD_Endgame_PlayerSnapshot playerData : frame.players)
			{
				timestamps.Insert(frame.timestamp);
				positions.Insert(playerData.position);
				rotations.Insert(playerData.angles);
				factions.Insert(playerData.factionKey);
				inVehicles.Insert(playerData.isInVehicle);
				playerVehicleIds.Insert(playerData.vehicleId);

				// Rpc() accepts at most 8 parameters, so string fields are packed rather than
				// sent as their own arrays. Player names are user-controlled and may contain
				// any character, so the name goes LAST and is not split on - only the two
				// leading fields are, with SplitLimited-style manual parsing on receive.
				playerIds.Insert(string.Format("%1|%2", playerData.playerId, playerData.playerName));
				string aliveFlag = "0";
				if (playerData.isAlive)
					aliveFlag = "1";
				playerRoleAlive.Insert(playerData.unitRole + "|" + aliveFlag);
			}
			
			// Add projectile data
			foreach (GRAD_Endgame_ProjectileSnapshot projData : frame.projectiles)
			{
				projTimestamps.Insert(frame.timestamp);
				projTypes.Insert(projData.projectileType);
				projFiringPos.Insert(projData.position);
				projImpactPos.Insert(projData.impactPosition);
				projVelocities.Insert(projData.velocity);
			}
			
			// Add transmission data
			foreach (GRAD_Endgame_TransmissionSnapshot transData : frame.transmissions)
			{
				transTimestamps.Insert(frame.timestamp);
				transPositions.Insert(transData.position);
				transStates.Insert(transData.state);
				transProgress.Insert(transData.progress);
			}
			
			// Add vehicle data
			foreach (GRAD_Endgame_VehicleSnapshot vehicleData : frame.vehicles)
			{
				vehicleTimestamps.Insert(frame.timestamp);
				vehicleIds.Insert(vehicleData.entityId);
				vehicleTypes.Insert(vehicleData.vehicleType);
				vehicleFactions.Insert(vehicleData.factionKey);
				vehiclePositions.Insert(vehicleData.position);
				vehicleRotations.Insert(vehicleData.angles);
				vehicleWasUsed.Insert(vehicleData.m_bWasUsed);
				vehicleIsEmpty.Insert(vehicleData.m_bIsEmpty);
			}
		}
		
		// Send player data
		Rpc(RpcAsk_ReceivePlayerChunk, timestamps, playerIds, positions, rotations, factions, inVehicles, playerVehicleIds, playerRoleAlive);
		
		// Send projectile data if any
		if (projTimestamps.Count() > 0)
			Rpc(RpcAsk_ReceiveProjectileChunk, projTimestamps, projTypes, projFiringPos, projImpactPos, projVelocities);
		
		// Send transmission data if any
		if (transTimestamps.Count() > 0)
			Rpc(RpcAsk_ReceiveTransmissionChunk, transTimestamps, transPositions, transStates, transProgress);

		// Send vehicle data if any
		if (vehicleTimestamps.Count() > 0)
			Rpc(RpcAsk_ReceiveVehicleChunk, vehicleTimestamps, vehicleIds, vehicleTypes, vehicleFactions, vehiclePositions, vehicleRotations, vehicleWasUsed, vehicleIsEmpty);
	}

	//------------------------------------------------------------------------------------------------
	// Explosive events are not chunked per frame - they are absolute-time events covering the whole
	// replay, so they are sent once, after the frame chunks.
	void SendExplosiveEvents()
	{
		if (!m_replayData || m_replayData.explosiveEvents.IsEmpty())
			return;

		ref array<int> kinds = {};
		ref array<vector> positions = {};
		ref array<vector> endPositions = {};
		ref array<float> startTimes = {};
		ref array<float> endTimes = {};
		ref array<int> colors = {};
		ref array<string> factions = {};

		foreach (GRAD_Endgame_ExplosiveEvent evt : m_replayData.explosiveEvents)
		{
			kinds.Insert(evt.kind);
			positions.Insert(evt.position);
			endPositions.Insert(evt.endPosition);
			startTimes.Insert(evt.startTime);
			endTimes.Insert(evt.endTime);
			colors.Insert(evt.color);
			factions.Insert(evt.factionKey);
		}

		Rpc(RpcAsk_ReceiveExplosiveEvents, kinds, positions, endPositions, startTimes, endTimes, colors, factions);

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: sent %1 explosive events", kinds.Count()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	void RpcAsk_ReceiveExplosiveEvents(array<int> kinds, array<vector> positions, array<vector> endPositions, array<float> startTimes, array<float> endTimes, array<int> colors, array<string> factions)
	{
		if (!m_replayData)
			m_replayData = GRAD_Endgame_ReplayData.Create();

		m_replayData.explosiveEvents.Clear();

		for (int i = 0; i < kinds.Count(); i++)
		{
			m_replayData.explosiveEvents.Insert(
				GRAD_Endgame_ExplosiveEvent.Create(kinds[i], positions[i], endPositions[i], startTimes[i], endTimes[i], colors[i], factions[i]));
		}

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: received %1 explosive events", kinds.Count()), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! Explosive events active at a given playback time, for the map layer to render.
	//! Includes AT shots still in flight, HE flashes still fading, and smoke clouds still present.
	//! Total recorded explosive events, regardless of whether any is currently active.
	int GetExplosiveEventCount()
	{
		if (!m_replayData)
			return 0;

		return m_replayData.explosiveEvents.Count();
	}

	//------------------------------------------------------------------------------------------------
	int GetActiveExplosiveEvents(float playbackTime, out notnull array<ref GRAD_Endgame_ExplosiveEvent> outEvents)
	{
		outEvents.Clear();

		if (!m_replayData)
			return 0;

		float replayStart = m_replayData.startTime;

		// Detonations are instantaneous in reality but need to stay on screen long enough to be
		// perceived. Because playback is time-compressed, the window is widened by the playback
		// speed so the animation reads the same at any replay rate. Blasts that were seconds apart
		// may therefore overlap on screen - that is an accepted trade for legibility.
		float detonationWindow = DETONATION_VISIBLE_SECONDS * Math.Max(1.0, m_fPlaybackSpeed);

		foreach (GRAD_Endgame_ExplosiveEvent evt : m_replayData.explosiveEvents)
		{
			// Event times are absolute world time; playbackTime is relative to replay start.
			float relStart = evt.startTime - replayStart;
			float relEnd = evt.endTime - replayStart;

			// Smoke has a genuine duration of its own; point events get the perceptual window.
			if (evt.kind != EGradEndgameExplosiveKind.SMOKE)
				relEnd = Math.Max(relEnd, relStart + detonationWindow);

			// A smoke thrown before recording began is still burning when the replay opens, so
			// clamp its start rather than filtering it out for having a negative relative time.
			if (relStart < 0)
				relStart = 0;

			if (relEnd < 0)
				continue; // finished before the replay window - genuinely not shown

			if (playbackTime >= relStart && playbackTime <= relEnd)
				outEvents.Insert(evt);
		}

		return outEvents.Count();
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	void RpcAsk_StartReplayPlayback(float totalDuration, string missionName, string mapName, float startTime)
	{
		string isServer = "Client";
		if (Replication.IsServer()) { isServer = "Server"; }
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: === RPC RECEIVED === RpcAsk_StartReplayPlayback on %1",isServer), LogLevel.NORMAL);
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: RpcAsk_StartReplayPlayback received - Server: %1", isServer), LogLevel.NORMAL);
		
		if (Replication.IsServer())
			return; // Don't run on server
			
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Client initializing replay data", LogLevel.NORMAL);
		
		// Initialize replay data structure
		m_replayData = GRAD_Endgame_ReplayData.Create();
		m_replayData.totalDuration = totalDuration;
		m_replayData.missionName = missionName;
		m_replayData.mapName = mapName;
		m_replayData.startTime = startTime;
		
		// Show loading progress in gamestate HUD
		GRAD_Endgame_Gamestate gamestateDisplay = FindGamestateDisplay();
		if (gamestateDisplay)
		{
			ShowOutcomeHeadline(gamestateDisplay);
			gamestateDisplay.ShowPersistentText("Replay loading... 0%");
			gamestateDisplay.UpdateProgress(0);
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: Showed persistent loading text in gamestate HUD", LogLevel.NORMAL);
		}

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Client replay data initialized - Duration: %1", totalDuration), LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	void RpcAsk_ReceiveVehicleChunk(array<float> timestamps, array<RplId> vehicleIds, array<string> vehicleTypes, array<string> vehicleFactions, array<vector> vehiclePositions, array<vector> vehicleRotations, array<bool> vehicleWasUsed, array<bool> vehicleIsEmpty)
	{
		if (Replication.IsServer())
			return;
			
		if (!m_replayData)
		{
			Print("GRAD_Endgame_ReplayManager: Replay data not initialized!", LogLevel.ERROR);
			return;
		}
		
		// Defensive check against null arrays from RPC
		if (!timestamps || !vehicleIds || !vehicleTypes || !vehicleFactions || !vehiclePositions || !vehicleRotations || !vehicleWasUsed || !vehicleIsEmpty)
		{
			Print("GRAD_Endgame_ReplayManager: Received null array in vehicle chunk RPC, skipping.", LogLevel.WARNING);
			return;
		}
		
		for (int i = 0; i < timestamps.Count(); i++)
		{
			float timestamp = timestamps[i];
			
			GRAD_Endgame_ReplayFrame frame = null;
			for (int j = 0; j < m_replayData.frames.Count(); j++)
			{
				if (m_replayData.frames[j].timestamp == timestamp)
				{
					frame = m_replayData.frames[j];
					break;
				}
			}
			
			if (!frame)
			{
				frame = new GRAD_Endgame_ReplayFrame();
				frame.timestamp = timestamp;
				m_replayData.frames.Insert(frame);
			}
			
			bool wasUsed = false;
			if (vehicleWasUsed && i < vehicleWasUsed.Count())
				wasUsed = vehicleWasUsed[i];

			bool empty = false;
			if (vehicleIsEmpty && i < vehicleIsEmpty.Count())
				empty = vehicleIsEmpty[i];

			GRAD_Endgame_VehicleSnapshot vehicleData = GRAD_Endgame_VehicleSnapshot.Create(
				vehicleIds[i],
				vehicleTypes[i],
				vehicleFactions[i],
				vehiclePositions[i],
				vehicleRotations[i], 
				empty,
				wasUsed
			);
			
			frame.vehicles.Insert(vehicleData);
		}
		
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Received vehicle chunk with %1 vehicles", timestamps.Count()), LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	//! playerIds entries are packed as "<playerId>|<playerName>" and playerRoleAlive as
	//! "<role>|<0|1 alive>" - Rpc() accepts at most 8 parameters, so these four fields share
	//! two arrays. See the packing site in SendReplayChunk for the format.
	void RpcAsk_ReceivePlayerChunk(array<float> timestamps, array<string> playerIds, array<vector> positions,
		array<vector> rotations, array<string> factions, array<bool> inVehicles, array<RplId> playerVehicleIds,
		array<string> playerRoleAlive)
	{
		string isServer = "Client";
		if (Replication.IsServer()) { isServer = "Server"; }
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: === RPC RECEIVED === RpcAsk_ReceivePlayerChunk on %1", isServer), LogLevel.NORMAL);
		
		if (Replication.IsServer())
			return;
			
		if (!m_replayData)
		{
			Print("GRAD_Endgame_ReplayManager: Replay data not initialized!", LogLevel.ERROR);
			return;
		}
		
		// Process player data
		for (int i = 0; i < timestamps.Count(); i++)
		{
			float timestamp = timestamps[i];
			
			// Find or create frame for this timestamp
			GRAD_Endgame_ReplayFrame frame = null;
			for (int j = 0; j < m_replayData.frames.Count(); j++)
			{
				if (m_replayData.frames[j].timestamp == timestamp)
				{
					frame = m_replayData.frames[j];
					break;
				}
			}
			
			if (!frame)
			{
				frame = new GRAD_Endgame_ReplayFrame();
				frame.timestamp = timestamp;
				m_replayData.frames.Insert(frame);
			}
			
			// Unpack "<role>|<0|1 alive>"; fall back to the old defaults if the field is
			// missing or malformed so a mismatched client still renders something sane.
			bool alive = true;
			string role = "Rifleman";
			if (playerRoleAlive && i < playerRoleAlive.Count())
			{
				// Role strings come from DetermineUnitRole and never contain "|", so the
				// first separator is also the only one.
				string packedRole = playerRoleAlive[i];
				int roleSep = packedRole.IndexOf("|");
				if (roleSep >= 0)
				{
					role = packedRole.Substring(0, roleSep);
					alive = (packedRole.Substring(roleSep + 1, packedRole.Length() - roleSep - 1) == "1");
				}
			}

			// Unpack "<playerId>|<playerName>". Split on the FIRST separator only - a player
			// name may itself contain "|", and everything after the first one is the name.
			string packedId = playerIds[i];
			int idSep = packedId.IndexOf("|");
			int parsedPlayerId = 0;
			string parsedPlayerName = "";
			if (idSep >= 0)
			{
				parsedPlayerId = packedId.Substring(0, idSep).ToInt();
				parsedPlayerName = packedId.Substring(idSep + 1, packedId.Length() - idSep - 1);
			}
			else
			{
				parsedPlayerId = packedId.ToInt();
			}

			GRAD_Endgame_PlayerSnapshot playerData = GRAD_Endgame_PlayerSnapshot.Create(
				parsedPlayerId,
				parsedPlayerName,
				factions[i],
				positions[i],
				rotations[i],
				alive,
				inVehicles[i],
				"", // vehicleType is not sent for players; the vehicle chunk carries it
				role,
				playerVehicleIds[i]
			);
			
			frame.players.Insert(playerData);
		}
		
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Received player chunk with %1 players", timestamps.Count()), LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	void RpcAsk_ReceiveProjectileChunk(array<float> projTimestamps, array<string> projTypes, 
		array<vector> projFiringPos, array<vector> projImpactPos, array<vector> projVelocities)
	{
		string isServer = "Client";
		if (Replication.IsServer()) { isServer = "Server"; }
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: === RPC RECEIVED === RpcAsk_ReceiveProjectileChunk on %1", isServer), LogLevel.NORMAL);
		
		if (Replication.IsServer())
			return;
			
		if (!m_replayData)
		{
			Print("GRAD_Endgame_ReplayManager: Replay data not initialized!", LogLevel.ERROR);
			return;
		}
		
		// Process projectile data
		for (int i = 0; i < projTimestamps.Count(); i++)
		{
			float timestamp = projTimestamps[i];
			
			// Find or create frame for this timestamp
			GRAD_Endgame_ReplayFrame frame = null;
			for (int j = 0; j < m_replayData.frames.Count(); j++)
			{
				if (m_replayData.frames[j].timestamp == timestamp)
				{
					frame = m_replayData.frames[j];
					break;
				}
			}
			
			if (!frame)
			{
				frame = new GRAD_Endgame_ReplayFrame();
				frame.timestamp = timestamp;
				m_replayData.frames.Insert(frame);
			}
			
			GRAD_Endgame_ProjectileSnapshot projData = GRAD_Endgame_ProjectileSnapshot.Create(
				projTypes[i],
				projFiringPos[i],
				projImpactPos[i],
				projVelocities[i],
				10.0
			);
			
			frame.projectiles.Insert(projData);
		}
		
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Received projectile chunk with %1 projectiles", projTimestamps.Count()), LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	void RpcAsk_ReceiveTransmissionChunk(array<float> transTimestamps, array<vector> transPositions, 
		array<int> transStates, array<float> transProgress)
	{
		string isServer = "Client";
		if (Replication.IsServer()) { isServer = "Server"; }
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: === RPC RECEIVED === RpcAsk_ReceiveTransmissionChunk on %1", isServer), LogLevel.NORMAL);
		
		if (Replication.IsServer())
			return;
			
		if (!m_replayData)
		{
			Print("GRAD_Endgame_ReplayManager: Replay data not initialized!", LogLevel.ERROR);
			return;
		}
		
		// Process transmission data
		for (int i = 0; i < transTimestamps.Count(); i++)
		{
			float timestamp = transTimestamps[i];
			
			// Find or create frame for this timestamp
			GRAD_Endgame_ReplayFrame frame = null;
			for (int j = 0; j < m_replayData.frames.Count(); j++)
			{
				if (m_replayData.frames[j].timestamp == timestamp)
				{
					frame = m_replayData.frames[j];
					break;
				}
			}
			
			if (!frame)
			{
				frame = new GRAD_Endgame_ReplayFrame();
				frame.timestamp = timestamp;
				m_replayData.frames.Insert(frame);
			}
			
			GRAD_Endgame_TransmissionSnapshot transData = GRAD_Endgame_TransmissionSnapshot.Create(
				transPositions[i],
				transStates[i],
				transProgress[i]
			);
			
			frame.transmissions.Insert(transData);
		}
		
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Received transmission chunk with %1 transmissions", transTimestamps.Count()), LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	void RpcAsk_UpdateLoadingProgress(float progress)
	{
		if (Replication.IsServer())
			return;

		// Update gamestate HUD with loading progress
		GRAD_Endgame_Gamestate gamestateDisplay = FindGamestateDisplay();
		if (gamestateDisplay)
			gamestateDisplay.UpdateProgress(progress);
	}
	
	//------------------------------------------------------------------------------------------------
	// Client-side: server has finished sending all chunks. The client now has
	// the full replay data; ack the server and wait for RpcDo_Broadcast_BeginPlayback
	// to actually start playback.
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	void RpcAsk_ReplayDataComplete()
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: === RPC RECEIVED === RpcAsk_ReplayDataComplete", LogLevel.NORMAL);

		if (Replication.IsServer())
			return;

		if (!m_replayData)
		{
			Print("GRAD_Endgame_ReplayManager: Replay data not available for ack!", LogLevel.ERROR);
			return;
		}

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Client received complete replay data, %1 frames - sending ready ack",
				m_replayData.frames.Count()), LogLevel.NORMAL);

		GRAD_Endgame_Gamestate gamestateDisplay = FindGamestateDisplay();
		if (gamestateDisplay)
			gamestateDisplay.UpdateText("Replay loaded, waiting for other players...");

		int localPlayerId = SCR_PlayerController.GetLocalPlayerId();
		if (localPlayerId <= 0)
		{
			Print("GRAD_Endgame_ReplayManager: No local player id available for ack", LogLevel.WARNING);
			return;
		}

		Rpc(RpcAsk_Server_ReplayLoaded, localPlayerId);
	}

	//------------------------------------------------------------------------------------------------
	// Server tells every client to start playback now. Runs the same
	// map-layer / adaptive-speed prep that previously happened immediately
	// inside RpcAsk_ReplayDataComplete.
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	void RpcDo_Broadcast_BeginPlayback()
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: === RPC RECEIVED === RpcDo_Broadcast_BeginPlayback (begin playback)", LogLevel.NORMAL);

		if (Replication.IsServer())
			return;

		if (!m_replayData)
		{
			Print("GRAD_Endgame_ReplayManager: BeginPlayback received but no replay data!", LogLevel.ERROR);
			return;
		}

		// Enable replay mode immediately for map layer
		SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
		if (mapEntity)
		{
			GRAD_Endgame_ReplayMapLayer replayLayer = GRAD_Endgame_ReplayMapLayer.Cast(mapEntity.GetMapModule(GRAD_Endgame_ReplayMapLayer));
			if (replayLayer)
				replayLayer.SetReplayMode(true);
		}

		CalculateAdaptiveSpeed();
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Calculated adaptive speed: %.2fx", m_fPlaybackSpeed), LogLevel.NORMAL);

		GRAD_Endgame_Gamestate gamestateDisplay = FindGamestateDisplay();
		if (gamestateDisplay)
			gamestateDisplay.UpdateText("Starting replay...");

		GetGame().GetCallqueue().CallLater(StartClientReplayPlayback, 500, false);
	}
	
	//------------------------------------------------------------------------------------------------
	void StartClientReplayPlayback()
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: StartClientReplayPlayback called", LogLevel.NORMAL);

		// Always open the map using our custom MapFullscreen.conf so that
		// GRAD_Endgame_ReplayMapLayer and GRAD_Endgame_MapMarkerManager are loaded.
		// The gadget-map path was removed because it opens the vanilla map config
		// which lacks the replay layer, causing markers to never appear.
		SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
		if (!mapEntity)
		{
			// SCR_MapEntity not available yet — retry in 500ms
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: SCR_MapEntity not available yet, retrying in 500ms", LogLevel.NORMAL);
			GetGame().GetCallqueue().CallLater(StartClientReplayPlayback, 500, false);
			return;
		}

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Opening map via SCR_MapEntity with replay config", LogLevel.NORMAL);

		ResourceName replayMapConfig = "{1B8AC767E06A0ACD}Configs/Map/MapFullscreen.conf";

		// If the spectator menu is active (client on dedicated server), route through
		// OpenMapWithConfig() so the map is anchored to m_wMapFrame with proper keybinds
		// and layout. Opening via mapEntity directly with null parent skips the frame
		// container, causing missing keybinds and freely floating markers.
		if (COA_SpectatorMenu.s_EndgameSpectatorMenu)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: Routing map open through COA_SpectatorMenu.OpenMapWithConfig", LogLevel.NORMAL);
			COA_SpectatorMenu.s_EndgameSpectatorMenu.OpenMapWithConfig(replayMapConfig);
		}
		else
		{
			// No spectator menu (a living player): open the vanilla map menu. It activates the map
			// input (pan, zoom) and hides the HUD; our layer is part of its config (MapFullscreen.conf).
			GetGame().GetMenuManager().OpenMenu(ChimeraMenuPreset.MapMenu);
		}

		// Wait for map + replay layer to be ready before starting playback
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: CLIENT - Waiting for map and replay layer to be ready...", LogLevel.NORMAL);
		WaitForReplayMapAndStartClientPlayback(0);
	}

	//------------------------------------------------------------------------------------------------
	// Polls until map entity + replay layer are ready on client, then starts playback
	void WaitForReplayMapAndStartClientPlayback(int attempt)
	{
		int maxAttempts = 50; // 50 * 200ms = 10 seconds max

		SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
		if (!mapEntity)
		{
			if (attempt < maxAttempts)
			{
				GetGame().GetCallqueue().CallLater(WaitForReplayMapAndStartClientPlayback, 200, false, attempt + 1);
				return;
			}
			Print("GRAD_Endgame_ReplayManager: CLIENT - Map entity not ready after max retries", LogLevel.ERROR);
		}

		GRAD_Endgame_ReplayMapLayer replayLayer;
		if (mapEntity)
		{
			replayLayer = GRAD_Endgame_ReplayMapLayer.Cast(mapEntity.GetMapModule(GRAD_Endgame_ReplayMapLayer));
		}

		if (!replayLayer && attempt < maxAttempts)
		{
			GetGame().GetCallqueue().CallLater(WaitForReplayMapAndStartClientPlayback, 200, false, attempt + 1);
			return;
		}

		if (!replayLayer)
			Print(string.Format("GRAD_Endgame_ReplayManager: CLIENT - ReplayMapLayer NOT found after %1 attempts - replay config may be wrong", attempt), LogLevel.ERROR);
		else
			Print(string.Format("GRAD_Endgame_ReplayManager: CLIENT - ReplayMapLayer found after %1 attempts, starting playback", attempt), LogLevel.NORMAL);

		// Set replay mode on both layers
		if (replayLayer)
		{
			replayLayer.SetReplayMode(true);
		}

		if (mapEntity)
		{
			GRAD_Endgame_MapMarkerManager markerMgr = GRAD_Endgame_MapMarkerManager.Cast(mapEntity.GetMapModule(GRAD_Endgame_MapMarkerManager));
			if (markerMgr)
			{
				markerMgr.SetReplayMode(true);
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print("GRAD_Endgame_ReplayManager: CLIENT - Enabled replay mode on marker manager", LogLevel.NORMAL);
			}
		}

		// Start playback (deferred briefly to allow map projection/pan/zoom to stabilise)
		int stabilizationDelayMs = 300; // Small delay to allow map to settle
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: CLIENT - Deferring playback start by %1 ms to stabilise map", stabilizationDelayMs), LogLevel.NORMAL);
		GetGame().GetCallqueue().CallLater(StartClientPlaybackNow, stabilizationDelayMs, false);

		// NOTE: Endscreen is scheduled server-side in StartPlaybackForAllClients.
		// TriggerEndscreen is server-only so no client-side scheduling needed.
		}

		//------------------------------------------------------------------------------------------------
		// Starts the client-side playback after a short stabilization delay
		void StartClientPlaybackNow()
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: CLIENT - StartClientPlaybackNow called", LogLevel.NORMAL);

			// Start playback
			BaseWorld world = GetGame().GetWorld();
			if (!world)
			{
				Print("GRAD_Endgame_ReplayManager: CLIENT - World not available", LogLevel.ERROR);
				return;
			}

			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: CLIENT - Setting playback state flags", LogLevel.NORMAL);
			m_bIsPlayingBack = true;
			m_bPlaybackPaused = false;
			m_fPlaybackStartTime = world.GetWorldTime() / 1000.0;
			m_fCurrentPlaybackTime = 0;
			m_iCurrentFrameIndex = 0;
			m_bFirstFrameDisplayed = false;

			// Skip empty frames at start
			SkipEmptyFrames();

			// Reset start time right before loop
			BaseWorld world2 = GetGame().GetWorld();
			if (world2)
			{
				m_fPlaybackStartTime = world2.GetWorldTime() / 1000.0;
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print(string.Format("GRAD_Endgame_ReplayManager: CLIENT - Reset playback start time to %.2f", m_fPlaybackStartTime), LogLevel.NORMAL);
			}

			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: CLIENT - Starting playback loop", LogLevel.NORMAL);
			GetGame().GetCallqueue().CallLater(UpdatePlayback, 100, true);
		}
	
	//------------------------------------------------------------------------------------------------
	void UpdatePlayback()
	{
		// Enhanced logging to diagnose playback issues
		if (!m_bIsPlayingBack || !m_replayData || m_bPlaybackPaused)
		{
			static int updatePlaybackSkipCounter = 0;
			updatePlaybackSkipCounter++;
			if (updatePlaybackSkipCounter % 10 == 0)
			{
				Print(string.Format("GRAD_Endgame_ReplayManager: UpdatePlayback skipped (#%1) - IsPlayingBack: %2, HasData: %3, IsPaused: %4", 
					updatePlaybackSkipCounter, m_bIsPlayingBack, m_replayData != null, m_bPlaybackPaused), LogLevel.WARNING);
				if (m_replayData)
				{
					Print(string.Format("GRAD_Endgame_ReplayManager: Replay data exists with %1 frames, startTime: %.2f, totalDuration: %.2f", 
						m_replayData.frames.Count(), m_replayData.startTime, m_replayData.totalDuration), LogLevel.WARNING);
				}
			}
			return;
		}
		
		// Debug: Log that UpdatePlayback is running
		static int updatePlaybackCallCounter = 0;
		updatePlaybackCallCounter++;
		if (updatePlaybackCallCounter <= 5 || updatePlaybackCallCounter % 50 == 0)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: UpdatePlayback called (#%1) - Current frame: %2/%3", 
					updatePlaybackCallCounter, m_iCurrentFrameIndex, m_replayData.frames.Count()), LogLevel.NORMAL);
		}
			
		// Calculate elapsed time since playback started (in seconds)
		BaseWorld world = GetGame().GetWorld();
		if (!world)
		{
			Print("GRAD_Endgame_ReplayManager: World not available in UpdatePlayback, stopping playback", LogLevel.ERROR);
			m_bIsPlayingBack = false;
			GetGame().GetCallqueue().Remove(UpdatePlayback);
			return;
		}
		
		float currentWorldTime = world.GetWorldTime() / 1000.0; // Convert to seconds
		m_fCurrentPlaybackTime = (currentWorldTime - m_fPlaybackStartTime) * m_fPlaybackSpeed;
		
		if (updatePlaybackCallCounter <= 5 || updatePlaybackCallCounter % 50 == 0)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: Playback time: %.2f, Speed: %.2f, Frame: %3/%4", 
					m_fCurrentPlaybackTime, m_fPlaybackSpeed, m_iCurrentFrameIndex, m_replayData.frames.Count()), LogLevel.NORMAL);
		}
		
		// Find current frame to display
		int framesProcessedThisUpdate = 0;
		while (m_iCurrentFrameIndex < m_replayData.frames.Count())
		{
			GRAD_Endgame_ReplayFrame frame = m_replayData.frames[m_iCurrentFrameIndex];
			framesProcessedThisUpdate++;
			float frameTimeFromStart = frame.timestamp - m_replayData.startTime;
			
			if (frameTimeFromStart <= m_fCurrentPlaybackTime)
			{
				// Display this frame
				if (updatePlaybackCallCounter <= 10 || m_iCurrentFrameIndex % 20 == 0)
				{
					if (GRAD_Endgame_ReplayDebug.IsDebugMode())
						Print(string.Format("GRAD_Endgame_ReplayManager: Displaying frame %1 (time: %.2f vs %.2f)", 
							m_iCurrentFrameIndex, frameTimeFromStart, m_fCurrentPlaybackTime), LogLevel.NORMAL);
				}
				DisplayReplayFrame(frame);
				m_iCurrentFrameIndex++;
			}
			else
			{
				if (updatePlaybackCallCounter <= 5)
				{
					if (GRAD_Endgame_ReplayDebug.IsDebugMode())
						Print(string.Format("GRAD_Endgame_ReplayManager: Waiting for frame %1 (time: %.2f, current: %.2f)", 
							m_iCurrentFrameIndex, frameTimeFromStart, m_fCurrentPlaybackTime), LogLevel.NORMAL);
				}
				break; // Wait for next update
			}
		}
		
		if (framesProcessedThisUpdate > 0 && (updatePlaybackCallCounter <= 10 || updatePlaybackCallCounter % 25 == 0))
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: Processed %1 frames in this update cycle", framesProcessedThisUpdate), LogLevel.NORMAL);
		}
		
		// Check if playback finished
		if (m_fCurrentPlaybackTime >= m_replayData.totalDuration)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: Playback finished, stopping", LogLevel.NORMAL);
			StopPlayback();
		}
	}
	
	//------------------------------------------------------------------------------------------------
	void DisplayReplayFrame(GRAD_Endgame_ReplayFrame frame)
	{
		// Reduce log frequency for frame display
		static int frameLogCounter = 0;
		frameLogCounter++;
		
		if (frameLogCounter % 10 == 0) // Only log every 10th frame
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: Displaying frame at %1s: %2 players, %3 projectiles", 
					frame.timestamp, frame.players.Count(), frame.projectiles.Count()), LogLevel.NORMAL);
		}
			
		// Update map layer with current frame
		SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
		if (mapEntity)
		{
			// Debug: Log map entity found
			if (frameLogCounter <= 3)
			{
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print("GRAD_Endgame_ReplayManager: Map entity found, searching for replay layer...", LogLevel.NORMAL);
			}
			
			GRAD_Endgame_ReplayMapLayer replayLayer = GRAD_Endgame_ReplayMapLayer.Cast(mapEntity.GetMapModule(GRAD_Endgame_ReplayMapLayer));
			if (replayLayer)
			{
				// Debug: Confirm replay layer found
				if (frameLogCounter <= 3)
				{
					if (GRAD_Endgame_ReplayDebug.IsDebugMode())
						Print(string.Format("GRAD_Endgame_ReplayManager: Replay layer found! Sending frame with %1 players", 
							frame.players.Count()), LogLevel.NORMAL);
				}
				replayLayer.UpdateReplayFrame(frame);

				// Hide loading screen after first frame is successfully sent to map layer
				if (!m_bFirstFrameDisplayed)
				{
					OnFirstFrameDisplayed();
				}
			}
			else
			{
				// Only log warning periodically to reduce spam
				if (frameLogCounter % 20 == 0)
				{
					Print("GRAD_Endgame_ReplayManager: WARNING - Replay layer not found on map entity", LogLevel.WARNING);
				}
				static int warningCounter = 0;
				warningCounter++;
				if (warningCounter % 20 == 0)
				{
					Print(string.Format("GRAD_Endgame_ReplayManager: No replay map layer found in map entity (warning #%1)", 
						warningCounter), LogLevel.WARNING);
				}
			}
		}
		else
		{
			static int noMapCounter = 0;
			noMapCounter++;
			if (noMapCounter % 20 == 0)
			{
				Print(string.Format("GRAD_Endgame_ReplayManager: No map entity found (warning #%1)", 
					noMapCounter), LogLevel.WARNING);
			}
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Called when the first replay frame has been successfully rendered on the map layer.
	// Hides the loading screen since markers are now visible.
	void OnFirstFrameDisplayed()
	{
		m_bFirstFrameDisplayed = true;
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: First replay frame rendered, hiding loading screen", LogLevel.NORMAL);

		GRAD_Endgame_Gamestate gamestateDisplay = FindGamestateDisplay();
		if (gamestateDisplay)
		{
			gamestateDisplay.HideText();
		}
	}

	//------------------------------------------------------------------------------------------------
	void SetPlaybackSpeed(float speed)
	{
		m_fPlaybackSpeed = speed;
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Playback speed set to %.1fx", speed), LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	float GetPlaybackSpeed()
	{
		return m_fPlaybackSpeed;
	}
	
	//------------------------------------------------------------------------------------------------
	float GetCurrentPlaybackTime()
	{
		return m_fCurrentPlaybackTime;
	}
	
	//------------------------------------------------------------------------------------------------
	float GetTotalDuration()
	{
		if (m_replayData)
			return m_replayData.totalDuration;
		return 0;
	}
	
	//------------------------------------------------------------------------------------------------
	void PausePlayback()
	{
		m_bPlaybackPaused = true;
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Playback paused", LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	void ResumePlayback()
	{
		if (m_bPlaybackPaused)
		{
			m_bPlaybackPaused = false;
			// Adjust start time to account for pause duration
			float currentWorldTime = GetGame().GetWorld().GetWorldTime() / 1000.0;
			m_fPlaybackStartTime = currentWorldTime - (m_fCurrentPlaybackTime / m_fPlaybackSpeed);
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: Playback resumed", LogLevel.NORMAL);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	void StopPlayback()
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: StopPlayback called", LogLevel.NORMAL);
		
		m_bIsPlayingBack = false;
		m_bPlaybackPaused = false;
		GetGame().GetCallqueue().Remove(UpdatePlayback);
		
		// Re-enable live markers
		SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
		if (mapEntity)
		{
			GRAD_Endgame_MapMarkerManager markerMgr = GRAD_Endgame_MapMarkerManager.Cast(mapEntity.GetMapModule(GRAD_Endgame_MapMarkerManager));
			if (markerMgr)
			{
				markerMgr.SetReplayMode(false);
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print("GRAD_Endgame_ReplayManager: Disabled replay mode on marker manager", LogLevel.NORMAL);
			}
			
			GRAD_Endgame_ReplayMapLayer replayLayer = GRAD_Endgame_ReplayMapLayer.Cast(mapEntity.GetMapModule(GRAD_Endgame_ReplayMapLayer));
			if (replayLayer)
			{
				replayLayer.SetReplayMode(false);
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print("GRAD_Endgame_ReplayManager: Disabled replay mode on replay map layer", LogLevel.NORMAL);
			}
		}
		
		// if (GRAD_Endgame_ReplayDebug.IsDebugMode())
		//	Print("GRAD_Endgame_ReplayManager: Playback finished, closing map", LogLevel.NORMAL);
		
		// uncommented to prevent null pointer spam. no time for this shit.
		// GetGame().GetCallqueue().CallLater(CloseMap, 0, false);
		
		// Endscreen will be triggered by server after scheduled time
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Endscreen will be triggered by server", LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	// Server automatically triggers endscreen after replay duration
	void TriggerEndscreen()
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: ===== TriggerEndscreen CALLED =====", LogLevel.NORMAL);
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: IsServer: %1", Replication.IsServer()), LogLevel.NORMAL);

		if (!Replication.IsServer())
		{
			Print("GRAD_Endgame_ReplayManager: Not server, returning", LogLevel.WARNING);
			return;
		}

		// Fire-once: StartActualPlayback and BeginReplayForAll can both schedule this.
		if (m_bEndscreenTriggered)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: TriggerEndscreen already fired, ignoring duplicate call", LogLevel.WARNING);
			return;
		}
		m_bEndscreenTriggered = true;
		GetGame().GetCallqueue().Remove(TriggerEndscreen);

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Server triggering endscreen NOW", LogLevel.NORMAL);
		
		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (gameMode)
			gameMode.AdvanceToAfterAction();
		else
			Print("GRAD_Endgame_ReplayManager: ERROR - Could not find the Endgame game mode component", LogLevel.ERROR);
	}
	
	//------------------------------------------------------------------------------------------------
	void CloseMap()
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Attempting to close map", LogLevel.NORMAL);
		
		PlayerController playerController = GetGame().GetPlayerController();
		if (!playerController)
		{
			Print("GRAD_Endgame_ReplayManager: No player controller found, closing map entity directly", LogLevel.WARNING);
			SCR_MapEntity mapEntityNoPc = SCR_MapEntity.GetMapInstance();
			if (mapEntityNoPc && mapEntityNoPc.IsOpen())
				mapEntityNoPc.CloseMap();
			return;
		}
		
		IEntity playerEntity = playerController.GetControlledEntity();
		if (!playerEntity)
		{
			// Spectator mode - COA_SpectatorMenu.s_EndgameSpectatorMenu is null on dedicated,
			// so close directly via SCR_MapEntity.
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: No player entity (spectator), closing map via SCR_MapEntity", LogLevel.NORMAL);
			if (COA_SpectatorMenu.s_EndgameSpectatorMenu)
				COA_SpectatorMenu.s_EndgameSpectatorMenu.CloseMap();
			else
			{
				SCR_MapEntity mapEntitySpec = SCR_MapEntity.GetMapInstance();
				if (mapEntitySpec && mapEntitySpec.IsOpen())
					mapEntitySpec.CloseMap();
			}
			return;
		}
		
		// Gadget cleanup is best-effort: during replay all players are switched to
		// the Spectator faction, so they still control an entity (the spectator
		// camera) but no longer carry a map gadget. Neither missing gadget manager
		// nor missing gadget may abort this method — closing the SCR_MapEntity
		// below is the part that actually matters and must always run.
		SCR_GadgetManagerComponent gadgetManager = SCR_GadgetManagerComponent.Cast(playerEntity.FindComponent(SCR_GadgetManagerComponent));
		if (gadgetManager)
		{
			IEntity mapGadget = gadgetManager.GetGadgetByType(EGadgetType.MAP);
			if (mapGadget)
			{
				// Put map back into inventory
				gadgetManager.SetGadgetMode(mapGadget, EGadgetMode.IN_SLOT);
			}
			else if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			{
				Print("GRAD_Endgame_ReplayManager: No map gadget found (spectator?), closing map entity only", LogLevel.NORMAL);
			}
		}
		else if (GRAD_Endgame_ReplayDebug.IsDebugMode())
		{
			Print("GRAD_Endgame_ReplayManager: No gadget manager found, closing map entity only", LogLevel.NORMAL);
		}

		// Always close the SCR_MapEntity itself — SetGadgetMode only manages the
		// gadget slot, it does not call mapEntity.CloseMap(). Without this the
		// map entity stays open, its widget frame gets destroyed by EndGameMode,
		// and its still-active SCR_MapCursorModule keeps ticking on dead widgets,
		// flooding the log with NULL pointer exceptions every frame.
		SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
		if (mapEntity && mapEntity.IsOpen())
			mapEntity.CloseMap();

		// The replay map was opened as the vanilla map menu for a living player: close that menu too.
		GetGame().GetMenuManager().CloseMenuByPreset(ChimeraMenuPreset.MapMenu);

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayManager: Map closed successfully", LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	void SeekToTime(float timeSeconds)
	{
		if (!m_replayData)
			return;
			
		m_fCurrentPlaybackTime = Math.Clamp(timeSeconds, 0, m_replayData.totalDuration);
		m_fPlaybackStartTime = GetGame().GetWorld().GetWorldTime() - m_fCurrentPlaybackTime;
		
		// Find corresponding frame index
		m_iCurrentFrameIndex = 0;
		for (int i = 0; i < m_replayData.frames.Count(); i++)
		{
			GRAD_Endgame_ReplayFrame frame = m_replayData.frames[i];
			float frameTime = frame.timestamp - m_replayData.startTime;
			
			if (frameTime <= m_fCurrentPlaybackTime)
			{
				m_iCurrentFrameIndex = i;
			}
			else
			{
				break;
			}
		}
		
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Seeked to time %1s (frame %2)", 
				timeSeconds, m_iCurrentFrameIndex), LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	// Public interface methods
	bool IsRecording() 
	{ 
		return m_bIsRecording; 
	}
	
	bool IsPlayingBack() 
	{ 
		return m_bIsPlayingBack; 
	}
	
	bool IsPlaybackPaused() 
	{ 
		return m_bPlaybackPaused; 
	}
	
	float GetPlaybackTime()
	{
		return m_fCurrentPlaybackTime;
	}

	//------------------------------------------------------------------------------------------------
	//! Absolute world time the replay began. Explosive events carry absolute timestamps, so the
	//! renderer needs this to convert them into playback-relative times.
	float GetReplayStartTime()
	{
		if (!m_replayData)
			return 0;

		return m_replayData.startTime;
	}
	
	//------------------------------------------------------------------------------------------------
	//! Records a smoke deployment, HE detonation or AT shot.
	//!
	//! These are stored as absolute-time events rather than per-frame snapshots: a rocket's flight
	//! lasts ~1-3s while frames are sampled every m_fRecordingInterval seconds (3s by default), so
	//! snapshotting would miss it entirely. The client interpolates them against the playback clock.
	//! \return the stored event, so the caller can refine it later (smoke end time), or null if
	//! it was not recorded.
	GRAD_Endgame_ExplosiveEvent RecordExplosiveEvent(EGradEndgameExplosiveKind kind, vector position, vector endPosition, float startTime, float endTime, int color, string factionKey)
	{
		if (!m_bIsRecording)
			return null;

		if (!m_replayData)
			return null;

		// Two entities can exist for one thrown grenade (observed: the same smoke recorded twice
		// in the same millisecond at an identical position), so a per-component guard cannot stop
		// it. Reject a duplicate of the same kind at effectively the same place and time instead.
		foreach (GRAD_Endgame_ExplosiveEvent existing : m_replayData.explosiveEvents)
		{
			if (existing.kind != kind)
				continue;

			if (Math.AbsFloat(existing.startTime - startTime) > EXPLOSIVE_DEDUPE_SECONDS)
				continue;

			if (vector.Distance(existing.position, position) > EXPLOSIVE_DEDUPE_METERS)
				continue;

			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: ignoring duplicate explosive event kind=%1 at %2", kind, position.ToString()), LogLevel.NORMAL);

			return existing;
		}

		GRAD_Endgame_ExplosiveEvent evt = GRAD_Endgame_ExplosiveEvent.Create(kind, position, endPosition, startTime, endTime, color, factionKey);
		m_replayData.explosiveEvents.Insert(evt);

		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: recorded explosive event kind=%1 at %2 (total %3)",
				kind, position.ToString(), m_replayData.explosiveEvents.Count()), LogLevel.NORMAL);

		return evt;
	}

	//------------------------------------------------------------------------------------------------
	// New method for recording projectile firing events
	void RecordProjectileFired(vector position, vector velocity, string ammoType)
	{
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: RecordProjectileFired called - Recording=%1, RecordProj=%2, AmmoType=%3", 
				m_bIsRecording, m_bRecordProjectiles, ammoType), LogLevel.NORMAL);
		
		if (!m_bIsRecording)
		{
			Print("GRAD_Endgame_ReplayManager: Not recording - ignoring projectile", LogLevel.WARNING);
			return;
		}
		
		if (!m_bRecordProjectiles)
		{
			Print("GRAD_Endgame_ReplayManager: Projectile recording disabled - ignoring projectile", LogLevel.WARNING);
			return;
		}
			
		// Store the projectile data for the next recording frame
		GRAD_Endgame_ProjectileData projData = new GRAD_Endgame_ProjectileData();
		projData.position = position;
		projData.velocity = velocity;
		projData.ammoType = ammoType;
		projData.fireTime = GetGame().GetWorld().GetWorldTime() / 1000.0; // Convert to seconds
		
		m_pendingProjectiles.Insert(projData);
		
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayManager: Successfully queued projectile - %1 at %2 (pending count: %3)", 
				ammoType, position.ToString(), m_pendingProjectiles.Count()), LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	void VerifyMapIsOpen()
	{
		SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
		if (mapEntity && mapEntity.IsOpen())
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayManager: Map verified as open", LogLevel.NORMAL);
		}
		else
		{
			Print("GRAD_Endgame_ReplayManager: Map not open, trying alternative method", LogLevel.WARNING);
			
			// Open the vanilla map menu
			GetGame().GetMenuManager().OpenMenu(ChimeraMenuPreset.MapMenu);
		}
	}
	
	
	
	//------------------------------------------------------------------------------------------------
	// Calculate adaptive playback speed to fit replay into max duration
	void CalculateAdaptiveSpeed()
	{
		if (!m_replayData)
		{
			m_fAdaptiveSpeed = 1.0;
			return;
		}
		
		float actualDuration = m_replayData.totalDuration;
		
		// If replay is longer than max duration, speed it up
		if (actualDuration > m_fMaxReplayDuration)
		{
			m_fAdaptiveSpeed = actualDuration / m_fMaxReplayDuration;
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: Replay duration %.1fs exceeds max %.1fs, using adaptive speed %.2fx", 
					actualDuration, m_fMaxReplayDuration, m_fAdaptiveSpeed), LogLevel.NORMAL);
		}
		else
		{
			m_fAdaptiveSpeed = 1.0;
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayManager: Replay duration %.1fs fits within max %.1fs, using normal speed", 
					actualDuration, m_fMaxReplayDuration), LogLevel.NORMAL);
		}
		
		// Set the playback speed to the adaptive speed
		m_fPlaybackSpeed = m_fAdaptiveSpeed;
	}
	
	//------------------------------------------------------------------------------------------------
	// Get replay progress as percentage (0.0 to 1.0) for progress bar
	float GetReplayProgress()
	{
		if (!m_replayData)
		{
			// No replay data yet - show 0% but don't error
			return 0.0;
		}
		
		if (m_replayData.totalDuration <= 0)
		{
			// Duration not set yet - show 0%
			return 0.0;
		}
			
		return Math.Clamp(m_fCurrentPlaybackTime / m_replayData.totalDuration, 0.0, 1.0);
	}
	
	//------------------------------------------------------------------------------------------------
	// Get current replay time in seconds
	float GetCurrentReplayTime()
	{
		return m_fCurrentPlaybackTime;
	}
	
	//------------------------------------------------------------------------------------------------
	// Get total replay duration in seconds
	float GetTotalReplayDuration()
	{
		if (!m_replayData)
			return 0.0;
			
		return m_replayData.totalDuration;
	}
	
	//------------------------------------------------------------------------------------------------
	// Skip empty frames at the start of replay
	void SkipEmptyFrames()
{
    if (!m_replayData || m_replayData.frames.Count() == 0)
        return;
        
    if (GRAD_Endgame_ReplayDebug.IsDebugMode())
        Print("GRAD_Endgame_ReplayManager: Checking for empty frames to skip...", LogLevel.NORMAL);
    
    int startIndex = m_iCurrentFrameIndex;
    
    for (int i = startIndex; i < m_replayData.frames.Count(); i++)
    {
        GRAD_Endgame_ReplayFrame frame = m_replayData.frames[i];

        // Check for player content — only players with valid positions count as visual content.
        // Vehicles/projectiles/transmissions alone don't constitute meaningful replay content
        // since they're supplementary to player markers.
        bool hasVisualContent = false;

        if (frame.players.Count() > 0)
        {
            foreach (GRAD_Endgame_PlayerSnapshot player : frame.players)
            {
                // Any real map position will have position.Length() in the thousands
                if (player.position.Length() > 100)
                {
                    hasVisualContent = true;
                    break;
                }
            }
        }

        if (hasVisualContent)
        {
            if (i > startIndex)
            {
                m_iCurrentFrameIndex = i;
                float frameTime = frame.timestamp - m_replayData.startTime;
                m_fCurrentPlaybackTime = frameTime;
                
                float currentWorldTime = GetGame().GetWorld().GetWorldTime() / 1000.0;
                
                // NO TERNARY: Standard if-check for speed safety
                float speed = m_fPlaybackSpeed;
                if (speed == 0)
                {
                    speed = 1.0;
                }
                
                m_fPlaybackStartTime = currentWorldTime - (m_fCurrentPlaybackTime / speed);
                
                if (GRAD_Endgame_ReplayDebug.IsDebugMode())
                    Print(string.Format("GRAD_Endgame_ReplayManager: Skipped %1 empty frames. Starting at frame %2", 
                        i - startIndex, i), LogLevel.NORMAL);
            }
            else
            {
                if (GRAD_Endgame_ReplayDebug.IsDebugMode())
                    Print("GRAD_Endgame_ReplayManager: Content found immediately (Frame " + i + ")", LogLevel.NORMAL);
            }
            return;
        }
    }
    
    Print("GRAD_Endgame_ReplayManager: Warning - All remaining frames seem empty!", LogLevel.WARNING);
	}
	
	//------------------------------------------------------------------------------------------------
	// Push the match outcome ("why is this replay running") onto the loading display, so it stays
	// above the loading text and progress bar for the whole load. The summary is an [RplProp] on
	// the manager, set the moment the outcome was decided, so it is already present on clients here.
	void ShowOutcomeHeadline(GRAD_Endgame_Gamestate gamestateDisplay)
	{
	}

	//------------------------------------------------------------------------------------------------
	// BreakingContact's loading-text HUD display is not part of Endgame.
	//------------------------------------------------------------------------------------------------
	GRAD_Endgame_Gamestate FindGamestateDisplay()
	{
		return null;
	}

}
