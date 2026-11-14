//
[BaseContainerProps()]
class GRAD_Endgame_ReplayMapLayer : GRAD_Endgame_MapMarkerLayer // Inherit from proven working class
{
	protected static GRAD_Endgame_ReplayMapLayer s_Instance;

	// Opacity applied to markers of dead units during replay playback
	protected const float DEAD_MARKER_OPACITY = 0.35;

	// --- Explosive event rendering ---
	// Radii are in metres so they scale with map zoom and stay true to the real area affected.
	protected const float SMOKE_RADIUS_M = 25.0;	// approximate screening radius of a smoke cloud
	// The cloud body is deliberately near-transparent: it marks an area without hiding the units
	// and vehicles inside it. Legibility comes from the outline, not the fill.
	protected const float SMOKE_FILL_ALPHA = 0.18;
	protected const float SMOKE_RING_ALPHA = 0.70;

	// Smallest on-screen radius, in pixels, any explosive marker is drawn at. A 25m cloud is only
	// a few pixels on a zoomed-out map, so without a floor it is drawn correctly yet invisible.
	protected const float MIN_EXPLOSIVE_SCREEN_RADIUS = 12.0;

	// --- Unit / vehicle icon sizing ---
	// Base on-screen size in pixels, used whenever the map is zoomed in past ICON_SCALE_ZOOM_THRESHOLD.
	protected const float ICON_BASE_SIZE_VEHICLE = 128.0;
	protected const float ICON_BASE_SIZE_INFANTRY = 64.0;

	// Icons hold their full size while zoomed in, and only start shrinking once the map is zoomed
	// out past this point. GetCurrentZoom() is pixels-per-metre, so LOWER values are further out.
	// Above the threshold the scale is pinned at 1.0; below it, size falls off in proportion to
	// zoom, so icons stop swamping the map when the whole AO is on screen.
	//
	// Set from the observed maximum zoom-in (~5.24 px/m on kolgujev), so icons are at full size
	// only when fully zoomed in and shrink gradually across the rest of the range.
	protected const float ICON_SCALE_ZOOM_THRESHOLD = 5.24;

	// Floor on the falloff, so icons stay findable at maximum zoom-out instead of vanishing.
	// Kept low because the square-root curve below approaches it slowly - with a linear falloff a
	// floor this low would be hit almost immediately and icons would stop responding to zoom.
	protected const float ICON_MIN_SCALE = 0.35;

	protected const float HE_MIN_RADIUS_M = 4.0;
	protected const float HE_MAX_RADIUS_M = 18.0;
	protected const int HE_COLOR = 0xFFFF3020;		// red blast

	// Real seconds a detonation animation should occupy on screen. Multiplied by playback speed
	// at draw time, so it stays roughly this long whether the replay runs at 1x or 10x.
	protected const float HE_ANIMATION_SECONDS = 1.0;

	// Shape of the detonation animation, as fractions of the event's total duration.
	protected const float HE_PUNCH_PEAK = 0.30;		// expansion completes here, then contracts
	protected const float HE_CONTRACTION = 0.45;	// how far it shrinks back from the peak
	protected const float HE_FADE_START = 0.55;		// opacity holds until here, then fades out

	protected const float AT_ROCKET_RADIUS_M = 4.0;
	protected const float AT_BURST_RADIUS_M = 22.0;
	protected const float AT_BURST_SECONDS = 2.0;
	protected const float AT_TRAIL_FRACTION = 0.25;	// trail covers the last quarter of the flight
	protected const int AT_COLOR = 0xFFFFD040;		// bright yellow, distinct from HE orange

	static GRAD_Endgame_ReplayMapLayer GetInstance()
	{
		return s_Instance;
	}
	
	protected Widget m_WidgetsRoot;
	protected Widget m_MapMenuRoot; // MapFrame — cached from GetMapMenuRoot(), used to find OverlayFooter
    protected ref map<string, ImageWidget> m_ActiveWidgets = new map<string, ImageWidget>();
	protected int m_iMapOpenFrameCounter = 0;
    
	// Guard to prevent double registration of input listeners
	protected bool m_bListenersRegistered = false;

	// Debug/logging guards
	protected bool m_bStabilizedLogged = false;
	protected bool m_bWorldToScreenSampled = false;

	// TEMPORARY (icon scale calibration): last zoom value logged, so the zoom readout fires only
	// when the zoom actually changes rather than every frame. Remove with LogZoomForCalibration().
	protected float m_fLastLoggedZoom = -1.0;

    // We track used widgets every frame to hide/remove unused ones (garbage collection)
    protected ref set<string> m_UsedWidgetKeys = new set<string>();

    override void OnMapOpen(MapConfiguration config)
    {
        super.OnMapOpen(config);

		// OverlayFooter is a direct child of MapMenu in the layout.
		// GetMapMenuRoot() returns MapFrame (a child of MapMenu), so walk up one level.
		// m_MapMenuRoot is used only for FindAnyWidget on hint buttons.
		// m_WidgetsRoot is always parented to the raw GetMapMenuRoot() (MapFrame).
		if (m_MapEntity)
		{
			Widget rawRoot = m_MapEntity.GetMapMenuRoot();
			if (rawRoot && rawRoot.GetName() == "MapFrame" && rawRoot.GetParent())
				m_MapMenuRoot = rawRoot.GetParent();
			else
				m_MapMenuRoot = rawRoot;
		}

		string menuRootName = "null";
		if (m_MapMenuRoot)
			menuRootName = m_MapMenuRoot.GetName();
		Print("Endgame Replay - OnMapOpen: m_MapMenuRoot=" + menuRootName, LogLevel.NORMAL);

        // Create a root frame to hold our markers, always parented to the raw MapFrame.
		if (!m_WidgetsRoot)
		{
			Widget parentWidget = m_MapEntity.GetMapMenuRoot();

			m_WidgetsRoot = GetGame().GetWorkspace().CreateWidget(WidgetType.FrameWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.NOFOCUS | WidgetFlags.IGNORE_CURSOR, Color.White, 0, parentWidget);

			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayMapLayer: Widget Root Created - m_MapMenuRoot=%1, m_WidgetsRoot=%2", m_MapMenuRoot != null, m_WidgetsRoot != null), LogLevel.NORMAL);
			FrameSlot.SetSize(m_WidgetsRoot, 1, 1);
			FrameSlot.SetAnchorMin(m_WidgetsRoot, 0, 0);
			FrameSlot.SetAnchorMax(m_WidgetsRoot, 1, 1);
			FrameSlot.SetOffsets(m_WidgetsRoot, 0, 0, 0, 0);
		}

		m_iMapOpenFrameCounter = 0;

		if (m_bIsInReplayMode)
			SetReplayHintButtonsVisible(true);
    }

    override void OnMapClose(MapConfiguration config)
    {
        super.OnMapClose(config);
        
        UnregisterToggleInputs();
        
        // Cleanup widgets
		if (m_WidgetsRoot)
        {
            m_WidgetsRoot.RemoveFromHierarchy();
            m_WidgetsRoot = null;
        }
        m_ActiveWidgets.Clear();
		m_iMapOpenFrameCounter = 0;
		SetReplayHintButtonsVisible(false);
		m_MapMenuRoot = null;
    }

    // Helper to get or create an icon widget
    protected ImageWidget GetOrCreateMarkerWidget(string key, string texturePath, bool isVehicle)
    {
        ImageWidget w;
        if (m_ActiveWidgets.Find(key, w))
        {
            // Update texture if it changed (rare, but possible)
            // Optimization: You can check a separate "currentTexture" map if needed, 
            // but LoadTexture is usually cached by engine.
            if (texturePath != "") 
                w.LoadImageTexture(0, texturePath);
                
            w.SetVisible(true);
            return w;
        }

        // Create new
        if (!m_WidgetsRoot) return null;

        // STRETCH makes the texture scale to the widget size. Without it the image always draws at
        // its native resolution and the widget acts as a window onto it - so shrinking the widget
        // crops the icon rather than scaling it, revealing more of the texture as it grows again.
        w = ImageWidget.Cast(GetGame().GetWorkspace().CreateWidget(WidgetType.ImageWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH, Color.White, 0, m_WidgetsRoot));
        
        if (texturePath != "")
            w.LoadImageTexture(0, texturePath);
            
        // ---------------------------------------------------------
        // THE FIX: Set Alignment to 0.5, 0.5 (Center)
        // ---------------------------------------------------------
        FrameSlot.SetAlignment(w, 0.5, 0.5);

        // Size is not set here: it depends on the current zoom and is applied every frame by
        // ApplyMarkerSize() from UpdateMarkerWidget(), which also covers cached widgets.
        m_ActiveWidgets.Insert(key, w);
        return w;
    }

    //------------------------------------------------------------------------------------------------
    // Zoom-dependent scale for unit and vehicle icons.
    //
    // Icons keep their full size while the map is zoomed in, so close-up reading is unchanged.
    // Once zoomed out past ICON_SCALE_ZOOM_THRESHOLD they shrink towards ICON_MIN_SCALE, so a
    // zoomed-out map is not buried under overlapping markers.
    //
    // The shrink follows the square root of the zoom ratio rather than the ratio itself. A linear
    // falloff is far too harsh over this range - it loses most of the icon size in the first part
    // of the zoom-out and then sits on the floor. The square root keeps icons noticeably larger
    // through the middle of the range and approaches the floor only at the far zoom-out end.
    protected float GetIconZoomScale()
    {
        if (!m_MapEntity || ICON_SCALE_ZOOM_THRESHOLD <= 0)
            return 1.0;

        float zoom = m_MapEntity.GetCurrentZoom();
        if (zoom >= ICON_SCALE_ZOOM_THRESHOLD)
            return 1.0;

        float ratio = zoom / ICON_SCALE_ZOOM_THRESHOLD;
        if (ratio < 0)
            ratio = 0;

        return Math.Clamp(Math.Sqrt(ratio), ICON_MIN_SCALE, 1.0);
    }

    //------------------------------------------------------------------------------------------------
    // TEMPORARY - icon scale calibration aid.
    //
    // Logs the current map zoom whenever it changes, so ICON_SCALE_ZOOM_THRESHOLD can be set from
    // observed values instead of guessed. Scroll the replay map through its full range and read the
    // zoom off the log at the point icons should stop scaling. Remove this function, its call in
    // ApplyMarkerSize() and m_fLastLoggedZoom once the threshold is chosen.
    protected void LogZoomForCalibration()
    {
        if (!m_MapEntity || !GRAD_Endgame_ReplayDebug.IsDebugMode())
            return;

        float zoom = m_MapEntity.GetCurrentZoom();

        // Only report meaningful changes, otherwise this floods the log every frame.
        if (m_fLastLoggedZoom > 0 && Math.AbsFloat(zoom - m_fLastLoggedZoom) < (m_fLastLoggedZoom * 0.02))
            return;

        m_fLastLoggedZoom = zoom;
        Print(string.Format("Endgame Replay - ReplayMapLayer zoom calibration: GetCurrentZoom()=%1 (px/m), current threshold=%2, resulting icon scale=%3",
            zoom, ICON_SCALE_ZOOM_THRESHOLD, GetIconZoomScale()), LogLevel.NORMAL);
    }

    //------------------------------------------------------------------------------------------------
    // Applies the current zoom-dependent size to a marker widget. DPIUnscale converts the pixel
    // size into widget units, keeping icons resolution independent.
    //
    // The widget is created with WidgetFlags.STRETCH, so the texture follows the frame slot size
    // and setting the slot is enough to scale the icon.
    protected void ApplyMarkerSize(ImageWidget w, bool isVehicle)
    {
        LogZoomForCalibration();

        float baseSize = ICON_BASE_SIZE_INFANTRY;
        if (isVehicle)
            baseSize = ICON_BASE_SIZE_VEHICLE;

        float size = GetGame().GetWorkspace().DPIUnscale(baseSize * GetIconZoomScale());
        FrameSlot.SetSize(w, size, size);
    }

    //------------------------------------------------------------------------------------------------
    // DRAW / UPDATE
    //------------------------------------------------------------------------------------------------
    override void Draw()
    {
        // 1. Clear Command Buffer
        m_Commands.Clear();
        
        // 2. Prepare Widget Tracking
        m_UsedWidgetKeys.Clear();

        GRAD_Endgame_ReplayManager replayManager = GRAD_Endgame_ReplayManager.GetInstance();

		if (!replayManager || !m_MapEntity || !m_WidgetsRoot)
		{
			// Entries can go null when the map closes and the widget tree is torn down while this
			// map still holds references, so each one is checked rather than blindly dereferenced.
			foreach (ImageWidget w : m_ActiveWidgets)
			{
				if (w)
					w.SetVisible(false);
			}
			return;
		}

		// Allow a few frames after map open for pan/zoom/projection to stabilise
		m_iMapOpenFrameCounter++;
		const int minStableFrames = 3;
		if (m_iMapOpenFrameCounter < minStableFrames)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayMapLayer: Skipping draw for initial stabilisation frame %1/%2", m_iMapOpenFrameCounter, minStableFrames), LogLevel.NORMAL);
			return;
		}
		else if (!m_bStabilizedLogged)
		{
			m_bStabilizedLogged = true;
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayMapLayer: Map projection stabilized after %1 frames", m_iMapOpenFrameCounter), LogLevel.NORMAL);
		}

