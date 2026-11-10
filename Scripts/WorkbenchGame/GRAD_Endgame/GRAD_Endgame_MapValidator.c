#ifdef WORKBENCH
//------------------------------------------------------------------------------------------------
//! Endgame map check (M7). In the World Editor: Plugins > GRAD Endgame > Validate Endgame map.
//! Prints a PASS / WARN / FAIL list, copies it to the clipboard and shows it in a dialog.
//!
//! How it finds things: the Endgame components register themselves in static lists in OnPostInit,
//! which also runs for the entities of the world open in the editor. So the check reads those
//! lists and needs no entity search API. If a list is empty although the prefabs are placed,
//! reload the world once (the lists fill as the world is built).
//!
//! What it cannot check: navmesh at the sites and the real AI frame cost. Those need a play test.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-10): not compiled, not run. Plugin skeleton copied from
//! COALITION's mission plugins (WorkbenchPluginAttribute, Run, Workbench.GetModule(WorldEditor),
//! FindEntityByName, SourceToEntity). Unverified: Workbench.Dialog.
[WorkbenchPluginAttribute(
	name: "Validate Endgame map",
	description: "Check that this world has everything an Endgame match needs",
	shortcut: "",
	wbModules: { "WorldEditor" },
	category: "GRAD Endgame",
	awesomeFontCode: 0xF00C)
]
class GRAD_Endgame_MapValidatorPlugin : WorkbenchPlugin
{
	protected const float MIN_FOB_DISTANCE = 1000;          //!< metres between the two FOBs
	protected const float MIN_RADIO_TO_FOB = 500;           //!< metres from any radio site to either FOB
	protected const float MIN_SITE_SPACING = 150;           //!< metres between two document sites

	protected ref array<string> m_aLines = {};
	protected int m_iFails;
	protected int m_iWarns;

	//------------------------------------------------------------------------------------------------
	override void Run()
	{
		m_aLines.Clear();
		m_iFails = 0;
		m_iWarns = 0;

		CheckLobby();
		GRAD_Endgame_Config cfg = CheckConfig();
		if (cfg)
		{
			GRAD_Endgame_FobComponent fobOne, fobTwo;
			CheckFobs(cfg, fobOne, fobTwo);
			CheckDocumentSites(cfg, fobOne, fobTwo);
			CheckRadioSites(cfg, fobOne, fobTwo);
			CheckCipher(cfg);
			CheckGarrisons(cfg);
		}

		string summary = string.Format("%1 failed, %2 warnings", m_iFails, m_iWarns);
		m_aLines.InsertAt("Endgame map check: " + summary, 0);

		string text = SCR_StringHelper.Join("\n", m_aLines);
		foreach (string line : m_aLines)
			Print("[GRAD_Endgame] " + line, LogLevel.NORMAL);

		System.ExportToClipboard(text);
		Workbench.Dialog("Endgame map check (copied to clipboard)", text);
	}

	//------------------------------------------------------------------------------------------------
	protected void Pass(string text)
	{
		m_aLines.Insert("PASS  " + text);
	}

	//------------------------------------------------------------------------------------------------
	protected void Warn(string text)
	{
		m_iWarns++;
		m_aLines.Insert("WARN  " + text);
	}

	//------------------------------------------------------------------------------------------------
	protected void Fail(string text)
	{
		m_iFails++;
		m_aLines.Insert("FAIL  " + text);
	}

