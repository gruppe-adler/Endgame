//------------------------------------------------------------------------------------------------
//! The document site prefab is built on a vanilla inventory item (it needs the model and the
//! collision), and that item brings its inventory actions: pick up, loot, equip, inspect...
//! Nobody may carry the documents away, or the copy point would move with them. So every
//! inventory action refuses an entity that is a document site.
//!
//! NOT done: locking the item with InventoryItemComponent.RequestUserLock in OnPostInit. It logged
//! "Attempting to send an RPC through unregistred item" once per site on the server, and a physics
//! crash followed (2026-11-14). If dragging the folder out of the ground list in the inventory
//! screen turns out to be possible, lock it later (after replication starts), not in OnPostInit.
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
