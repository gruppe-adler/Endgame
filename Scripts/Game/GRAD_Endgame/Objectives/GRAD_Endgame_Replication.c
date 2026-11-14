//------------------------------------------------------------------------------------------------
//! Replication checks. A component's [RplProp] values and a broadcast user action only work when the
//! entity has an RplComponent. A prop without one still runs on the server, so a listen-server test
//! looks fine, but on a dedicated server clients never see the state (found in the 2026-11-14 MP
//! review: the document and radio site tables had none).
class GRAD_Endgame_Rpl
{
	//------------------------------------------------------------------------------------------------
	//! Log an error if the entity has no RplComponent. Call it from OnPostInit of every component
	//! that has replicated properties or a broadcast action.
	static void Require(notnull IEntity entity, string what)
	{
		if (entity.FindComponent(RplComponent))
			return;

		Print(string.Format("[GRAD_Endgame] %1 at %2 has no RplComponent: its replicated state and its actions will not reach clients on a dedicated server. Add an RplComponent to its prefab.", what, entity.GetOrigin()), LogLevel.ERROR);
	}
}