        // MapContext is already activated by COA_SpectatorMenu.OnMenuUpdate every frame when map is open.
        // No need to activate it here again.

        // ---------------------------------------------------------
        // UPDATE UNITS & VEHICLES
        // ---------------------------------------------------------
        
        // --- 1. Vehicles (Draw ALL vehicles here, occupied or not) ---
        array<ref GRAD_Endgame_ReplayVehicleMarker> vehiclesToRender;
        if (m_bIsInReplayMode) vehiclesToRender = m_vehicleMarkers;
        else if (m_hasLastFrame) vehiclesToRender = m_lastFrameVehicleMarkers;
        else vehiclesToRender = {};

        foreach (GRAD_Endgame_ReplayVehicleMarker vehicleMarker : vehiclesToRender)
        {
            if (!vehicleMarker.isVisible) continue;

            // Check if occupied by player to determine Color/Icon
            // Also get the occupant's faction to use for vehicle coloring
            bool isOccupied = false;
            string occupantFaction = "";

            foreach (GRAD_Endgame_ReplayPlayerMarker p : m_playerMarkers) {
                if (p.isInVehicle && p.vehicleId == vehicleMarker.entityId) {
                    isOccupied = true;
                    occupantFaction = p.factionKey;
                    break;
                }
            }

            // Show as empty only if no player is inside AND the recorded snapshot says empty
            bool showAsEmpty = !isOccupied && vehicleMarker.isEmpty;

            // Filter: Hide empty vehicles if toggle is active
            // But keep previously-used vehicles visible (wasUsed exempts from hiding)
            if (m_bHideEmptyVehicles && showAsEmpty && !vehicleMarker.wasUsed)
                continue;

            // Use occupant faction if occupied, otherwise use vehicle's own faction
            string effectiveFaction = vehicleMarker.factionKey;
            if (isOccupied)
                effectiveFaction = occupantFaction;

            // Filter: Hide civilian faction vehicles if toggle is active
            if (m_bHideCivilians && effectiveFaction == "CIV")
                continue;

            // Get Key based on vehicle state - use effectiveFaction for correct coloring
            string vehicleIconKey = GetVehicleIconKey(vehicleMarker.vehicleType, effectiveFaction, showAsEmpty);
            string texturePath = m_vehicleIconTextures.Get(vehicleIconKey);

            // Fallback
            if (texturePath == "") texturePath = m_vehicleIconTextures.Get("M998_closed_empty");

            // Update Widget
            string widgetKey = "VEH_" + vehicleMarker.entityId.ToString();

            // Pass 'showAsEmpty' to the update function to tint it Gray if needed
            UpdateMarkerWidget(widgetKey, texturePath, vehicleMarker.position, vehicleMarker.direction, true, showAsEmpty);
        }

        // --- 2. Infantry / Players ---
        array<ref GRAD_Endgame_ReplayPlayerMarker> playersToRender;
        if (m_bIsInReplayMode) playersToRender = m_playerMarkers;
        else if (m_hasLastFrame) playersToRender = m_lastFramePlayerMarkers;
        else playersToRender = {};

        foreach (GRAD_Endgame_ReplayPlayerMarker playerMarker : playersToRender)
        {
            if (!playerMarker.isVisible) continue;
            
            // --- CHANGE 2: SKIP PLAYERS IN VEHICLES ---
            // The vehicle loop above has already drawn the vehicle.
            // Drawing it here again would use the wrong rotation (Player Look Dir).
            if (playerMarker.isInVehicle) continue;
            
            // Filter: Hide civilian faction infantry if toggle is active
            if (m_bHideCivilians && playerMarker.factionKey == "CIV")
                continue;
            
            // Logic below now only handles foot mobile units
            string roleStr = playerMarker.unitType;
            if (roleStr == "") roleStr = "Rifleman";

            // Normalize COALITION-Lobby's "OPFOR"/"BLUFOR" faction keys to BreakingContact's
            // own "USSR"/"US" convention used by the m_unitTypeTextures keys. Without this the
            // lookup misses and every unit falls back to the BLUFOR "Default" icon.
            string plFaction = playerMarker.factionKey;
            if (GRAD_Endgame_ReplayDebug.IsOpforFactionKey(plFaction))
                plFaction = "USSR";
            else if (GRAD_Endgame_ReplayDebug.IsBluforFactionKey(plFaction))
                plFaction = "US";

            string key = roleStr + "_" + plFaction;
            string texturePath = m_unitTypeTextures.Get(key);

            // CIV faction fallback: civilians don't have military roles, use Rifleman_CIV
            if (texturePath == "" && plFaction == "CIV")
                texturePath = m_unitTypeTextures.Get("Rifleman_CIV");

            // Role fallback: keep the faction colour even when the role has no icon registered
            if (texturePath == "")
                texturePath = m_unitTypeTextures.Get("Rifleman_" + plFaction);

            if (texturePath == "") texturePath = m_unitTypeTextures.Get("Default");

            string widgetKey = "PLR_" + playerMarker.playerId.ToString();
            UpdateMarkerWidget(widgetKey, texturePath, playerMarker.position, playerMarker.direction, false, false, !playerMarker.isAlive);
        }

        // --- 3. Transmissions ---
        // Transmissions are rendered as progress circles only (no icon widgets)
        // See the drawing section below for transmission rendering

        // --- 4. Hide Unused Widgets ---
        foreach (string key, ImageWidget w : m_ActiveWidgets)
        {
            if (!w)
                continue;

            if (!m_UsedWidgetKeys.Contains(key))
            {
                w.SetVisible(false);
            }
        }

