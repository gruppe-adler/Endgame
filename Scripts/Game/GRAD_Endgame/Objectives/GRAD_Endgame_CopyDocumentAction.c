//------------------------------------------------------------------------------------------------
//! Hold action "copy the documents". Pattern from BreakingContact's
//! GRAD_BC_ToggleRadioTransmission:
//!  - HasLocalEffectOnlyScript false: CanBeShown / CanBePerformed run on the client,
//!    PerformAction runs on the server.
//!  - The hold itself is PerformContinuousAction + LoopActionUpdate; a flag makes sure the copy is
//!    counted once per hold.
//!
//! One player at a time (finding D1): the hold timer and the completed flag belong to this action
//! object, not to the player, so two players holding at once would disturb each other. The site
//! therefore carries a replicated "in use by" id; everyone else gets "In use".
//!
//! The progress bar is local to the performer. The action label shows the copies the acting
//! player's own side has made here.
//!
//! TODO (Workbench): read the hold duration from the config (m_fCopySeconds); for now the
//! prefab's Duration decides. TODO: confirm that OnActionStart and OnActionCanceled run on the
//! server for a broadcast action. If they do not, the lock is never set (harmless) and D1 stays.
//!
//! CHANGED WITHOUT WORKBENCH (2026-11-10): not compiled, not run.
class GRAD_Endgame_CopyDocumentAction : SCR_ScriptedUserAction
{
	protected GRAD_Endgame_DocumentSiteComponent m_Site;
	protected bool m_bActionCompleted;

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
		return m_Site != null && m_Site.IsActive();
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
		m_bActionCompleted = false;
		super.OnActionStart(pUserEntity);

		if (m_Site && Replication.IsServer())
			m_Site.Lock(GetPlayerId(pUserEntity));
	}

	//------------------------------------------------------------------------------------------------
	override void OnActionCanceled(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		m_bActionCompleted = false;

		if (m_Site && Replication.IsServer())
			m_Site.Unlock(GetPlayerId(pUserEntity));
	}

	//------------------------------------------------------------------------------------------------
	override void PerformContinuousAction(IEntity pOwnerEntity, IEntity pUserEntity, float timeSlice)
	{
		if (!LoopActionUpdate(timeSlice))
			return;

		if (m_bActionCompleted)
			return;

		m_bActionCompleted = true;
		PerformAction(pOwnerEntity, pUserEntity);
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
