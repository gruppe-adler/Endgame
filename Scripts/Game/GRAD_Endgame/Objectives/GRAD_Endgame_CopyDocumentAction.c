//------------------------------------------------------------------------------------------------
//! Hold action "copy the documents". Pattern from BreakingContact's
//! GRAD_BC_ToggleRadioTransmission:
//!  - HasLocalEffectOnlyScript false: CanBeShown / CanBePerformed run on the client,
//!    PerformAction runs on the server.
//!  - The hold is the engine's own: the prefab's Duration. PerformAction runs when it is up.
//!    (An earlier version used PerformContinuousAction and LoopActionUpdate; that made the hold
//!    time unclear and let the widget bar run on a different clock.)
//!
//! One player at a time (finding D1): the site carries a replicated "in use by" id, set by
//! OnActionStart together with the action's real duration; everyone else gets "In use". The
//! widget bar of the whole team is (server time now - start) / that duration, so it fills in the
//! time the hold takes.
//!
//! The action label shows the copies the acting player's own side has made here.
//!
//! TODO: confirm that OnActionStart and OnActionCanceled run on the server for a broadcast
//! action. If they do not, the lock is never set, and no widget bar is shown.
//!
//! CHANGED WITHOUT WORKBENCH (2026-11-10): not compiled, not run.
class GRAD_Endgame_CopyDocumentAction : SCR_ScriptedUserAction
{
	protected GRAD_Endgame_DocumentSiteComponent m_Site;

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		super.Init(pOwnerEntity, pManagerComponent);
		m_Site = GRAD_Endgame_DocumentSiteComponent.Cast(pOwnerEntity.FindComponent(GRAD_Endgame_DocumentSiteComponent));
	}

	//------------------------------------------------------------------------------------------------
	protected int GetPlayerId(IEntity user)
	{
		return GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(user);
	}

	//------------------------------------------------------------------------------------------------
	override bool HasLocalEffectOnlyScript()
	{
		return false;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBroadcastScript()
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		return m_Site != null && m_Site.IsSiteActive();
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		if (!m_Site)
			return false;

		string reason = m_Site.GetRefusalReason(GetPlayerId(user));
		if (reason.IsEmpty())
			return true;

		SetCannotPerformReason(reason);
		return false;
	}

	//------------------------------------------------------------------------------------------------
	override void OnActionStart(IEntity pUserEntity)
	{
		super.OnActionStart(pUserEntity);

		if (m_Site && Replication.IsServer())
			m_Site.Lock(GetPlayerId(pUserEntity), GetActionDuration());
	}

	//------------------------------------------------------------------------------------------------
	override void OnActionCanceled(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (m_Site && Replication.IsServer())
			m_Site.Unlock(GetPlayerId(pUserEntity));
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!m_Site)
			return;

		// AddCopy does nothing on clients, checks the rules again on the server and frees the site.
		m_Site.AddCopy(GetPlayerId(pUserEntity));
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		if (!m_Site)
			return false;

		string factionKey = GRAD_Endgame_DocumentSiteComponent.GetFactionKeyOfPlayer(SCR_PlayerController.GetLocalPlayerId());
		outName = string.Format("Copy documents (%1 made)", m_Site.GetCopyCount(factionKey));
		return true;
	}
}