	//------------------------------------------------------------------------------------------------
	protected void CheckLobby()
	{
		WorldEditor worldEditor = Workbench.GetModule(WorldEditor);
		if (!worldEditor)
		{
			Fail("World Editor is not open.");
			return;
		}

		WorldEditorAPI api = worldEditor.GetApi();
		IEntitySource lobbySource = api.FindEntityByName("COA_Lobby");
		if (!lobbySource)
		{
			Fail("No entity named COA_Lobby (the COALITION game mode).");
			return;
		}

		COA_Gamemode gamemode = COA_Gamemode.Cast(api.SourceToEntity(lobbySource));
		if (!gamemode)
		{
			Fail("COA_Lobby is not a COA_Gamemode.");
			return;
		}
		Pass("COALITION lobby found.");

		if (!gamemode.FindComponent(GRAD_Endgame_GameModeComponent))
			Fail("COA_Lobby has no GRAD_Endgame_GameModeComponent: no phases, no win condition.");
		else
			Pass("Endgame game-mode component on the lobby.");

		if (gamemode.m_iTimeLimitMinutes != 60)
			Warn(string.Format("Time limit is %1 min; the agreed match length is 60.", gamemode.m_iTimeLimitMinutes));
		if (gamemode.m_bWaveRespawn)
			Fail("COALITION wave respawn is on; it must be off, Endgame runs its own waves.");
		if (!gamemode.m_bRespawnEnabled)
			Fail("COALITION respawn is off; it must be on for the respawn menu.");
		if (gamemode.m_bLockUnusedSlots)
			Warn("Disable JIP is on; Endgame allows joining in phases 1 and 2 and locks slots itself in phase 3.");
	}

	//------------------------------------------------------------------------------------------------
	protected GRAD_Endgame_Config CheckConfig()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
		{
			Fail("Config resource does not load: " + GRAD_Endgame_ConfigLoader.CONFIG_PATH);
			return null;
		}
		Pass("Config loads.");

