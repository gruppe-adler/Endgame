//------------------------------------------------------------------------------------------------
//! The document site prefab is built on a vanilla inventory item (it needs the model and the
//! collision), and that item brings its inventory actions: pick up, loot, equip, inspect...
//! Nobody may carry the documents away, or the copy point would move with them. So every
//! inventory action refuses an entity that is a document site. The server also locks the item
//! (GRAD_Endgame_DocumentSiteComponent), which covers the inventory screen.
//!
//! Only document sites are affected; every other inventory action is untouched.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-14): not compiled, not run.
modded class SCR_InventoryAction
{
	//------------------------------------------------------------------------------------------------
	protected bool GRAD_Endgame_IsDocumentSite()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return false;

		return owner.FindComponent(GRAD_Endgame_DocumentSiteComponent) != null;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		if (GRAD_Endgame_IsDocumentSite())
			return false;

		return super.CanBeShownScript(user);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		if (GRAD_Endgame_IsDocumentSite())
			return false;

		return super.CanBePerformedScript(user);
	}
}
