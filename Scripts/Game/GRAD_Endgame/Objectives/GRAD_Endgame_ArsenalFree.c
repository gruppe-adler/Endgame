//------------------------------------------------------------------------------------------------
//! Endgame's ammo arsenals cost no supplies: the boxes at the FOBs are not fed by any supply
//! system, so with supply costs on every item would be greyed out (empty resource container).
//! While the Endgame game mode runs, no arsenal asks for supplies.
//!
//! TEST (2026-11-14): written without Workbench. If it does not compile (IsArsenalUsingSupplies
//! not overridable) or the items stay greyed, set the supply cost type on the arsenal component
//! of the ammo box prefabs instead.
modded class SCR_ArsenalComponent
{
	//------------------------------------------------------------------------------------------------
	override bool IsArsenalUsingSupplies()
	{
		if (GRAD_Endgame_GameModeComponent.GetInstance())
			return false;

		return super.IsArsenalUsingSupplies();
	}
}
