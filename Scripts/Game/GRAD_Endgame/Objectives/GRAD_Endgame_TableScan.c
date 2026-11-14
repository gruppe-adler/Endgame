//------------------------------------------------------------------------------------------------
//! Finds tables in the world around a point, so a document site can put its documents on a table
//! that is already there instead of bringing its own.
//!
//! A "table" is any entity whose prefab name or model name contains "table", whose top is
//! TOP_MIN..TOP_MAX metres above its base and whose footprint is at least FOOTPRINT_MIN metres.
//! Entities of this mod (the old table-based sites) are ignored.
//!
//! TEST (2026-11-14): the scan is new. Run it first (m_bRunSelfTests) and read the "table_scan"
//! lines of the match log before building on it.
class GRAD_Endgame_TableHit
{
	IEntity m_Entity;
	vector m_vTop;        //!< middle of the table top, world position
	float m_fDistance;    //!< from the scan centre
	string m_sName;       //!< prefab or model name, for the log
}

//------------------------------------------------------------------------------------------------
class GRAD_Endgame_TableScan
{
	protected static const float TOP_MIN = 0.55;         //!< lowest table top above its base
	protected static const float TOP_MAX = 1.25;         //!< highest
	protected static const float FOOTPRINT_MIN = 0.5;    //!< smallest side

	protected static ref array<ref GRAD_Endgame_TableHit> s_aHits;
	protected static vector s_vCentre;
	protected static ref set<IEntity> s_Seen;

	//------------------------------------------------------------------------------------------------
	//! Tables within radius of centre, nearest first.
	static void Find(vector centre, float radius, notnull array<ref GRAD_Endgame_TableHit> result)
	{
		result.Clear();
		s_aHits = new array<ref GRAD_Endgame_TableHit>();
		s_Seen = new set<IEntity>();
		s_vCentre = centre;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		world.QueryEntitiesBySphere(centre, radius, OnEntity, null, EQueryEntitiesFlags.ALL);

		// nearest first (insertion sort, the lists are short)
		foreach (GRAD_Endgame_TableHit hit : s_aHits)
		{
			int at = result.Count();
			while (at > 0 && result[at - 1].m_fDistance > hit.m_fDistance)
				at--;
			result.InsertAt(hit, at);
		}

		s_aHits = null;
		s_Seen = null;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool OnEntity(IEntity entity)
	{
		if (!entity || s_Seen.Contains(entity))
			return true;
		s_Seen.Insert(entity);

		string name = GetName(entity);
		if (name.IsEmpty())
			return true;

		string lower = name;
		lower.ToLower();
		if (!lower.Contains("table") || lower.Contains("grad_endgame"))
			return true;

		vector mins, maxs;
		entity.GetWorldBounds(mins, maxs);
		float height = maxs[1] - mins[1];
		float sizeX = maxs[0] - mins[0];
		float sizeZ = maxs[2] - mins[2];
		if (height < TOP_MIN || height > TOP_MAX || sizeX < FOOTPRINT_MIN || sizeZ < FOOTPRINT_MIN)
			return true;

		GRAD_Endgame_TableHit hit = new GRAD_Endgame_TableHit();
		hit.m_Entity = entity;
		hit.m_vTop = Vector((mins[0] + maxs[0]) * 0.5, maxs[1], (mins[2] + maxs[2]) * 0.5);
		hit.m_fDistance = vector.Distance(hit.m_vTop, s_vCentre);
		hit.m_sName = name;
		s_aHits.Insert(hit);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Tables that suit a document: not a coffee table, a sink or a kitchen table (a drill bench is fine).
	static bool IsSuitable(notnull GRAD_Endgame_TableHit hit)
	{
		string lower = hit.m_sName;
		lower.ToLower();
		array<string> unwanted = {"coffee", "sink", "kitchen"};
		foreach (string word : unwanted)
		{
			if (lower.Contains(word))
				return false;
		}
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Prefab name, else model name, else empty.
	protected static string GetName(IEntity entity)
	{
		EntityPrefabData prefabData = entity.GetPrefabData();
		if (prefabData)
		{
			string prefab = prefabData.GetPrefabName();
			if (!prefab.IsEmpty())
				return prefab;
		}

		VObject mesh = entity.GetVObject();
		if (mesh)
			return mesh.GetResourceName();

		return string.Empty;
	}

	//------------------------------------------------------------------------------------------------
	//! Log how many tables lie within each radius of every document site, and the nearest ones.
	//! This is the test: do we find enough tables to place the documents on?
	static void LogSiteScan()
	{
		array<float> radii = {30, 60, 100};
		array<ref GRAD_Endgame_TableHit> hits = {};
		int sitesWithTable;

		foreach (GRAD_Endgame_DocumentSiteComponent site : GRAD_Endgame_DocumentSiteComponent.GetAll())
		{
			vector origin = site.GetOwner().GetOrigin();
			string counts;
			foreach (float radius : radii)
			{
				Find(origin, radius, hits);
				counts += string.Format("%1 m: %2  ", radius, hits.Count());
			}

			// the nearest few, from the widest scan
			string nearest;
			for (int i = 0; i < hits.Count() && i < 3; i++)
				nearest += string.Format("[%1 m %2] ", Math.Round(hits[i].m_fDistance), FilePath.StripPath(hits[i].m_sName));

			if (!hits.IsEmpty())
				sitesWithTable++;

			GRAD_Endgame_MatchLog.Write("table_scan", "", 0, origin, counts + nearest);
		}

		GRAD_Endgame_MatchLog.Write("table_scan_summary", "", 0, vector.Zero, string.Format("%1 of %2 document sites have a table within 100 m", sitesWithTable, GRAD_Endgame_DocumentSiteComponent.GetAll().Count()));
	}
}