		if (cfg.GetDebugTimeScale() != 1)
			Warn(string.Format("Debug time scale is %1; set it to 1 before a real match.", cfg.GetDebugTimeScale()));
		if (cfg.GetDebugLog())
			Warn("Debug log is on.");
		return cfg;
	}

	//------------------------------------------------------------------------------------------------
	protected void CheckFobs(notnull GRAD_Endgame_Config cfg, out GRAD_Endgame_FobComponent fobOne, out GRAD_Endgame_FobComponent fobTwo)
	{
		int countOne, countTwo;
		foreach (GRAD_Endgame_FobComponent fob : GRAD_Endgame_FobComponent.GetAll())
		{
			string key = fob.GetFactionKey();
			if (key == cfg.GetFactionKeyOne())
			{
				countOne++;
				fobOne = fob;
			}
			else if (key == cfg.GetFactionKeyTwo())
			{
				countTwo++;
				fobTwo = fob;
			}
			else
			{
				Fail(string.Format("FOB at %1 has faction key '%2', which is neither config key.", fob.GetOwner().GetOrigin(), key));
			}

			if (!fob.GetOwner().FindComponent(GRAD_Endgame_GarrisonComponent))
				Fail(string.Format("FOB of %1 has no garrison on the same entity; it can never be taken.", key));
		}

		if (countOne != 1 || countTwo != 1)
		{
			Fail(string.Format("Need exactly one FOB per side; found %1 for %2 and %3 for %4.", countOne, cfg.GetFactionKeyOne(), countTwo, cfg.GetFactionKeyTwo()));
			return;
		}

		int vehiclesOne, vehiclesTwo;
		foreach (GRAD_Endgame_FobVehicleSpawnerComponent spawner : GRAD_Endgame_FobVehicleSpawnerComponent.GetAll())
		{
			if (spawner.GetFactionKey() == cfg.GetFactionKeyOne())
				vehiclesOne++;
			else if (spawner.GetFactionKey() == cfg.GetFactionKeyTwo())
				vehiclesTwo++;
		}
		if (vehiclesOne == 0 || vehiclesTwo == 0)
			Warn(string.Format("FOB vehicle spots: %1 for %2, %3 for %4. Each side should have some.", vehiclesOne, cfg.GetFactionKeyOne(), vehiclesTwo, cfg.GetFactionKeyTwo()));
		else if (vehiclesOne != vehiclesTwo)
			Warn(string.Format("Uneven FOB vehicles: %1 for %2, %3 for %4.", vehiclesOne, cfg.GetFactionKeyOne(), vehiclesTwo, cfg.GetFactionKeyTwo()));
		else
			Pass(string.Format("%1 FOB vehicle spots per side.", vehiclesOne));

		float distance = vector.DistanceXZ(fobOne.GetOwner().GetOrigin(), fobTwo.GetOwner().GetOrigin());
		if (distance < MIN_FOB_DISTANCE)
			Warn(string.Format("FOBs are only %1 m apart (want %2+).", Math.Round(distance), MIN_FOB_DISTANCE));
		else
			Pass(string.Format("Two FOBs, %1 m apart.", Math.Round(distance)));
	}

	//------------------------------------------------------------------------------------------------
	protected void CheckDocumentSites(notnull GRAD_Endgame_Config cfg, GRAD_Endgame_FobComponent fobOne, GRAD_Endgame_FobComponent fobTwo)
	{
		array<GRAD_Endgame_DocumentSiteComponent> sites = GRAD_Endgame_DocumentSiteComponent.GetAll();
		int active = cfg.GetActiveDocumentSites();
		int threshold = cfg.GetDocumentThreshold();

		if (sites.Count() < active)
			Fail(string.Format("%1 document sites placed, the config activates %2.", sites.Count(), active));
		else if (sites.Count() == active)
			Warn(string.Format("Exactly %1 document sites placed: every match uses the same ones. Place more for variety.", active));
		else
			Pass(string.Format("%1 document sites placed, %2 active per match.", sites.Count(), active));

		if (threshold > active && active > 0)
			Fail(string.Format("Threshold %1 is more than the %2 active sites at one copy per site: phase 3 can never start.", threshold, active));

		array<vector> positions = {};
		foreach (GRAD_Endgame_DocumentSiteComponent site : sites)
			positions.Insert(site.GetOwner().GetOrigin());

		for (int i = 0; i < positions.Count(); i++)
		{
			for (int j = i + 1; j < positions.Count(); j++)
			{
				float spacing = vector.DistanceXZ(positions[i], positions[j]);
				if (spacing < MIN_SITE_SPACING)
					Warn(string.Format("Document sites at %1 and %2 are only %3 m apart.", positions[i], positions[j], Math.Round(spacing)));
			}
		}

		if (!fobOne || !fobTwo || sites.Count() < active)
			return;

		CheckBalance("document", positions, fobOne, fobTwo, active, cfg.GetSiteBalanceTolerance());
	}

	//------------------------------------------------------------------------------------------------
	//! Draw many selections the way the game does and report how often they are fair.
	protected void CheckBalance(string what, notnull array<vector> positions, notnull GRAD_Endgame_FobComponent fobOne, notnull GRAD_Endgame_FobComponent fobTwo, int count, float tolerance)
	{
		vector one = fobOne.GetOwner().GetOrigin();
		vector two = fobTwo.GetOwner().GetOrigin();

		int fair;
		float best = float.MAX;
		set<string> distinct = new set<string>();
		int runs = 50;

		for (int run = 0; run < runs; run++)
		{
			array<int> chosen = {};
			float imbalance = GRAD_Endgame_SiteSelection.Choose(positions, one, two, count, tolerance, chosen);
			if (imbalance <= tolerance)
				fair++;
			if (imbalance < best)
				best = imbalance;

			chosen.Sort();
			distinct.Insert(chosen.ToString());
		}

		if (fair == 0)
			Fail(string.Format("No fair %1 site set exists (best imbalance %2 \%, allowed %3 \%). Place sites halfway between the FOBs.", what, Math.Round(best * 100), Math.Round(tolerance * 100)));
		else if (distinct.Count() < 3)
			Warn(string.Format("Only %1 different fair %2 site sets came up in %3 draws: little variety between matches.", distinct.Count(), what, runs));
		else
			Pass(string.Format("%1 sites: %2 different fair sets in %3 draws.", what, distinct.Count(), runs));
	}

	//------------------------------------------------------------------------------------------------
	protected void CheckRadioSites(notnull GRAD_Endgame_Config cfg, GRAD_Endgame_FobComponent fobOne, GRAD_Endgame_FobComponent fobTwo)
	{
		array<GRAD_Endgame_RadioSiteComponent> sites = GRAD_Endgame_RadioSiteComponent.GetAll();
		int active = cfg.GetActiveRadioSites();

		if (sites.Count() < active)
			Fail(string.Format("%1 radio sites placed, the config activates %2.", sites.Count(), active));
		else
			Pass(string.Format("%1 radio sites placed, %2 active per match.", sites.Count(), active));

		if (!fobOne || !fobTwo)
			return;

		array<vector> positions = {};
		foreach (GRAD_Endgame_RadioSiteComponent site : sites)
		{
			vector pos = site.GetOwner().GetOrigin();
			positions.Insert(pos);

			float nearest = Math.Min(vector.DistanceXZ(pos, fobOne.GetOwner().GetOrigin()), vector.DistanceXZ(pos, fobTwo.GetOwner().GetOrigin()));
			if (nearest < MIN_RADIO_TO_FOB)
				Warn(string.Format("Radio site at %1 is only %2 m from a FOB.", pos, Math.Round(nearest)));

			if (cfg.GetMilitiaAtRadioSites() && !site.GetOwner().FindComponent(GRAD_Endgame_GarrisonComponent))
				Warn(string.Format("Radio site at %1 has no garrison, but militia at radio sites is on.", pos));
		}

		if (sites.Count() >= active && active > 0)
			CheckBalance("radio", positions, fobOne, fobTwo, active, cfg.GetSiteBalanceTolerance());
	}

	//------------------------------------------------------------------------------------------------
	protected void CheckCipher(notnull GRAD_Endgame_Config cfg)
	{
		int sites = GRAD_Endgame_CipherSiteComponent.GetAll().Count();
		if (sites == 0)
			Fail("No cipher site placed: nobody can transmit.");
		else if (sites == 1)
			Warn("Only one cipher site: the cipher always lies at the same place.");
		else
			Pass(string.Format("%1 cipher sites placed.", sites));

		ResourceName prefab = cfg.GetCipherPrefab();
		if (prefab.IsEmpty())
		{
			Fail("No cipher prefab set in the config (m_sCipherPrefab).");
			return;
		}

		Resource resource = Resource.Load(prefab);
		if (!resource || !resource.IsValid())
			Fail("Cipher prefab does not load: " + prefab);
		else
			Pass("Cipher prefab loads.");
	}

	//------------------------------------------------------------------------------------------------
	protected void CheckGarrisons(notnull GRAD_Endgame_Config cfg)
	{
		array<GRAD_Endgame_DocumentSiteComponent> documentSites = GRAD_Endgame_DocumentSiteComponent.GetAll();
		int withoutGarrison;
		foreach (GRAD_Endgame_DocumentSiteComponent site : documentSites)
		{
			if (!site.GetOwner().FindComponent(GRAD_Endgame_GarrisonComponent))
				withoutGarrison++;
		}

		array<ResourceName> groups = {};
		array<string> tierNames = {"LIGHT", "MEDIUM", "HEAVY"};
		array<GRAD_Endgame_EGarrisonTier> tiers = {GRAD_Endgame_EGarrisonTier.LIGHT, GRAD_Endgame_EGarrisonTier.MEDIUM, GRAD_Endgame_EGarrisonTier.HEAVY};
		for (int t = 0; t < tiers.Count(); t++)
		{
			cfg.GetGarrisonGroups(tiers[t], groups);
			if (groups.IsEmpty())
				Fail(string.Format("Garrison tier %1 has no group prefabs in the config. Suggestion: FIA groups from Prefabs/Groups/INDFOR.", tierNames[t]));
			foreach (ResourceName group : groups)
			{
				if (group.Contains("USSR") || group.Contains("/OPFOR/") || group.Contains("/BLUFOR/") || group.Contains("US_"))
					Warn(string.Format("Garrison tier %1 uses %2, which looks like a player faction. Garrisons should be FIA.", tierNames[t], group));
			}
		}
		Warn("Check by hand: FIA must be hostile to both player factions in the faction manager.");

		if (withoutGarrison > 0)
			Warn(string.Format("%1 document site(s) have no garrison.", withoutGarrison));
		else if (!documentSites.IsEmpty())
			Pass("Every document site has a garrison.");

		// Garrisons alive at once in a match: 2 FOBs + active document sites + active radio sites.
		int perMatch = 2 + Math.Min(cfg.GetActiveDocumentSites(), documentSites.Count());
		if (cfg.GetMilitiaAtRadioSites())
			perMatch += Math.Min(cfg.GetActiveRadioSites(), GRAD_Endgame_RadioSiteComponent.GetAll().Count());
		Pass(string.Format("%1 garrison groups spawn per match. Compare with the AI budget measured in test 20.", perMatch));
	}
}
#endif
