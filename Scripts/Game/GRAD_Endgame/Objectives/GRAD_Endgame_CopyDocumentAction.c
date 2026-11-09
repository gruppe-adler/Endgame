//------------------------------------------------------------------------------------------------
//! Hold action "copy the documents" (T5). Pattern from BreakingContact's
//! GRAD_BC_ToggleRadioTransmission:
//!  - HasLocalEffectOnlyScript false: CanBeShown / CanBePerformed run on the client,
//!    PerformAction runs on the server.
//!  - The hold itself is PerformContinuousAction + LoopActionUpdate; a flag makes sure the copy is
//!    counted once per hold.
//! The progress bar is local to the performer. Everyone else sees the replicated copy count in the
//! action label.
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
		return m_Site != null;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		return m_Site != null;
	}

	//------------------------------------------------------------------------------------------------
	override void OnActionStart(IEntity pUserEntity)
	{
		m_bActionCompleted = false;
		super.OnActionStart(pUserEntity);
	}

	//------------------------------------------------------------------------------------------------
	override void OnActionCanceled(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		m_bActionCompleted = false;
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

		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(pUserEntity);
		m_Site.AddCopy(playerId);
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		if (!m_Site)
			return false;

		outName = string.Format("Copy documents (%1 made)", m_Site.GetCopyCount());
		return true;
	}
}