        // ---------------------------------------------------------
        // DRAW LINES & PROJECTILES (Keep using Commands for performance on lines)
        // ---------------------------------------------------------
        // Projectiles
        if (m_bIsInReplayMode) {
            foreach (GRAD_Endgame_ReplayProjectileMarker proj : m_projectileMarkers) {
                 if (proj.isVisible) DrawLine(proj.position, proj.impactPosition, 2.0, 0xFFFF0000);
            }

            // Transmission progress circles
            foreach (GRAD_Endgame_ReplayTransmissionMarker transMarker : m_transmissionMarkers)
            {
                if (!transMarker.isVisible) continue;

                // Determine circle color based on transmission state
                int circleColor;
                switch (transMarker.state)
                {
                    case GRAD_Endgame_ETransmissionState.TRANSMITTING:
                        circleColor = 0xFF00FF00; // Green - active
                        break;
                    case GRAD_Endgame_ETransmissionState.INTERRUPTED:
                        circleColor = 0xFFFF8800; // Orange - interrupted
                        break;
                    case GRAD_Endgame_ETransmissionState.DONE:
                        circleColor = 0xFF0080FF; // Blue - completed
                        break;
                    case GRAD_Endgame_ETransmissionState.OFF:
                    default:
                        circleColor = 0xFF808080; // Gray - off
                        break;
                }

                // Draw center dot at fixed screen size (does not scale with zoom)
                float screenX, screenY;
                m_MapEntity.WorldToScreen(transMarker.position[0], transMarker.position[2], screenX, screenY, true);
                const float dotRadius = 8.0;
                DrawScreenCircle(screenX, screenY, dotRadius, 0xFF000000, 12);
                
                // Draw background ring
                DrawCircle(transMarker.position, 30.0, 3.0, 0x40000000); // Semi-transparent black background
                
                // Draw progress arc if transmitting or in progress
                if (transMarker.state == GRAD_Endgame_ETransmissionState.TRANSMITTING && transMarker.progress > 0)
                {
                    DrawProgressCircle(transMarker.position, 30.0, 3.0, transMarker.progress, circleColor);
                }
                else if (transMarker.state == GRAD_Endgame_ETransmissionState.DONE)
                {
                    // Full circle for completed
                    DrawCircle(transMarker.position, 30.0, 3.0, circleColor);
                }
                else
                {
                    // Partial circle for other states
                    DrawCircle(transMarker.position, 30.0, 3.0, circleColor);
                }
            }

            // Smoke clouds, HE flashes and AT rockets
            DrawExplosiveEvents(replayManager);

            // Draw progress bar during replay
            DrawProgressBar(replayManager);
        }
    }

    //------------------------------------------------------------------------------------------------
    // Renders the three explosive categories. Each is driven by the playback clock rather than by
    // frame snapshots, so they animate smoothly regardless of the recording interval.
    protected void DrawExplosiveEvents(GRAD_Endgame_ReplayManager replayManager)
    {
        array<ref GRAD_Endgame_ExplosiveEvent> activeEvents = {};
        float playbackTime = replayManager.GetPlaybackTime();

        int activeCount = replayManager.GetActiveExplosiveEvents(playbackTime, activeEvents);

        // Periodic diagnostic: distinguishes "no events recorded", "events recorded but none
        // active at this playback time" and "active but not drawing" - three very different bugs
        // that all look identical on screen.
        static int explosiveDrawTick = 0;
        explosiveDrawTick++;
        if (explosiveDrawTick % 60 == 0 && GRAD_Endgame_ReplayDebug.IsDebugMode() && m_MapEntity)
        {
            // Screen radius a smoke cloud resolves to at the current zoom. If this is only a few
            // pixels the marker is drawing correctly but is simply too small to see.
            Print(string.Format("GRAD_Endgame_ReplayMapLayer: zoom=%1 smoke %2m -> %3px",
                m_MapEntity.GetCurrentZoom(), SMOKE_RADIUS_M, SMOKE_RADIUS_M * m_MapEntity.GetCurrentZoom()), LogLevel.NORMAL);
        }

        if (explosiveDrawTick % 60 == 0 && GRAD_Endgame_ReplayDebug.IsDebugMode())
        {
            Print(string.Format("GRAD_Endgame_ReplayMapLayer: explosives t=%1s active=%2 total=%3",
                playbackTime, activeCount, replayManager.GetExplosiveEventCount()), LogLevel.NORMAL);
        }

        if (activeCount == 0)
            return;

        foreach (GRAD_Endgame_ExplosiveEvent evt : activeEvents)
        {
            switch (evt.kind)
            {
                case EGradEndgameExplosiveKind.SMOKE:
                    DrawSmokeCloud(evt, playbackTime);
                    break;

                case EGradEndgameExplosiveKind.HE:
                    DrawHeFlash(evt, playbackTime);
                    break;

                case EGradEndgameExplosiveKind.AT:
                    DrawAtRocket(evt, playbackTime);
                    break;
            }
        }
    }

    //------------------------------------------------------------------------------------------------
    // Smoke: a filled disc in the grenade's own colour that grows briefly as the cloud builds,
    // then fades out over the remainder of its life.
    protected void DrawSmokeCloud(GRAD_Endgame_ExplosiveEvent evt, float playbackTime)
    {
        float relStart = evt.startTime - GetReplayStartTime();
        float duration = evt.endTime - evt.startTime;
        if (duration <= 0)
            return;

        float age = playbackTime - relStart;
        float lifeFraction = Math.Clamp(age / duration, 0.0, 1.0);

        // Cloud builds over the first few seconds, then holds full size.
        const float buildUpSeconds = 4.0;
        float growth = Math.Clamp(age / buildUpSeconds, 0.0, 1.0);
        float radius = SMOKE_RADIUS_M * (0.35 + 0.65 * growth);

        // Hold opacity for the first half of its life, then fade to nothing.
        float alphaScale = 1.0;
        if (lifeFraction > 0.5)
            alphaScale = 1.0 - ((lifeFraction - 0.5) / 0.5);

        // The cloud must not obscure what is happening inside it, so the body is a very faint
        // wash and the outline carries most of the legibility. The outline is drawn at a much
        // higher alpha than the fill for that reason.
        int fillColor = ApplyAlpha(evt.color, SMOKE_FILL_ALPHA * alphaScale);
        int ringColor = ApplyAlpha(evt.color, SMOKE_RING_ALPHA * alphaScale);

        DrawWorldDisc(evt.position, radius, fillColor);
        DrawWorldRing(evt.position, radius, 2.0, ringColor);
    }

    //------------------------------------------------------------------------------------------------
    // HE: a brief ring that expands outward and fades, reading as a detonation rather than a
    // lingering presence. No persistent marker is left behind.
    protected void DrawHeFlash(GRAD_Endgame_ExplosiveEvent evt, float playbackTime)
    {
        float relStart = evt.startTime - GetReplayStartTime();

        // The recorded window is in replay-time, but playback is compressed - a long round is
        // squeezed into a minute, so a two second event can flash past in a fraction of a second.
        // Stretch the window by the playback speed so the animation lasts a consistent amount of
        // REAL time on screen no matter how fast the replay is running.
        float duration = HE_ANIMATION_SECONDS * GetPlaybackSpeedScale();
        if (duration <= 0)
            return;

        float progress = Math.Clamp((playbackTime - relStart) / duration, 0.0, 1.0);

        // Grow fast, settle back slowly. A detonation that expands and contracts reads as an
        // event even when it flashes past, which matters because the replay is time-compressed:
        // a two second window can be a fraction of a second on screen at playback speed.
        //
        // The curve peaks at PUNCH_PEAK then eases back, with the ease-out shaped by a squared
        // falloff so the contraction decelerates rather than snapping shut.
        float scale;
        if (progress < HE_PUNCH_PEAK)
        {
            // Ease-out on the way up: fast initial expansion, decelerating into the peak.
            float t = progress / HE_PUNCH_PEAK;
            scale = 1.0 - ((1.0 - t) * (1.0 - t));
        }
        else
        {
            // Ease-in-out on the way back down, settling toward a small residual.
            float t = (progress - HE_PUNCH_PEAK) / (1.0 - HE_PUNCH_PEAK);
            scale = 1.0 - (t * t * HE_CONTRACTION);
        }

        float radius = HE_MIN_RADIUS_M + (HE_MAX_RADIUS_M - HE_MIN_RADIUS_M) * scale;

        // Hold full opacity through the punch, then fade over the tail so it disappears cleanly.
        float alpha = 1.0;
        if (progress > HE_FADE_START)
            alpha = 1.0 - ((progress - HE_FADE_START) / (1.0 - HE_FADE_START));

        DrawWorldRing(evt.position, radius, 3.0, ApplyAlpha(HE_COLOR, alpha));
        DrawWorldDisc(evt.position, radius * 0.45, ApplyAlpha(HE_COLOR, alpha * 0.55));
    }

    //------------------------------------------------------------------------------------------------
    // AT: the rocket is animated along its flight path, trailing a line back toward the launch
    // point, then bursts at the impact position.
    protected void DrawAtRocket(GRAD_Endgame_ExplosiveEvent evt, float playbackTime)
    {
        float relStart = evt.startTime - GetReplayStartTime();
        float flightTime = evt.endTime - evt.startTime;

        int trailColor = ApplyAlpha(AT_COLOR, 0.55);

        if (flightTime > 0)
        {
            float progress = Math.Clamp((playbackTime - relStart) / flightTime, 0.0, 1.0);

            if (progress < 1.0)
            {
                // Rocket in flight: interpolate along the launch->impact line.
                vector current = vector.Lerp(evt.position, evt.endPosition, progress);

                // Short trail behind the rocket rather than a line all the way back to the shooter,
                // so the direction of travel reads clearly without cluttering the map.
                float trailStart = Math.Max(0.0, progress - AT_TRAIL_FRACTION);
                vector trailFrom = vector.Lerp(evt.position, evt.endPosition, trailStart);

                DrawLine(trailFrom, current, 2.0, trailColor);
                DrawWorldDisc(current, AT_ROCKET_RADIUS_M, AT_COLOR);
                return;
            }
        }

        // Flight finished: show the impact burst. Stretched by playback speed for the same
        // perceptual reason as the HE flash - see DrawHeFlash.
        float sinceImpact = playbackTime - (evt.endTime - GetReplayStartTime());
        float burstDuration = AT_BURST_SECONDS * GetPlaybackSpeedScale();
        float burstProgress = Math.Clamp(sinceImpact / burstDuration, 0.0, 1.0);

        // Same punch-and-settle shape as HE so the two read as related events, in AT's colour.
        float burstScale;
        if (burstProgress < HE_PUNCH_PEAK)
        {
            float tUp = burstProgress / HE_PUNCH_PEAK;
            burstScale = 1.0 - ((1.0 - tUp) * (1.0 - tUp));
        }
        else
        {
            float tDown = (burstProgress - HE_PUNCH_PEAK) / (1.0 - HE_PUNCH_PEAK);
            burstScale = 1.0 - (tDown * tDown * HE_CONTRACTION);
        }

        float alpha = 1.0;
        if (burstProgress > HE_FADE_START)
            alpha = 1.0 - ((burstProgress - HE_FADE_START) / (1.0 - HE_FADE_START));

        float radius = HE_MIN_RADIUS_M + (AT_BURST_RADIUS_M - HE_MIN_RADIUS_M) * burstScale;

        DrawWorldRing(evt.endPosition, radius, 3.0, ApplyAlpha(AT_COLOR, alpha));
        DrawWorldDisc(evt.endPosition, radius * 0.45, ApplyAlpha(AT_COLOR, alpha * 0.55));
    }

    //------------------------------------------------------------------------------------------------
    // Ring at a world position, sized in METRES. The DrawCircle(center, radius, width, colour)
    // overload takes its radius in screen pixels, so metre-based callers must convert first -
    // otherwise a 25m cloud is drawn as a 25px ring regardless of zoom.
    protected void DrawWorldRing(vector center, float radiusMeters, float width, int color)
    {
        DrawCircle(center, WorldRadiusToScreen(radiusMeters), width, color);
    }

    //------------------------------------------------------------------------------------------------
    // Metres to screen pixels at the current zoom, with a floor.
    //
    // A smoke cloud is only ~25m across, which is a couple of pixels on a zoomed-out map - drawn
    // correctly but far too small to notice. The floor keeps it visible as a small marker instead
    // of disappearing, while it still grows naturally as the map is zoomed in.
    protected float WorldRadiusToScreen(float radiusMeters)
    {
        float screenRadius = radiusMeters;
        if (m_MapEntity)
            screenRadius = radiusMeters * m_MapEntity.GetCurrentZoom();

        return Math.Max(screenRadius, MIN_EXPLOSIVE_SCREEN_RADIUS);
    }

    //------------------------------------------------------------------------------------------------
    // Filled disc at a world position, sized in metres so it scales with map zoom.
    protected void DrawWorldDisc(vector center, float radiusMeters, int color, int segments = 24)
    {
        float screenX, screenY;
        m_MapEntity.WorldToScreen(center[0], center[2], screenX, screenY, true);

        // Metres to screen pixels, with the same floor the rings use so a small cloud stays
        // visible when zoomed out instead of collapsing to nothing.
        float screenRadius = WorldRadiusToScreen(radiusMeters);

        PolygonDrawCommand cmd = new PolygonDrawCommand();
        cmd.m_iColor = color;
        cmd.m_Vertices = new array<float>;

        for (int i = 0; i < segments; i++)
        {
            float theta = i * (Math.PI2 / segments);
            cmd.m_Vertices.Insert(screenX + screenRadius * Math.Cos(theta));
            cmd.m_Vertices.Insert(screenY + screenRadius * Math.Sin(theta));
        }

        m_Commands.Insert(cmd);
    }

    //------------------------------------------------------------------------------------------------
    // Replaces the alpha channel of an ARGB colour with the given 0..1 scale.
    protected int ApplyAlpha(int argb, float alphaScale)
    {
        int alpha = Math.Round(Math.Clamp(alphaScale, 0.0, 1.0) * 255);
        return (alpha << 24) | (argb & 0x00FFFFFF);
    }

    //------------------------------------------------------------------------------------------------
    // Playback speed, used to stretch fixed-length animations so they last a consistent amount of
    // real time on screen regardless of how compressed the replay is. Never below 1.
    protected float GetPlaybackSpeedScale()
    {
        GRAD_Endgame_ReplayManager replayManager = GRAD_Endgame_ReplayManager.GetInstance();
        if (!replayManager)
            return 1.0;

        return Math.Max(1.0, replayManager.GetPlaybackSpeed());
    }

    //------------------------------------------------------------------------------------------------
    protected float GetReplayStartTime()
    {
        GRAD_Endgame_ReplayManager replayManager = GRAD_Endgame_ReplayManager.GetInstance();
        if (!replayManager)
            return 0;

        return replayManager.GetReplayStartTime();
    }

    // Core function to update a single marker widget
    protected void UpdateMarkerWidget(string key, string texturePath, vector worldPos, float direction, bool isVehicle, bool isEmptyVehicle, bool isDead = false)
    {
        // 1. Get/Create Widget
        ImageWidget w = GetOrCreateMarkerWidget(key, texturePath, isVehicle);
        if (!w) return;

        m_UsedWidgetKeys.Insert(key);

		// 2. World to Screen - Returns screen pixels
		float screenX, screenY;
		m_MapEntity.WorldToScreen(worldPos[0], worldPos[2], screenX, screenY, true);

		// One-shot sample log to diagnose coordinate mapping issues (avoid spamming logs)
		if (!m_bWorldToScreenSampled)
		{
			m_bWorldToScreenSampled = true;
			bool parentedToMapRoot = false;
			if (m_WidgetsRoot && m_MapEntity)
			{
				parentedToMapRoot = (m_MapEntity.GetMapMenuRoot() == m_WidgetsRoot);
			}
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayMapLayer: WorldToScreen sample - worldPos=[%1, %2, %3] -> screen=[%4, %5], parentedToMapRoot=%6", worldPos[0], worldPos[1], worldPos[2], screenX, screenY, parentedToMapRoot), LogLevel.NORMAL);
		}

        // 3. Position & Rotation
        // FIXED: Apply DPI unscaling to screen coordinates for proper positioning
        // Since alignment is 0.5, 0.5 (center), position is already the center point
        float posX = GetGame().GetWorkspace().DPIUnscale(screenX);
        float posY = GetGame().GetWorkspace().DPIUnscale(screenY);
        FrameSlot.SetPos(w, posX, posY);

        // Size tracks the current zoom, so it must be re-applied every frame rather than only on
        // widget creation - cached widgets would otherwise keep the size they were created at.
        ApplyMarkerSize(w, isVehicle);

        // Rotation: Input 'direction' is usually World Yaw.
        // Icons usually face UP.
        // World 0 (North) -> Screen UP.
        // If World Yaw increases Clockwise, Screen Rotation increases Clockwise.
        // Standard formula:
        float rot = direction;
        w.SetRotation(rot);

        // 4. Color / Opacity
        if (isEmptyVehicle) w.SetColor(Color.Gray); // Example tinting
        else w.SetColor(Color.White);

        // Dead units keep their faction colour but render faded, so they stay
        // identifiable while reading clearly as no longer alive.
        if (isDead) w.SetOpacity(DEAD_MARKER_OPACITY);
        else w.SetOpacity(1.0);
    }
	
	// Map vehicle prefab and faction to icon key
	protected string GetVehicleIconKey(string prefab, string factionKey, bool isEmpty)
	{
		if (factionKey.IsEmpty())
			factionKey = "Empty";

		// Normalize COALITION-Lobby's "OPFOR"/"BLUFOR" faction keys to BreakingContact's
		// own "USSR"/"US" convention used by every icon-key branch below.
		if (GRAD_Endgame_ReplayDebug.IsOpforFactionKey(factionKey))
			factionKey = "USSR";
		else if (GRAD_Endgame_ReplayDebug.IsBluforFactionKey(factionKey))
			factionKey = "US";


		if (prefab.IsEmpty()) return "";
		string key = "";
		string pf = prefab;
		pf.ToLower();
		
		// Debug logging for unmapped vehicles
		static int vehicleLogCount = 0;
		vehicleLogCount++;
		if (vehicleLogCount <= 20)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("Endgame Replay - GetVehicleIconKey: prefab='%1', faction='%2', isEmpty=%3", pf, factionKey, isEmpty), LogLevel.NORMAL);
		}
		// Special case: ambient/empty vehicles (no faction)
		if (factionKey == "Empty")
		{
			isEmpty = true;
		}
		// Normal logic for faction vehicles
		if (pf.Contains("lav"))
		{
			if (isEmpty) key = "LAV_empty";
			else if (factionKey == "US") key = "LAV_blufor";
			else if (factionKey == "USSR") key = "LAV_opfor";
			else if (factionKey == "CIV") key = "LAV_civ";
		}
		else if (pf.Contains("m151a2"))
		{
			if (pf.Contains("transport"))
			{
				if (isEmpty) key = "M151A2_closed_empty";
				else if (factionKey == "US") key = "M151A2_closed_blufor";
				else if (factionKey == "USSR") key = "M151A2_closed_opfor";
				else if (factionKey == "CIV") key = "M151A2_civ";
			}
			else if (pf.Contains("open"))
			{
				if (isEmpty) key = "M151A2_open_empty";
				else if (factionKey == "US") key = "M151A2_open_blufor";
				else if (factionKey == "USSR") key = "M151A2_open_opfor";
				else if (factionKey == "CIV") key = "M151A2_open_civ";
			}
			else
			{
				if (isEmpty) key = "M151A2_empty";
				else if (factionKey == "US") key = "M151A2_blufor";
				else if (factionKey == "USSR") key = "M151A2_opfor";
				else if (factionKey == "CIV") key = "M151A2_civ";
			}
		}
		else if (pf.Contains("m923"))
		{
			if (isEmpty) key = "M923A1_closed_empty";
			else if (factionKey == "US") key = "M923A1_closed_blufor";
			else if (factionKey == "USSR") key = "M923A1_closed_opfor";
			else if (factionKey == "CIV") key = "M923A1_closed_civ";
		}
		else if (pf.Contains("m998"))
		{
			if (pf.Contains("m2hb"))
			{
				if (isEmpty) key = "M998_M2HB_empty";
				else if (factionKey == "US") key = "M998_M2HB_blufor";
				else if (factionKey == "USSR") key = "M998_M2HB_opfor";
				else if (factionKey == "CIV") key = "M998_M2HB_civ";
			}
			else if (pf.Contains("transport"))
			{
				if (isEmpty) key = "M998_closed_empty";
				else if (factionKey == "US") key = "M998_closed_blufor";
				else if (factionKey == "USSR") key = "M998_closed_opfor";
				else if (factionKey == "CIV") key = "M998_closed_civ";
			}
			else
			{
				if (isEmpty) key = "M998_open_empty";
				else if (factionKey == "US") key = "M998_open_blufor";
				else if (factionKey == "USSR") key = "M998_open_opfor";
				else if (factionKey == "CIV") key = "M998_open_civ";
			}
		}
		else if (pf.Contains("uaz452") || pf.Contains("uaz_452"))
		{
			if (isEmpty) key = "UAZ_452_empty";
			else if (factionKey == "US") key = "UAZ_452_blufor";
			else if (factionKey == "USSR") key = "UAZ_452_opfor";
			else if (factionKey == "CIV") key = "UAZ_452_civ";
		}
		else if (pf.Contains("btr"))
		{
			if (isEmpty) key = "BTR_empty";
			else if (factionKey == "US") key = "BTR_blufor";
			else if (factionKey == "USSR") key = "BTR_opfor";
			else if (factionKey == "CIV") key = "BTR_civ";
		}
		else if (pf.Contains("s1203"))
		{
			if (isEmpty) key = "S1203_empty";
			else if (factionKey == "US") key = "S1203_blufor";
			else if (factionKey == "USSR") key = "S1203_opfor";
			else if (factionKey == "CIV") key = "S1203_civ";
		}
		else if (pf.Contains("s105"))
		{
			if (isEmpty) key = "S105_empty";
			else if (factionKey == "US") key = "S105_blufor";
			else if (factionKey == "USSR") key = "S105_opfor";
			else if (factionKey == "CIV") key = "S105_civ";
		}
		else if (pf.Contains("uaz469") || pf.Contains("uaz_469"))
		{
			if (pf.Contains("pkm"))
			{
				if (isEmpty) key = "UAZ_469_PKM_empty";
				else if (factionKey == "US") key = "UAZ_469_PKM_blufor";
				else if (factionKey == "USSR") key = "UAZ_469_PKM_opfor";
				else if (factionKey == "CIV") key = "UAZ_469_PKM_civ";
			}
			else if (pf.Contains("open"))
			{
				if (isEmpty) key = "UAZ_469_Open_empty";
				else if (factionKey == "US") key = "UAZ_469_Open_blufor";
				else if (factionKey == "USSR") key = "UAZ_469_Open_opfor";
				else if (factionKey == "CIV") key = "UAZ_469_Open_civ";
			}
			else
			{
				if (isEmpty) key = "UAZ_469_closed_empty";
				else if (factionKey == "US") key = "UAZ_469_closed_blufor";
				else if (factionKey == "USSR") key = "UAZ_469_closed_opfor";
				else if (factionKey == "CIV") key = "UAZ_469_closed_civ";
			}
		}
		else if (pf.Contains("uh1h"))
		{
			if (isEmpty) key = "UH1H1_empty";
			else if (factionKey == "US") key = "UH1H1_blufor";
			else if (factionKey == "USSR") key = "UH1H1_opfor";
			else if (factionKey == "CIV") key = "UH1H1_civ";
		}
		else if (pf.Contains("ural4320") || pf.Contains("ural_4320"))
		{
			if (pf.Contains("command") || pf.Contains("radio") || pf.Contains("r-142") || pf.Contains("r142"))
			{
				if (isEmpty) key = "Radiotruck_empty";
				else if (factionKey == "US") key = "Radiotruck_blufor";
				else if (factionKey == "USSR") key = "Radiotruck_opfor";
				else if (factionKey == "CIV") key = "Radiotruck_civ";
			}
			else if (pf.Contains("transport") || pf.Contains("covered"))
			{
				if (isEmpty) key = "Ural_empty";
				else if (factionKey == "US") key = "Ural_blufor";
				else if (factionKey == "USSR") key = "Ural_opfor";
				else if (factionKey == "CIV") key = "Ural_civ";
			}
			else
			{
				if (isEmpty) key = "Ural_Open_empty";
				else if (factionKey == "US") key = "Ural_Open_blufor";
				else if (factionKey == "USSR") key = "UralOpen_opfor";
				else if (factionKey == "CIV") key = "UralOpen_civ";
			}
		}
		
		// Log when no key is matched
		if (key == "" && vehicleLogCount <= 20)
		{
			Print(string.Format("BC Warning - GetVehicleIconKey: No icon key found for prefab='%1'", pf), LogLevel.WARNING);
		}
		return key;
	}

	// Vehicle icon textures
	protected ref map<string, string> m_vehicleIconTextures = new map<string, string>();
	// Replay display state
	protected ref array<ref GRAD_Endgame_ReplayPlayerMarker> m_playerMarkers = {};
	protected ref array<ref GRAD_Endgame_ReplayProjectileMarker> m_projectileMarkers = {};
	protected ref array<ref GRAD_Endgame_ReplayTransmissionMarker> m_transmissionMarkers = {};
	protected ref array<ref GRAD_Endgame_ReplayVehicleMarker> m_vehicleMarkers = {};
	
	// Replay mode flag - true when actively viewing replay, false for normal map use
	protected bool m_bIsInReplayMode = false;
	
	// Toggle filters for replay display
	protected bool m_bHideEmptyVehicles = false;
	protected bool m_bHideCivilians = false;
	
	// Public method to enable/disable replay mode
	void SetReplayMode(bool enabled)
	{
		m_bIsInReplayMode = enabled;
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayMapLayer: Replay mode set to %1", enabled), LogLevel.NORMAL);
		
		if (enabled)
			RegisterToggleInputs();
		else
			UnregisterToggleInputs();

		if (m_MapMenuRoot)
			SetReplayHintButtonsVisible(enabled);
	}

	//------------------------------------------------------------------------------------------------
	// Show or hide the replay hint buttons in the map footer
	protected void SetReplayHintButtonsVisible(bool visible)
	{
		if (!m_MapMenuRoot)
		{
			Print(string.Format("Endgame Replay - SetReplayHintButtonsVisible(%1): m_MapMenuRoot null (map already closed)", visible), LogLevel.SPAM);
			return;
		}

		// Walk up the widget tree until we find the ancestor that owns "OverlayFooter".
		// When the map is opened through PSCore's PS_SpectatorMenu, GetMapMenuRoot() returns an
		// inner frame rather than the top-level MapMenu, so a single GetParent() isn't
		// guaranteed to land on the right node.
		// NOTE: COA_SpectatorMenu's layout has no "OverlayFooter" widget at all, so under
		// COALITION-Lobby this loop never finds one and the replay hint toggles below are a
		// no-op (same graceful degrade as when the widget was simply missing before).
		Widget searchRoot = m_MapMenuRoot;
		Widget overlayFooter;
		while (searchRoot)
		{
			overlayFooter = searchRoot.FindAnyWidget("OverlayFooter");
			if (overlayFooter)
				break;
			searchRoot = searchRoot.GetParent();
		}

		Print(string.Format("Endgame Replay - SetReplayHintButtonsVisible(%1): root=%2 overlayFooter=%3", visible, m_MapMenuRoot.GetName(), overlayFooter != null), LogLevel.NORMAL);

		if (overlayFooter)
			overlayFooter.SetVisible(visible);

		if (overlayFooter)
		{
			Widget toggleVehicles = overlayFooter.FindAnyWidget("BC_ToggleEmptyVehicles");
			if (toggleVehicles)
				toggleVehicles.SetVisible(visible);

			Widget toggleCivilians = overlayFooter.FindAnyWidget("BC_ToggleCivilians");
			if (toggleCivilians)
				toggleCivilians.SetVisible(visible);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Register input listeners for replay toggle keys
	protected void RegisterToggleInputs()
	{
		if (m_bListenersRegistered)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayMapLayer: RegisterToggleInputs called but listeners already registered", LogLevel.NORMAL);
			return;
		}

		InputManager inputManager = GetGame().GetInputManager();
		if (!inputManager)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayMapLayer: RegisterToggleInputs - InputManager not available", LogLevel.WARNING);
			return;
		}

		// Ensure widgets and map are ready before registering inputs
		if (!m_WidgetsRoot || !m_MapEntity)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayMapLayer: RegisterToggleInputs - widgets or map not ready (widgets=%1, map=%2)", m_WidgetsRoot != null, m_MapEntity != null), LogLevel.WARNING);
			return;
		}

		inputManager.AddActionListener("GRAD_Endgame_ToggleEmptyVehicles", EActionTrigger.DOWN, OnToggleEmptyVehicles);
		inputManager.AddActionListener("GRAD_Endgame_ToggleCivilians", EActionTrigger.DOWN, OnToggleCivilians);
		m_bListenersRegistered = true;
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayMapLayer: RegisterToggleInputs - listeners registered: %1", m_bListenersRegistered), LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	// Unregister input listeners for replay toggle keys
	protected void UnregisterToggleInputs()
	{
		if (!m_bListenersRegistered)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayMapLayer: UnregisterToggleInputs called but no listeners were registered", LogLevel.NORMAL);
			return;
		}

		InputManager inputManager = GetGame().GetInputManager();
		if (!inputManager)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print("GRAD_Endgame_ReplayMapLayer: UnregisterToggleInputs - InputManager not available", LogLevel.WARNING);
			m_bListenersRegistered = false;
			return;
		}

		inputManager.RemoveActionListener("GRAD_Endgame_ToggleEmptyVehicles", EActionTrigger.DOWN, OnToggleEmptyVehicles);
		inputManager.RemoveActionListener("GRAD_Endgame_ToggleCivilians", EActionTrigger.DOWN, OnToggleCivilians);
		m_bListenersRegistered = false;
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayMapLayer: UnregisterToggleInputs - listeners removed", LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	// Toggle hiding empty vehicles
	protected void OnToggleEmptyVehicles()
	{
		if (!m_bIsInReplayMode)
			return;
		
		m_bHideEmptyVehicles = !m_bHideEmptyVehicles;
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayMapLayer: Hide empty vehicles: %1", m_bHideEmptyVehicles), LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	// Toggle hiding civilian faction markers
	protected void OnToggleCivilians()
	{
		if (!m_bIsInReplayMode)
			return;
		
		m_bHideCivilians = !m_bHideCivilians;
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayMapLayer: Hide civilians: %1", m_bHideCivilians), LogLevel.NORMAL);
	}
	
	//------------------------------------------------------------------------------------------------
	// Public getters for toggle states (used by vanilla available action conditions)
	bool IsInReplayMode()
	{
		return m_bIsInReplayMode;
	}
	
	bool IsHidingEmptyVehicles()
	{
		return m_bHideEmptyVehicles;
	}
	
	bool IsHidingCivilians()
	{
		return m_bHideCivilians;
	}
	
	// Keep last frame data for persistent display
	protected ref array<ref GRAD_Endgame_ReplayPlayerMarker> m_lastFramePlayerMarkers = {};
	protected ref array<ref GRAD_Endgame_ReplayProjectileMarker> m_lastFrameProjectileMarkers = {};
	protected ref array<ref GRAD_Endgame_ReplayTransmissionMarker> m_lastFrameTransmissionMarkers = {};
	protected ref array<ref GRAD_Endgame_ReplayVehicleMarker> m_lastFrameVehicleMarkers = {};
	protected bool m_hasLastFrame = false;
	
	// Unit type icon textures
	protected ref map<string, string> m_unitTypeTextures = new map<string, string>();
	
	// Cached loaded textures
	protected ref map<string, ref SharedItemRef> m_loadedTextures = new map<string, ref SharedItemRef>();
	
	// Transmission and radio truck icon paths
	protected ref map<string, string> m_transmissionIconPaths = new map<string, string>();
	
	//------------------------------------------------------------------------------------------------
	override void Init()
	{
		s_Instance = this;
		
		// Vehicle icon mapping
		m_vehicleIconTextures.Set("LAV_blufor", "{1D2C64090C772F84}UI/Textures/Icons/LAV_blufor.edds");
		m_vehicleIconTextures.Set("LAV_empty", "{AD78B1EE505E01FC}UI/Textures/Icons/LAV_empty.edds");
		m_vehicleIconTextures.Set("LAV_opfor", "{2F20DFA7DAF579E3}UI/Textures/Icons/LAV_opfor.edds");
		m_vehicleIconTextures.Set("LAV_civ", "{827076D9B9D20772}UI/Textures/Icons/LAV_civ.edds");

		m_vehicleIconTextures.Set("BTR_blufor", "{06FB93A5E254D29E}UI/Textures/Icons/BTR70_blufor.edds");
		m_vehicleIconTextures.Set("BTR_empty", "{4F0602CC01556B32}UI/Textures/Icons/BTR70_empty.edds");
		m_vehicleIconTextures.Set("BTR_opfor", "{CD5E6C858BFE132D}UI/Textures/Icons/BTR70_opfor.edds");
		m_vehicleIconTextures.Set("BTR_civ", "{69281F7FCBAE7D15}UI/Textures/Icons/BTR70_civ.edds");
		m_vehicleIconTextures.Set("BRDM_civ", "{8E3E453BF4A75D2A}UI/Textures/Icons/BRDM_civ.edds");

		m_vehicleIconTextures.Set("M151A2_blufor", "{F134996BEE493C80}UI/Textures/Icons/M151A2_blufor.edds");
		m_vehicleIconTextures.Set("M151A2_empty", "{3F818F4DDA023873}UI/Textures/Icons/M151A2_empty.edds");
		m_vehicleIconTextures.Set("M151A2_civ", "{17D3D81B53B15BA2}UI/Textures/Icons/M151A2_civ.edds");

		m_vehicleIconTextures.Set("M151A2_open_blufor", "{77AC2D8B6883E41F}UI/Textures/Icons/M151A2_open_blufor.edds");
		m_vehicleIconTextures.Set("M151A2_open_empty", "{CA8A6C10410526AA}UI/Textures/Icons/M151A2_open_empty.edds");
		m_vehicleIconTextures.Set("M151A2_open_opfor", "{48D20259CBAE5EB5}UI/Textures/Icons/M151A2_open_opfor.edds");
		m_vehicleIconTextures.Set("M151A2_open_civ", "{6FEE5FD13872E495}UI/Textures/Icons/M151A2_open_civ.edds");
		m_vehicleIconTextures.Set("M151A2_opfor", "{BDD9E10450A9406C}UI/Textures/Icons/M151A2_opfor.edds");
		
		m_vehicleIconTextures.Set("M923A1_closed_blufor", "{92B598738E1468A1}UI/Textures/Icons/M923A1_closed_blufor.edds");
		m_vehicleIconTextures.Set("M923A1_closed_empty", "{C2FD136933FF34FF}UI/Textures/Icons/M923A1_closed_empty.edds");
		m_vehicleIconTextures.Set("M923A1_closed_opfor", "{40A57D20B9544CE0}UI/Textures/Icons/M923A1_closed_opfor.edds");
		m_vehicleIconTextures.Set("M923A1_closed_civ", "{BB674722C29F5930}UI/Textures/Icons/M923A1_closed_civ.edds");

		m_vehicleIconTextures.Set("M998_closed_blufor", "{6BC2F3B2C876C86F}UI/Textures/Icons/M998_closed_blufor.edds");
		m_vehicleIconTextures.Set("M998_closed_empty", "{2F7D445FFCB4E602}UI/Textures/Icons/M998_closed_empty.edds");
		m_vehicleIconTextures.Set("M998_closed_opfor", "{AD252A16761F9E1D}UI/Textures/Icons/M998_closed_opfor.edds");
		m_vehicleIconTextures.Set("M998_closed_civ", "{720F1A9CDAF14450}UI/Textures/Icons/M998_closed_civ.edds");
		m_vehicleIconTextures.Set("M998_M2HB_blufor", "{BDE1B2689B13B6D4}UI/Textures/Icons/M998_M2HB_blufor.edds");
		m_vehicleIconTextures.Set("M998_M2HB_empty", "{91A96E1BFE6A80DE}UI/Textures/Icons/M998_M2HB_empty.edds");
		m_vehicleIconTextures.Set("M998_M2HB_opfor", "{13F1005274C1F8C1}UI/Textures/Icons/M998_M2HB_opfor.edds");
		m_vehicleIconTextures.Set("M998_M2HB_civ", "{670B9C5E7F097111}UI/Textures/Icons/M998_M2HB_civ.edds");
		m_vehicleIconTextures.Set("M998_open_blufor", "{13D8AB44ACDAB597}UI/Textures/Icons/M998_open_blufor.edds");
		m_vehicleIconTextures.Set("M998_open_empty", "{0D4CC53522A25D19}UI/Textures/Icons/M998_open_empty.edds");
		m_vehicleIconTextures.Set("M998_open_opfor", "{8F14AB7CA8092506}UI/Textures/Icons/M998_open_opfor.edds");
		m_vehicleIconTextures.Set("M998_open_civ", "{C3D7950D32B9AA42}UI/Textures/Icons/M998_open_civ.edds");

		m_vehicleIconTextures.Set("UAZ_452_opfor", "{D5FBFD6881DC4B58}UI/Textures/Icons/UAZ_452_opfor.edds");
		m_vehicleIconTextures.Set("UAZ_452_blufor", "{5731CADA4C68FBCF}UI/Textures/Icons/UAZ_452_blufor.edds");
		m_vehicleIconTextures.Set("UAZ_452_empty", "{57A393210B773347}UI/Textures/Icons/UAZ_452_empty.edds");
		m_vehicleIconTextures.Set("UAZ_452_civ", "{EE03A452BB1CB947}UI/Textures/Icons/UAZ_452_civ.edds");

		m_vehicleIconTextures.Set("UAZ_469_closed_blufor", "{613ACF2847FB8583}UI/Textures/Icons/UAZ_469 closed_blufor.edds");
		m_vehicleIconTextures.Set("UAZ_469_closed_empty", "{529E5BA357DB08E3}UI/Textures/Icons/UAZ_469 closed_empty.edds");
		m_vehicleIconTextures.Set("UAZ_469_closed_civ", "{212869E8ADEA2CB4}UI/Textures/Icons/UAZ_469_closed_civ.edds");
		m_vehicleIconTextures.Set("UAZ_469_Open_blufor", "{BE066D044B960605}UI/Textures/Icons/UAZ_469 Open_blufor.edds");
		m_vehicleIconTextures.Set("UAZ_469_Open_empty", "{2826A28533C37A7B}UI/Textures/Icons/UAZ_469 Open_empty.edds");
		m_vehicleIconTextures.Set("UAZ_469_Open_opfor", "{AA7ECCCCB9680264}UI/Textures/Icons/UAZ_469 Open_opfor.edds");
		m_vehicleIconTextures.Set("UAZ_469_Open_civ", "{DCCC9D184B70C18D}UI/Textures/Icons/UAZ_469 Open_civ.edds");
		m_vehicleIconTextures.Set("UAZ_469_PKM_blufor", "{8BF3272F9BF556DD}UI/Textures/Icons/UAZ_469 PKM_blufor.edds");
		m_vehicleIconTextures.Set("UAZ_469_PKM_empty", "{D3C0984F7BD3DE72}UI/Textures/Icons/UAZ_469 PKM_empty.edds");
		m_vehicleIconTextures.Set("UAZ_469_closed_opfor", "{90645BA8C54E7ABC}UI/Textures/Icons/UAZ_469_closed_opfor.edds");
		m_vehicleIconTextures.Set("UAZ_469_PKM_opfor", "{2B897E967D0AC339}UI/Textures/Icons/UAZ_469_PKM_opfor.edds");
		m_vehicleIconTextures.Set("UAZ_469_PKM_civ", "{8B123D01D64150A6}UI/Textures/Icons/UAZ_469_PKM_civ.edds");

		m_vehicleIconTextures.Set("UH1H1_blufor", "{A4FDF3DA7E1BF136}UI/Textures/Icons/UH1H1_blufor.edds");
		m_vehicleIconTextures.Set("UH1H1_empty", "{519C8DBD9918D6CC}UI/Textures/Icons/UH1H1_empty.edds");
		m_vehicleIconTextures.Set("UH1H1_opfor", "{D3C4E3F413B3AED3}UI/Textures/Icons/UH1H1_opfor.edds");
		m_vehicleIconTextures.Set("UH1H1_civ", "{8AF6C3FF1127D54E}UI/Textures/Icons/UH1H1_civ.edds");

		m_vehicleIconTextures.Set("Ural_blufor", "{87808F1F7D6839E4}UI/Textures/Icons/Ural_blufor.edds");
		m_vehicleIconTextures.Set("Ural_empty", "{8020A00CF179274D}UI/Textures/Icons/Ural_empty.edds");
		m_vehicleIconTextures.Set("Ural_civ", "{8E69A2A9E2146781}UI/Textures/Icons/Ural_civ.edds");
		m_vehicleIconTextures.Set("Ural_Open_blufor", "{16ED7010CF5FA66B}UI/Textures/Icons/Ural_Open_blufor.edds");
		m_vehicleIconTextures.Set("Ural_Open_empty", "{BD154D82B60DC8F0}UI/Textures/Icons/Ural_Open_empty.edds");
		m_vehicleIconTextures.Set("Ural_opfor", "{07E46F71B9B92027}UI/Textures/Icons/Ural_opfor.edds");
		m_vehicleIconTextures.Set("UralOpen_opfor", "{AD63B0D696D2577D}UI/Textures/Icons/UralOpen_opfor.edds");
		m_vehicleIconTextures.Set("UralOpen_civ", "{B4C769843A8E510F}UI/Textures/Icons/UralOpen_civ.edds");

		m_vehicleIconTextures.Set("Radiotruck_blufor", "{ABB9C00A86D1437D}UI/Textures/Icons/Radiotruck_blufor.edds");
		m_vehicleIconTextures.Set("Radiotruck_empty", "{AB02F6EFC1893111}UI/Textures/Icons/Radiotruck_empty.edds");
		m_vehicleIconTextures.Set("Radiotruck_opfor", "{295A98A64B22490E}UI/Textures/Icons/Radiotruck_opfor.edds");
		m_vehicleIconTextures.Set("Radiotruck_civ", "{6C972D729BAFD741}UI/Textures/Icons/Radiotruck_civ.edds");

		m_vehicleIconTextures.Set("Commandvehicle_blufor", "{2CCAAB5BEDD7BAA1}UI/Textures/Icons/Commandvehicle_blufor.edds");
		m_vehicleIconTextures.Set("Commandvehicle_empty", "{C2436C5A1B9CF72D}UI/Textures/Icons/Commandvehicle_Empty.edds");
		m_vehicleIconTextures.Set("Commandvehicle_opfor", "{401B021391378F32}UI/Textures/Icons/Commandvehicle_opfor.edds");
		m_vehicleIconTextures.Set("Commandvehicle_civ", "{666023B94A7FEA7D}UI/Textures/Icons/Commandvehicle_civ.edds");

		m_vehicleIconTextures.Set("S105_blufor", "{8705E23234AE5219}UI/Textures/Icons/S105_blufor.edds");
		m_vehicleIconTextures.Set("S105_empty", "{116570AE64B80F4E}UI/Textures/Icons/S105_empty.edds");
		m_vehicleIconTextures.Set("S105_opfor", "{933D1EE7EE137751}UI/Textures/Icons/S105_opfor.edds");
		m_vehicleIconTextures.Set("S105_civ", "{0BAAFBE11D405AB4}UI/Textures/Icons/S105_civ.edds");

		m_vehicleIconTextures.Set("S1203_blufor", "{E04DE98D3C890A8A}UI/Textures/Icons/S1203_blufor.edds");
		m_vehicleIconTextures.Set("S1203_empty", "{1040C8443019C220}UI/Textures/Icons/S1203_empty.edds");
		m_vehicleIconTextures.Set("S1203_opfor", "{9218A60DBAB2BA3F}UI/Textures/Icons/S1203_opfor.edds");
		m_vehicleIconTextures.Set("S1203_civ", "{5906B87982121DA7}UI/Textures/Icons/S1203_civ.edds");

		
	
		super.Init();
		
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayMapLayer: Initializing replay map layer", LogLevel.NORMAL);
		
		// Initialize transmission and radio truck icon paths
		m_transmissionIconPaths.Set("transmission_active", "{3E2F061E35D2DA76}UI/Textures/Icons/GRAD_BC_mapIcons.imageset:transmission_active");
		m_transmissionIconPaths.Set("transmission_default", "{3E2F061E35D2DA76}UI/Textures/Icons/GRAD_BC_mapIcons.imageset:transmission_default");
		m_transmissionIconPaths.Set("transmission_interrupted", "{3E2F061E35D2DA76}UI/Textures/Icons/GRAD_BC_mapIcons.imageset:transmission_interrupted");
		m_transmissionIconPaths.Set("radiotruck_active", "{3E2F061E35D2DA76}UI/Textures/Icons/GRAD_BC_mapIcons.imageset:radiotruck_active");
		
		// Initialize unit type textures with new faction-sensitive icons
		// Format: m_unitTypeTextures.Set("Role_Faction", "texturePath");
		// Factions: US (blufor), USSR (opfor), CIV (civ)
		// AntiTank
		m_unitTypeTextures.Set("AntiTank_US", "{ECC9BB91F97CEC7E}UI/Textures/Icons/iconman_at_blufor.edds");
		m_unitTypeTextures.Set("AntiTank_USSR", "{449232651E2D6688}UI/Textures/Icons/iconman_at_opfor.edds");
		m_unitTypeTextures.Set("AntiTank_CIV", "{EAD70F3FE70432A4}UI/Textures/Icons/iconman_at_civ.edds");
		m_unitTypeTextures.Set("AT_US", "{ECC9BB91F97CEC7E}UI/Textures/Icons/iconman_at_blufor.edds");
		m_unitTypeTextures.Set("AT_USSR", "{449232651E2D6688}UI/Textures/Icons/iconman_at_opfor.edds");
		m_unitTypeTextures.Set("AT_CIV", "{EAD70F3FE70432A4}UI/Textures/Icons/iconman_at_civ.edds");
		m_unitTypeTextures.Set("AAT_US", "{ECC9BB91F97CEC7E}UI/Textures/Icons/iconman_at_blufor.edds");
		m_unitTypeTextures.Set("AAT_USSR", "{449232651E2D6688}UI/Textures/Icons/iconman_at_opfor.edds");
		m_unitTypeTextures.Set("AAT_CIV", "{EAD70F3FE70432A4}UI/Textures/Icons/iconman_at_civ.edds");
		// Engineer
		m_unitTypeTextures.Set("Engineer_US", "{67FCB2D6D795807C}UI/Textures/Icons/iconman_engineer_blufor.edds");
		m_unitTypeTextures.Set("Engineer_USSR", "{0D1394432D5C8C2A}UI/Textures/Icons/iconman_engineer_opfor.edds");
		m_unitTypeTextures.Set("Engineer_CIV", "{9F9D2F059235FF68}UI/Textures/Icons/iconman_engineer_civ.edds");
		// Grenadier
		m_unitTypeTextures.Set("Grenadier_US", "{138C2049645E03C1}UI/Textures/Icons/iconman_explosive_blufor.edds");
		m_unitTypeTextures.Set("Grenadier_USSR", "{687A58A5F6834059}UI/Textures/Icons/iconman_explosive_opfor.edds");
		m_unitTypeTextures.Set("Grenadier_CIV", "{93CE7B5AB88B8235}UI/Textures/Icons/iconman_explosive_civ.edds");
		// Medic
		m_unitTypeTextures.Set("Medic_US", "{C6C1954CA9E57C00}UI/Textures/Icons/iconman_medic_blufor.edds");
		m_unitTypeTextures.Set("Medic_USSR", "{1C9664FDA31BF765}UI/Textures/Icons/iconman_medic_opfor.edds");
		m_unitTypeTextures.Set("Medic_CIV", "{5857673AF18884E7}UI/Textures/Icons/iconman_medic_civ.edds");
		// MachineGunner
		m_unitTypeTextures.Set("MachineGunner_US", "{3EA1BC7E0E0B0533}UI/Textures/Icons/iconman_mg_blufor.edds");
		m_unitTypeTextures.Set("MachineGunner_USSR", "{65CED072E552385C}UI/Textures/Icons/iconman_mg_opfor.edds");
		m_unitTypeTextures.Set("MachineGunner_CIV", "{FEF73C39377E824B}UI/Textures/Icons/iconman_mg_civ.edds");
		m_unitTypeTextures.Set("AR_US", "{3EA1BC7E0E0B0533}UI/Textures/Icons/iconman_mg_blufor.edds");
		m_unitTypeTextures.Set("AR_USSR", "{65CED072E552385C}UI/Textures/Icons/iconman_mg_opfor.edds");
		m_unitTypeTextures.Set("AR_CIV", "{FEF73C39377E824B}UI/Textures/Icons/iconman_mg_civ.edds");
		m_unitTypeTextures.Set("AutomaticRifleman_US", "{3EA1BC7E0E0B0533}UI/Textures/Icons/iconman_mg_blufor.edds");
		m_unitTypeTextures.Set("AutomaticRifleman_USSR", "{65CED072E552385C}UI/Textures/Icons/iconman_mg_opfor.edds");
		m_unitTypeTextures.Set("AutomaticRifleman_CIV", "{FEF73C39377E824B}UI/Textures/Icons/iconman_mg_civ.edds");
		// Rifleman
		m_unitTypeTextures.Set("Rifleman_US", "{9912E888E7CC2E28}UI/Textures/Icons/iconman_rifleman_blufor.edds");
		m_unitTypeTextures.Set("Rifleman_USSR", "{A3894E64547137A3}UI/Textures/Icons/iconman_rifleman_opfor.edds");
		m_unitTypeTextures.Set("Rifleman_CIV", "{97ECC0C7F6184F3F}UI/Textures/Icons/iconman_rifleman_civ.edds");
		// Squad Leader
		m_unitTypeTextures.Set("Leader_US", "{E146B4EA5F726807}UI/Textures/Icons/iconman_squadleader_blufor.edds");
		m_unitTypeTextures.Set("Leader_USSR", "{E32B1F3A2323E5C4}UI/Textures/Icons/iconman_squadleader_opfor.edds");
		m_unitTypeTextures.Set("Leader_CIV", "{94FC55A740EFF5F5}UI/Textures/Icons/iconman_squadleader_civ.edds");
		m_unitTypeTextures.Set("SL_US", "{E146B4EA5F726807}UI/Textures/Icons/iconman_squadleader_blufor.edds");
		m_unitTypeTextures.Set("SL_USSR", "{E32B1F3A2323E5C4}UI/Textures/Icons/iconman_squadleader_opfor.edds");
		m_unitTypeTextures.Set("SL_CIV", "{94FC55A740EFF5F5}UI/Textures/Icons/iconman_squadleader_civ.edds");
		// Team Leader
		m_unitTypeTextures.Set("TeamLeader_US", "{DEBB5CF0EDAEAA90}UI/Textures/Icons/iconman_teamleader_blufor.edds");
		m_unitTypeTextures.Set("TeamLeader_USSR", "{7043346D3A86D4AC}UI/Textures/Icons/iconman_teamleader_opfor.edds");
		m_unitTypeTextures.Set("TeamLeader_CIV", "{03ACA7A746ACE869}UI/Textures/Icons/iconman_teamleader_civ.edds");
		// Virtual
		m_unitTypeTextures.Set("Virtual_US", "{0E582F43978CE7F8}UI/Textures/Icons/iconman_virtual_blufor.edds");
		m_unitTypeTextures.Set("Virtual_USSR", "{3E17667584578998}UI/Textures/Icons/iconman_virtual_opfor.edds");
		m_unitTypeTextures.Set("Virtual_CIV", "{1D125ACAB33784C1}UI/Textures/Icons/iconman_virtual_civ.edds");
		// Recon/Sharpshooter/Spotter
		m_unitTypeTextures.Set("Sharpshooter_US", "{0F4868B3AFD5B26F}UI/Textures/Icons/iconmanrecon_blufor.edds");
		m_unitTypeTextures.Set("Sharpshooter_USSR", "{AD41A08D77783D67}UI/Textures/Icons/iconmanrecon_opfor.edds");
		m_unitTypeTextures.Set("Sharpshooter_CIV", "{CA0BFA20F232081B}UI/Textures/Icons/iconmanrecon_civ.edds");
		m_unitTypeTextures.Set("Recon_US", "{0F4868B3AFD5B26F}UI/Textures/Icons/iconmanrecon_blufor.edds");
		m_unitTypeTextures.Set("Recon_USSR", "{AD41A08D77783D67}UI/Textures/Icons/iconmanrecon_opfor.edds");
		m_unitTypeTextures.Set("Recon_CIV", "{CA0BFA20F232081B}UI/Textures/Icons/iconmanrecon_civ.edds");
		m_unitTypeTextures.Set("Spotter_US", "{0F4868B3AFD5B26F}UI/Textures/Icons/iconmanrecon_blufor.edds");
		m_unitTypeTextures.Set("Spotter_USSR", "{AD41A08D77783D67}UI/Textures/Icons/iconmanrecon_opfor.edds");
		m_unitTypeTextures.Set("Spotter_CIV", "{CA0BFA20F232081B}UI/Textures/Icons/iconmanrecon_civ.edds");
		// Officer fallback (no new icon provided)
		m_unitTypeTextures.Set("Officer_US", "{E146B4EA5F726807}UI/Textures/Icons/iconman_squadleader_blufor.edds");
		m_unitTypeTextures.Set("Officer_USSR", "{E32B1F3A2323E5C4}UI/Textures/Icons/iconman_squadleader_opfor.edds");
		m_unitTypeTextures.Set("Officer_CIV", "{94FC55A740EFF5F5}UI/Textures/Icons/iconman_squadleader_civ.edds");

		// Vehicle icons (unchanged)
		m_unitTypeTextures.Set("ArmedCar", "{EB7C826CD3D2BE53}UI/Textures/Icons/iconarmedcar_ca.edds");
		m_unitTypeTextures.Set("Car", "{AAEC4011C3FAAE79}UI/Textures/Icons/iconcar_ca.edds");
		m_unitTypeTextures.Set("Helicopter", "{7F728098B42A124F}UI/Textures/Icons/iconheli_ca.edds");
		m_unitTypeTextures.Set("HelicopterArmed", "{0729FDD4F156F1DD}UI/Textures/Icons/iconheliarmed_ca.edds");
		m_unitTypeTextures.Set("Truck", "{DDACA1439DB45633}UI/Textures/Icons/icontruck_ca.edds");
		m_unitTypeTextures.Set("Tank", "{CAC18FF5CC2A427D}UI/Textures/Icons/icontank_ca.edds");
		m_unitTypeTextures.Set("Plane", "{D9861CB8FECEC812}UI/Textures/Icons/iconplane_ca.edds");

		// Default fallback for unknown unit types
		m_unitTypeTextures.Set("Default", "{9912E888E7CC2E28}UI/Textures/Icons/iconman_rifleman_blufor.edds");
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print("GRAD_Endgame_ReplayMapLayer: Replay map layer ready with faction-sensitive unit type textures", LogLevel.NORMAL);
	}
	
	
	
	//------------------------------------------------------------------------------------------------
	// Draw an image texture at a world position
	override void DrawImage(vector center, int width, int height, SharedItemRef tex)
	{
		ImageDrawCommand cmd = new ImageDrawCommand();
		
		float xcp, ycp;        
		m_MapEntity.WorldToScreen(center[0], center[2], xcp, ycp, true);
		
		cmd.m_Position = Vector(xcp - (width/2), ycp - (height/2), 0);
		cmd.m_pTexture = tex;
		cmd.m_Size = Vector(width, height, 0);
		cmd.m_iFlags = WidgetFlags.BLEND;
		m_Commands.Insert(cmd);
	}
	
	//------------------------------------------------------------------------------------------------
	// Draw an image with a specific color tint
	void DrawImageColor(vector center, int width, int height, SharedItemRef tex, int color)
	{
		ImageDrawCommand cmd = new ImageDrawCommand();
		
		float xcp, ycp;        
		m_MapEntity.WorldToScreen(center[0], center[2], xcp, ycp, true);
		
		cmd.m_Position = Vector(xcp - (width/2), ycp - (height/2), 0);
		cmd.m_pTexture = tex;
		cmd.m_Size = Vector(width, height, 0);
		cmd.m_iColor = color;
		cmd.m_iFlags = WidgetFlags.BLEND;
		m_Commands.Insert(cmd);
	}
	
	//------------------------------------------------------------------------------------------------
	// Draw an image with rotation
	// Rotation happens around center of the image
	void DrawImageColorRotated(vector center, int width, int height, SharedItemRef tex, float rotationDegrees)
	{
		ImageDrawCommand cmd = new ImageDrawCommand();
		cmd.m_pTexture = tex;
		cmd.m_iColor = 0xFFFFFFFF;
		
		float xcp, ycp;        
		m_MapEntity.WorldToScreen(center[0], center[2], xcp, ycp, true);
		
		// Use m_Pivot to rotate around the center
		cmd.m_Position = Vector(xcp - (width * 0.5), ycp - (height * 0.5), 0);
		cmd.m_Size = Vector(width, height, 0);
		cmd.m_Pivot = Vector(width * 0.5, height * 0.5, 0);
		cmd.m_fRotation = rotationDegrees;
		cmd.m_iFlags = WidgetFlags.BLEND;
		
		m_Commands.Insert(cmd);
	}
	
	//------------------------------------------------------------------------------------------------
	// Draw a transmission/radio truck icon from texture path
	void DrawTransmissionIcon(vector center, int width, int height, string texturePath)
	{
		if (!m_Canvas || !texturePath)
			return;
			
		   float xcp, ycp;        
		m_MapEntity.WorldToScreen(center[0], center[2], xcp, ycp, true);
		
		   // Check if texture is already loaded, otherwise load and cache it
		   SharedItemRef texture = m_loadedTextures.Get(texturePath);
		   if (!texture)
		   {
			   texture = m_Canvas.LoadTexture(texturePath);
			   if (texture)
				   m_loadedTextures.Set(texturePath, texture);
		   }
		   if (!texture)
		   {
			   Print(string.Format("Endgame Replay - DrawTransmissionIcon: Fallback to circle for texturePath=%1", texturePath), LogLevel.WARNING);
			   DrawCircle(center, width * 0.3, 0xFFFFFFFF, 12);
			   return;
		   }
		   // Create image draw command
		   ImageDrawCommand cmd = new ImageDrawCommand();
		   cmd.m_Position = Vector(xcp - (width/2), ycp - (height/2), 0);
		   cmd.m_Size = Vector(width, height, 0);
		   cmd.m_iColor = 0xFFFFFFFF;
		   cmd.m_pTexture = texture;
		   cmd.m_iFlags = WidgetFlags.BLEND;
		   m_Commands.Insert(cmd);
	}
	
	//------------------------------------------------------------------------------------------------
	// Map vehicle prefab name to icon type
	
	//------------------------------------------------------------------------------------------------
	// Draw a unit marker with icon and directional chevron
	   protected void DrawUnitMarker(vector position, float direction, string unitType, string factionKey, bool isVehicle, bool isAlive, string vehicleIconKey = "")
	   {
		   float iconSize;
		   if (isVehicle)
			   iconSize = 64.0;
		   else
			   iconSize = 32.0;

		   string texturePath;
			if (isVehicle)
		    {
		        if (vehicleIconKey != "")
		        {
		            texturePath = m_vehicleIconTextures.Get(vehicleIconKey);
		        }
		    }
		    else // Is infantry
		    {
		        string roleStr = unitType;
		        if (roleStr == "")
		           roleStr = "Rifleman";

		        string key = roleStr + "_" + factionKey;
		        texturePath = m_unitTypeTextures.Get(key);

		        // CIV faction fallback: civilians don't have military roles, use Rifleman_CIV
		        if (texturePath == "" && factionKey == "CIV")
		            texturePath = m_unitTypeTextures.Get("Rifleman_CIV");
		    }

		    // Centralized fallback
		    if (texturePath == "")
		    {
		        if (isVehicle)
		        {
		             // Fallback for vehicle to a generic empty vehicle icon
		             texturePath = m_vehicleIconTextures.Get("Commandvehicle_empty");
		        } else {
		             texturePath = m_unitTypeTextures.Get("Default");
        		}
		        Print(string.Format("Endgame Replay - DrawUnitMarker: Using fallback icon for vehicle/unit. isVehicle: %1, unitType: %2, faction: %3, vehicleIconKey: %4", isVehicle, unitType, factionKey, vehicleIconKey), LogLevel.WARNING);
		    }


		   SharedItemRef texture = m_loadedTextures.Get(texturePath);
		   if (!texture && m_Canvas && texturePath != "")
		   {
			   texture = m_Canvas.LoadTexture(texturePath);
			   if (texture)
				   m_loadedTextures.Set(texturePath, texture);
		   }

		   int iconPixelSize;
		   if (isVehicle)
			   iconPixelSize = 128;
		   else
			   iconPixelSize = 64;

		   if (texture)
		   {
			   // Fix rotation: convert world yaw to map icon rotation
			   // float iconRotation = -direction + 90.0; // Map north-up, world yaw=0 east? Adjust as needed
			   // Print(string.Format("Endgame Replay - DrawUnitMarker: direction=%.2f, iconRotation=%.2f", direction, iconRotation), LogLevel.NORMAL);
			   DrawImageColorRotated(position, iconPixelSize, iconPixelSize, texture, direction);
		   }
		   else
		   {
			   Print(string.Format("Endgame Replay - DrawUnitMarker: Fallback to circle for texturePath=%1", texturePath), LogLevel.WARNING);
			   DrawCircle(position, iconSize * 0.3, 0xFFFFFFFF, 12);
		   }
	   }
	
	//------------------------------------------------------------------------------------------------
	// Called by replay manager to update marker positions
	void UpdateReplayFrame(GRAD_Endgame_ReplayFrame frame)
	{
		// Reduced logging frequency - only log every 20th frame
		static int frameUpdateCount = 0;
		frameUpdateCount++;
		if (frameUpdateCount % 20 == 0)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayMapLayer: Received frame with %1 players, %2 projectiles, %3 transmissions, %4 vehicles",
					frame.players.Count(), frame.projectiles.Count(), frame.transmissions.Count(), frame.vehicles.Count()), LogLevel.NORMAL);
		}

		// Build new marker arrays first (don't clear existing ones yet to avoid empty frames)
		array<ref GRAD_Endgame_ReplayPlayerMarker> newPlayerMarkers = {};
		array<ref GRAD_Endgame_ReplayProjectileMarker> newProjectileMarkers = {};
		array<ref GRAD_Endgame_ReplayTransmissionMarker> newTransmissionMarkers = {};
		array<ref GRAD_Endgame_ReplayVehicleMarker> newVehicleMarkers = {};
		
		// Create player markers
		foreach (GRAD_Endgame_PlayerSnapshot playerSnapshot : frame.players)
		{
			GRAD_Endgame_ReplayPlayerMarker marker = new GRAD_Endgame_ReplayPlayerMarker();
			marker.playerId = playerSnapshot.playerId;
			marker.playerName = playerSnapshot.playerName;
			marker.factionKey = playerSnapshot.factionKey;
			marker.position = playerSnapshot.position;
			marker.direction = playerSnapshot.angles[0]; // Yaw
			marker.isAlive = playerSnapshot.isAlive;
			marker.isInVehicle = playerSnapshot.isInVehicle;
			marker.vehicleId = playerSnapshot.vehicleId;
			marker.unitType = playerSnapshot.unitRole; // Infantry role from snapshot
			marker.vehicleType = playerSnapshot.vehicleType; // Vehicle type from snapshot
			marker.isVisible = true;
			
			// Debug: Log position and faction data for first few frames
			static int positionLogCount = 0;
			positionLogCount++;
			if (positionLogCount <= 10)
			{
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print(string.Format("GRAD_Endgame_ReplayMapLayer: Player %1 (%2) faction: '%3', position: [%4, %5, %6], direction: %7°, type: %8", 
						marker.playerId, marker.playerName, marker.factionKey, marker.position[0], marker.position[1], marker.position[2], 
						marker.direction, marker.unitType));
			}
			
			newPlayerMarkers.Insert(marker);
		}
		
		// Create projectile markers
		foreach (GRAD_Endgame_ProjectileSnapshot projSnapshot : frame.projectiles)
		{
			GRAD_Endgame_ReplayProjectileMarker marker = new GRAD_Endgame_ReplayProjectileMarker();
			marker.projectileType = projSnapshot.projectileType;
			marker.position = projSnapshot.position; // firing position
			marker.impactPosition = projSnapshot.impactPosition; // impact position
			marker.velocity = projSnapshot.velocity;
			marker.isVisible = true;
			
			// Debug log for first few projectiles
			static int projLogCount = 0;
			projLogCount++;
			if (projLogCount <= 5)
			{
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print(string.Format("GRAD_Endgame_ReplayMapLayer: Projectile %1 - Type: %2, From: [%3, %4, %5], To: [%6, %7, %8]",
						projLogCount, marker.projectileType,
						marker.position[0], marker.position[1], marker.position[2],
						marker.impactPosition[0], marker.impactPosition[1], marker.impactPosition[2]));
			}
			
			newProjectileMarkers.Insert(marker);
		}
		
		// Create transmission markers
		foreach (GRAD_Endgame_TransmissionSnapshot transSnapshot : frame.transmissions)
		{
			GRAD_Endgame_ReplayTransmissionMarker marker = new GRAD_Endgame_ReplayTransmissionMarker();
			marker.position = transSnapshot.position;
			marker.state = transSnapshot.state;
			marker.progress = transSnapshot.progress;
			marker.isVisible = true;
			
			// Debug log for first few transmissions
			static int transLogCount = 0;
			transLogCount++;
			if (transLogCount <= 5)
			{
				if (GRAD_Endgame_ReplayDebug.IsDebugMode())
					Print(string.Format("GRAD_Endgame_ReplayMapLayer: Transmission %1 - State: %2, Progress: %3%%, Position: [%4, %5, %6]",
						transLogCount, marker.state, marker.progress * 100,
						marker.position[0], marker.position[1], marker.position[2]));
			}
			
			newTransmissionMarkers.Insert(marker);
		}
		
		// Create vehicle markers
		foreach (GRAD_Endgame_VehicleSnapshot vehicleSnapshot : frame.vehicles)
		{
			GRAD_Endgame_ReplayVehicleMarker marker = new GRAD_Endgame_ReplayVehicleMarker();
			marker.entityId = vehicleSnapshot.entityId;
			marker.vehicleType = vehicleSnapshot.vehicleType;
			marker.factionKey = vehicleSnapshot.factionKey;
			marker.position = vehicleSnapshot.position;
			marker.direction = vehicleSnapshot.angles[0]; // Yaw
			marker.isVisible = true;
			marker.isEmpty = vehicleSnapshot.m_bIsEmpty;
			marker.wasUsed = vehicleSnapshot.m_bWasUsed;
			newVehicleMarkers.Insert(marker);
		}
		
		// PERFORMANCE FIX: Before swapping, save current markers as last frame (by reference, not deep copy)
		// This is much faster than deep copying all marker data
		if (m_playerMarkers.Count() > 0 || m_projectileMarkers.Count() > 0 || m_transmissionMarkers.Count() > 0 || m_vehicleMarkers.Count() > 0)
		{
			m_lastFramePlayerMarkers = m_playerMarkers;
			m_lastFrameProjectileMarkers = m_projectileMarkers;
			m_lastFrameTransmissionMarkers = m_transmissionMarkers;
			m_lastFrameVehicleMarkers = m_vehicleMarkers;
			m_hasLastFrame = true;
		}

		// Now atomically swap the marker arrays to avoid empty frames during rendering
		m_playerMarkers = newPlayerMarkers;
		m_projectileMarkers = newProjectileMarkers;
		m_transmissionMarkers = newTransmissionMarkers;
		m_vehicleMarkers = newVehicleMarkers;

		m_bIsInReplayMode = true; // Mark that we're in replay mode

		// Reduced logging
		if (frameUpdateCount % 20 == 0)
		{
			if (GRAD_Endgame_ReplayDebug.IsDebugMode())
				Print(string.Format("GRAD_Endgame_ReplayMapLayer: Updated frame with %1 players, %2 vehicles (last frame: %3 players)",
					m_playerMarkers.Count(), m_vehicleMarkers.Count(), m_lastFramePlayerMarkers.Count()), LogLevel.NORMAL);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Draw progress bar showing replay progress at top of map
	void DrawProgressBar(GRAD_Endgame_ReplayManager replayManager)
	{
		if (!replayManager || !m_MapEntity)
			return;
			
		float progress = replayManager.GetReplayProgress();
		float currentTime = replayManager.GetCurrentReplayTime();
		float totalTime = replayManager.GetTotalReplayDuration();
		float playbackSpeed = replayManager.GetPlaybackSpeed();
		
		// Get screen dimensions
		float screenW, screenH;
		m_MapEntity.GetMapWidget().GetScreenSize(screenW, screenH);
		
		// Progress bar dimensions and position
		float barWidth = screenW - 100; // Leave margins
		const float barHeight = 25;
		const float barX = 50; // Left margin
		const float barY = 20; // Top margin
		const float cornerRadius = 12; // Rounded corner size
		
		// Draw background with rounded corners (dark semi-transparent)
		// Main background rectangle
		PolygonDrawCommand bgBar = new PolygonDrawCommand();
		bgBar.m_iColor = 0xCC1A1A1A; // Dark gray semi-transparent
		bgBar.m_Vertices = new array<float>;
		bgBar.m_Vertices.Insert(barX + cornerRadius);
		bgBar.m_Vertices.Insert(barY);
		bgBar.m_Vertices.Insert(barX + barWidth - cornerRadius);
		bgBar.m_Vertices.Insert(barY);
		bgBar.m_Vertices.Insert(barX + barWidth - cornerRadius);
		bgBar.m_Vertices.Insert(barY + barHeight);
		bgBar.m_Vertices.Insert(barX + cornerRadius);
		bgBar.m_Vertices.Insert(barY + barHeight);
		m_Commands.Insert(bgBar);
		
		// Draw progress fill with rounded corners (blue)
		int fillWidth = barWidth * progress;
		if (fillWidth > cornerRadius * 2) // Only draw if there's enough space for corners
		{
			PolygonDrawCommand fillBar = new PolygonDrawCommand();
			fillBar.m_iColor = 0xFFD18D1F; // Blue color for active tint
			fillBar.m_Vertices = new array<float>;
			fillBar.m_Vertices.Insert(barX + cornerRadius);
			fillBar.m_Vertices.Insert(barY);
			fillBar.m_Vertices.Insert(barX + fillWidth - cornerRadius);
			fillBar.m_Vertices.Insert(barY);
			fillBar.m_Vertices.Insert(barX + fillWidth - cornerRadius);
			fillBar.m_Vertices.Insert(barY + barHeight);
			fillBar.m_Vertices.Insert(barX + cornerRadius);
			fillBar.m_Vertices.Insert(barY + barHeight);
			m_Commands.Insert(fillBar);
			
			// Draw rounded left edge of fill
			DrawScreenCircle(barX + cornerRadius, barY + barHeight/2, cornerRadius, 0xFF3A7FD5, 8);
			
			// Draw rounded right edge of fill if progress is not complete
			if (progress < 0.99)
			{
				DrawScreenCircle(barX + fillWidth - cornerRadius, barY + barHeight/2, cornerRadius, 0xFF3A7FD5, 8);
			}
		}
		
		// Draw rounded corners for background using circles
		DrawScreenCircle(barX + cornerRadius, barY + barHeight/2, cornerRadius, 0xCC1A1A1A, 8);
		DrawScreenCircle(barX + barWidth - cornerRadius, barY + barHeight/2, cornerRadius, 0xCC1A1A1A, 8);
		
		// Draw border (white) with rounded effect
		LineDrawCommand border = new LineDrawCommand();
		border.m_iColor = 0xFFFFFFFF;
		border.m_fWidth = 2;
		border.m_Vertices = new array<float>;
		border.m_Vertices.Insert(barX);
		border.m_Vertices.Insert(barY);
		border.m_Vertices.Insert(barX + barWidth);
		border.m_Vertices.Insert(barY);
		border.m_Vertices.Insert(barX + barWidth);
		border.m_Vertices.Insert(barY + barHeight);
		border.m_Vertices.Insert(barX);
		border.m_Vertices.Insert(barY + barHeight);
		border.m_bShouldEnclose = true;
		m_Commands.Insert(border);
		
		// Add text display (time and speed)
		// Format time as MM:SS
		int currentMin = Math.Floor(currentTime / 60);
		int currentSec = Math.Floor(currentTime - (currentMin * 60));
		int totalMin = Math.Floor(totalTime / 60);
		int totalSec = Math.Floor(totalTime - (totalMin * 60));
		
		// Create formatted time strings
		string currentMinStr = currentMin.ToString();
		if (currentMin < 10) currentMinStr = "0" + currentMinStr;
		string currentSecStr = currentSec.ToString();
		if (currentSec < 10) currentSecStr = "0" + currentSecStr;
		string totalMinStr = totalMin.ToString();
		if (totalMin < 10) totalMinStr = "0" + totalMinStr;
		string totalSecStr = totalSec.ToString();
		if (totalSec < 10) totalSecStr = "0" + totalSecStr;
		
		string timeText = string.Format("REPLAY: %1:%2 / %3:%4 (%.1fx speed)", 
			currentMinStr, currentSecStr, totalMinStr, totalSecStr, playbackSpeed);
		
		// Note: Text rendering in map layers is limited, so we'll use the progress bar as the primary indicator
		// The time display will be in the replay controls widget
		
		if (GRAD_Endgame_ReplayDebug.IsDebugMode())
			Print(string.Format("GRAD_Endgame_ReplayMapLayer: Progress bar drawn - %.1f%% complete (%1:%2/%3:%4)", 
				progress * 100, currentMinStr, currentSecStr, totalMinStr, totalSecStr), LogLevel.VERBOSE);
	}
	
	//------------------------------------------------------------------------------------------------
	// Overloaded DrawCircle with width parameter for drawing rings
	void DrawCircle(vector center, float radius, float width, int color)
	{
		float screenX, screenY;
		m_MapEntity.WorldToScreen(center[0], center[2], screenX, screenY, true);
		
		const int segments = 32;
		
		for (int i = 0; i < segments; i++)
		{
			float angle1 = ((float)i / (float)segments) * Math.PI2;
			float angle2 = ((float)(i + 1) / (float)segments) * Math.PI2;
			
			float x1outer = screenX + Math.Cos(angle1) * (radius + width * 0.5);
			float y1outer = screenY + Math.Sin(angle1) * (radius + width * 0.5);
			float x2outer = screenX + Math.Cos(angle2) * (radius + width * 0.5);
			float y2outer = screenY + Math.Sin(angle2) * (radius + width * 0.5);
			
			float x1inner = screenX + Math.Cos(angle1) * (radius - width * 0.5);
			float y1inner = screenY + Math.Sin(angle1) * (radius - width * 0.5);
			float x2inner = screenX + Math.Cos(angle2) * (radius - width * 0.5);
			float y2inner = screenY + Math.Sin(angle2) * (radius - width * 0.5);
			
			PolygonDrawCommand cmd = new PolygonDrawCommand();
			cmd.m_iColor = color;
			cmd.m_Vertices = new array<float>;
			
			cmd.m_Vertices.Insert(x1outer); cmd.m_Vertices.Insert(y1outer);
			cmd.m_Vertices.Insert(x2outer); cmd.m_Vertices.Insert(y2outer);
			cmd.m_Vertices.Insert(x1inner); cmd.m_Vertices.Insert(y1inner);
			
			m_Commands.Insert(cmd);
			
			PolygonDrawCommand cmd2 = new PolygonDrawCommand();
			cmd2.m_iColor = color;
			cmd2.m_Vertices = new array<float>;
			
			cmd2.m_Vertices.Insert(x2outer); cmd2.m_Vertices.Insert(y2outer);
			cmd2.m_Vertices.Insert(x2inner); cmd2.m_Vertices.Insert(y2inner);
			cmd2.m_Vertices.Insert(x1inner); cmd2.m_Vertices.Insert(y1inner);
			
			m_Commands.Insert(cmd2);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Helper to draw a circle in screen coordinates (for UI elements like progress bar)
	void DrawScreenCircle(float screenX, float screenY, float radius, int color, int n = 16)
	{
		PolygonDrawCommand cmd = new PolygonDrawCommand();
		cmd.m_iColor = color;
		cmd.m_Vertices = new array<float>;
		
		for(int i = 0; i < n; i++)
		{
			float theta = i*(2*Math.PI/n);
			float x = screenX + radius*Math.Cos(theta);
			float y = screenY + radius*Math.Sin(theta);
			cmd.m_Vertices.Insert(x);
			cmd.m_Vertices.Insert(y);
		}
		m_Commands.Insert(cmd);
	}
	
	//------------------------------------------------------------------------------------------------
	// Override DrawLine to apply DPIUnscale for projectiles
	override void DrawLine(vector startPos, vector endPos, float width, int color)
	{
		PolygonDrawCommand cmd = new PolygonDrawCommand();
		cmd.m_iColor = color;
		
		cmd.m_Vertices = new array<float>;
		
		float x1, y1, x2, y2;
		m_MapEntity.WorldToScreen(startPos[0], startPos[2], x1, y1, true);
		m_MapEntity.WorldToScreen(endPos[0], endPos[2], x2, y2, true);
		
		// Create a thick line by drawing a rectangle
		float dx = x2 - x1;
		float dy = y2 - y1;
		float length = Math.Sqrt(dx * dx + dy * dy);
		
		if (length > 0)
		{
			float offsetX = (-dy / length) * (width / 2.0);
			float offsetY = (dx / length) * (width / 2.0);
			
			cmd.m_Vertices.Insert(x1 + offsetX); cmd.m_Vertices.Insert(y1 + offsetY);
			cmd.m_Vertices.Insert(x2 + offsetX); cmd.m_Vertices.Insert(y2 + offsetY);
			cmd.m_Vertices.Insert(x2 - offsetX); cmd.m_Vertices.Insert(y2 - offsetY);
			cmd.m_Vertices.Insert(x1 - offsetX); cmd.m_Vertices.Insert(y1 - offsetY);
		}
		
		m_Commands.Insert(cmd);
	}
	
	//------------------------------------------------------------------------------------------------
	// Draw a progress circle/arc around a point showing percentage complete (0.0 to 1.0)
	void DrawProgressCircle(vector center, float radius, float width, float progress, int color)
	{
		// Clamp progress to valid range
		progress = Math.Clamp(progress, 0.0, 1.0);
		
		float screenX, screenY;
		m_MapEntity.WorldToScreen(center[0], center[2], screenX, screenY, true);
		
		// Number of segments for the arc (more = smoother circle)
		const int totalSegments = 32;
		int progressSegments = Math.Max(1, (int)(totalSegments * progress));
		
		// Draw the arc as a series of triangles
		for (int i = 0; i < progressSegments; i++)
		{
			float angle1 = ((float)i / (float)totalSegments) * Math.PI2 - Math.PI_HALF; // Start from top
			float angle2 = ((float)(i + 1) / (float)totalSegments) * Math.PI2 - Math.PI_HALF;
			
			// Outer vertices
			float x1outer = screenX + Math.Cos(angle1) * (radius + width * 0.5);
			float y1outer = screenY + Math.Sin(angle1) * (radius + width * 0.5);
			float x2outer = screenX + Math.Cos(angle2) * (radius + width * 0.5);
			float y2outer = screenY + Math.Sin(angle2) * (radius + width * 0.5);
			
			// Inner vertices
			float x1inner = screenX + Math.Cos(angle1) * (radius - width * 0.5);
			float y1inner = screenY + Math.Sin(angle1) * (radius - width * 0.5);
			float x2inner = screenX + Math.Cos(angle2) * (radius - width * 0.5);
			float y2inner = screenY + Math.Sin(angle2) * (radius - width * 0.5);
			
			// Draw quad as two triangles
			PolygonDrawCommand cmd = new PolygonDrawCommand();
			cmd.m_iColor = color;
			cmd.m_Vertices = new array<float>;
			
			// Triangle 1
			cmd.m_Vertices.Insert(x1outer); cmd.m_Vertices.Insert(y1outer);
			cmd.m_Vertices.Insert(x2outer); cmd.m_Vertices.Insert(y2outer);
			cmd.m_Vertices.Insert(x1inner); cmd.m_Vertices.Insert(y1inner);
			
			m_Commands.Insert(cmd);
			
			// Triangle 2
			PolygonDrawCommand cmd2 = new PolygonDrawCommand();
			cmd2.m_iColor = color;
			cmd2.m_Vertices = new array<float>;
			
			cmd2.m_Vertices.Insert(x2outer); cmd2.m_Vertices.Insert(y2outer);
			cmd2.m_Vertices.Insert(x2inner); cmd2.m_Vertices.Insert(y2inner);
			cmd2.m_Vertices.Insert(x1inner); cmd2.m_Vertices.Insert(y1inner);
			
			m_Commands.Insert(cmd2);
		}
	}
}

// Marker classes for replay display
class GRAD_Endgame_ReplayPlayerMarker : Managed
{
	int playerId;
	string playerName;
	string factionKey;
	vector position;
	float direction;
	bool isAlive;
	bool isInVehicle;
	RplId vehicleId;
	string unitType; // Infantry unit type (Rifleman, Medic, etc.)
	string vehicleType; // Vehicle prefab name when in vehicle
	bool isVisible;
}

class GRAD_Endgame_ReplayProjectileMarker : Managed
{
	string projectileType;
	vector position; // firing position
	vector impactPosition; // impact/endpoint position
	vector velocity;
	bool isVisible;
}

class GRAD_Endgame_ReplayTransmissionMarker : Managed
{
	vector position;
	GRAD_Endgame_ETransmissionState state;
	float progress; // 0.0 to 1.0
	bool isVisible;
}

class GRAD_Endgame_ReplayVehicleMarker : Managed
{
    RplId entityId;
    string vehicleType;
    string factionKey;
    vector position;
	float direction;
	bool isVisible;
	bool isEmpty;
	bool wasUsed;
}
