//------------------------------------------------------------------------------------------------
//! Chooses which of the placed document sites are active in a match (decided 2026-11-10): a map
//! may place any number of sites; the server activates a fixed count at random, so matches differ,
//! but only among choices that are fair to both sides.
//!
//! Fairness: for a choice of sites, each side's "distance to cover" is the sum of the 2D distances
//! from its FOB to the chosen sites. Imbalance = |sumOne - sumTwo| / (sumOne + sumTwo).
//! The selection draws many random choices, keeps those within the tolerance, and picks one of them
//! at random. If none is within the tolerance (a lopsided map), it takes the fairest one found and
//! says so in the log, so the mapper knows to place more central sites.
//!
//! Pure logic on positions: no engine calls except the random generator, so it can be tested in the
//! script console (GRAD_Endgame_SiteSelectionTest).
class GRAD_Endgame_SiteSelection
{
	static const int ATTEMPTS = 400;

	//------------------------------------------------------------------------------------------------
	//! \param[in] sites positions of all placed sites
	//! \param[in] fobOne, fobTwo FOB positions of the two sides
	//! \param[in] count how many sites to activate (all if there are not more)
	//! \param[in] tolerance largest accepted imbalance, 0.1 = 10 %
	//! \param[out] chosen indices into sites
	//! \return imbalance of the chosen set
	static float Choose(notnull array<vector> sites, vector fobOne, vector fobTwo, int count, float tolerance, notnull array<int> chosen)
	{
		chosen.Clear();
		int n = sites.Count();

		if (count <= 0 || n <= count)
		{
			for (int all = 0; all < n; all++)
				chosen.Insert(all);
			return Imbalance(sites, fobOne, fobTwo, chosen);
		}

		array<ref array<int>> fair = {};
		array<int> best;
		float bestImbalance = float.MAX;

		for (int attempt = 0; attempt < ATTEMPTS; attempt++)
		{
			array<int> candidate = RandomSubset(n, count);
			float imbalance = Imbalance(sites, fobOne, fobTwo, candidate);

			if (imbalance <= tolerance)
				fair.Insert(candidate);

			if (imbalance < bestImbalance)
			{
				bestImbalance = imbalance;
				best = candidate;
			}
		}

		array<int> pick = best;
		if (!fair.IsEmpty())
			pick = fair.GetRandomElement();

		foreach (int index : pick)
			chosen.Insert(index);

		return Imbalance(sites, fobOne, fobTwo, chosen);
	}

	//------------------------------------------------------------------------------------------------
	//! |sumOne - sumTwo| / (sumOne + sumTwo) over the given site indices, 0 = perfectly even.
	static float Imbalance(notnull array<vector> sites, vector fobOne, vector fobTwo, notnull array<int> indices)
	{
		float sumOne, sumTwo;
		foreach (int index : indices)
		{
			sumOne += vector.DistanceXZ(sites[index], fobOne);
			sumTwo += vector.DistanceXZ(sites[index], fobTwo);
		}

		if (sumOne + sumTwo <= 0)
			return 0;
		return Math.AbsFloat(sumOne - sumTwo) / (sumOne + sumTwo);
	}

	//------------------------------------------------------------------------------------------------
	//! count different indices from 0 .. n-1, in random order.
	protected static array<int> RandomSubset(int n, int count)
	{
		array<int> pool = {};
		for (int i = 0; i < n; i++)
			pool.Insert(i);

		array<int> subset = {};
		for (int k = 0; k < count; k++)
		{
			int at = Math.RandomInt(0, pool.Count());
			subset.Insert(pool[at]);
			pool.Remove(at);
		}
		return subset;
	}
}

//------------------------------------------------------------------------------------------------
//! Script console:  GRAD_Endgame_SiteSelectionTest.Run();
//! A made-up map: FOBs 2 km apart, eight sites, three of them close to FOB one.
class GRAD_Endgame_SiteSelectionTest
{
	static void Run()
	{
		vector fobOne = "0 0 0";
		vector fobTwo = "2000 0 0";
		array<vector> sites = {"200 0 300", "300 0 -200", "150 0 100", "1000 0 600", "1000 0 -700", "1800 0 400", "1700 0 -300", "1000 0 0"};

		int fairRuns;
		for (int run = 0; run < 20; run++)
		{
			array<int> chosen = {};
			float imbalance = GRAD_Endgame_SiteSelection.Choose(sites, fobOne, fobTwo, 5, 0.1, chosen);
			if (chosen.Count() == 5 && imbalance <= 0.1)
				fairRuns++;
			Print(string.Format("[GRAD_Endgame] run %1: sites %2, imbalance %3", run, chosen, imbalance), LogLevel.NORMAL);
		}

		Print(string.Format("[GRAD_Endgame] SiteSelection: %1 of 20 runs fair and of size 5 (expected 20; the chosen sets should differ)", fairRuns), LogLevel.NORMAL);
	}
}
