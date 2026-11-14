//------------------------------------------------------------------------------------------------
// PORTED from BreakingContact (GRAD_BC_ExplosiveCatalog), renamed GRAD_Endgame_*. Not compiled, not run here.
//------------------------------------------------------------------------------------------------
// GRAD_Endgame_ExplosiveCatalog
//
// Single place that maps an explosive prefab to its replay category and colour.
//
// Smoke colour cannot be read at runtime: the grenade prefabs express colour as a material
// assignment (e.g. m18_green.emat), not as a colour value, so there is nothing to query. The
// mapping below is therefore hardcoded, keyed on a substring of the prefab path.
//
// To support a new grenade, add a branch here - nothing else needs to change.
//------------------------------------------------------------------------------------------------
class GRAD_Endgame_ExplosiveCatalog
{
	// Smoke cloud colours, ARGB. Alpha is set at draw time from the fade curve, so the value
	// here is fully opaque and only the RGB matters.
	static const int COLOR_SMOKE_WHITE  = 0xFFE8E8E8;
	static const int COLOR_SMOKE_GREEN  = 0xFF3FBF5F;
	static const int COLOR_SMOKE_VIOLET = 0xFFA050C8;
	static const int COLOR_SMOKE_ORANGE = 0xFFE08030;
	static const int COLOR_SMOKE_RED    = 0xFFD03030;

	// Fallback lifetime for a smoke cloud, in seconds, used when the tracker cannot observe the
	// entity's real removal. Measured smoke lifetimes should replace this if they differ.
	static const float SMOKE_FALLBACK_DURATION = 60.0;

	// How long an HE flash ring is drawn for, in seconds of replay time.
	static const float HE_FLASH_DURATION = 2.0;

	//------------------------------------------------------------------------------------------------
	//! Classify a prefab into a replay category.
	//! \param prefabPath full or partial prefab resource path
	//! \param outKind receives the category
	//! \return true if the prefab is an explosive we record, false to ignore it
	static bool Classify(string prefabPath, out EGradEndgameExplosiveKind outKind)
	{
		string p = prefabPath;
		p.ToLower();

		if (p.Contains("smoke"))
		{
			outKind = EGradEndgameExplosiveKind.SMOKE;
			return true;
		}

		// Rockets and other launcher ammo. Checked before the generic grenade test because some
		// launcher rounds also carry "grenade" in their name.
		if (p.Contains("rocket") || p.Contains("_pg") || p.Contains("rpg"))
		{
			outKind = EGradEndgameExplosiveKind.AT;
			return true;
		}

		if (p.Contains("grenade") || p.Contains("rgd") || p.Contains("m67"))
		{
			outKind = EGradEndgameExplosiveKind.HE;
			return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Smoke cloud colour for a prefab. Returns white for unrecognised smoke so a new variant
	//! still renders rather than disappearing.
	static int GetSmokeColor(string prefabPath)
	{
		string p = prefabPath;
		p.ToLower();

		if (p.Contains("green"))
			return COLOR_SMOKE_GREEN;

		if (p.Contains("violet") || p.Contains("purple"))
			return COLOR_SMOKE_VIOLET;

		if (p.Contains("red"))
			return COLOR_SMOKE_RED;

		// RDG2 is the Soviet orange-ish screening smoke; ANM8HC is white US screening smoke.
		if (p.Contains("rdg2"))
			return COLOR_SMOKE_ORANGE;

		return COLOR_SMOKE_WHITE;
	}
}
