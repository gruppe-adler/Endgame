//------------------------------------------------------------------------------------------------
enum GRAD_Endgame_ETransmissionState
{
	OFF,
	TRANSMITTING,
	INTERRUPTED,
	DONE
}

//------------------------------------------------------------------------------------------------
class GRAD_Endgame_RadioSiteComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! A radio site (M5). States as in BreakingContact's transmission component: OFF, TRANSMITTING,
//! INTERRUPTED, DONE (DISABLED is left out until destroyable sites are wanted).
//!
//! Rules (concept, "The radio phase"):
//!  - In phase RADIO, a player of either side who carries the cipher (and, with
//!    m_eTransmissionRole SIGNALLER_ONLY, has the radio operator slot) starts the transmission at
//!    an active site with the transmit action. Starting is not a long hold; he then has to stay.
//!  - The server checks once per second: operator alive, within m_fOperatorRange of the set, still
//!    carrying the cipher. If not, the transmission is interrupted; the progress pauses or resets
//!    (m_eTransmissionInterrupt).
//!  - Progress belongs to the side that made it. If the other side transmits here, it starts at 0.
//!  - At m_fTransmissionSeconds the transmitting side wins.
//!  - Direction finding: while transmitting, the other side gets a search area every
//!    m_fDfIntervalSeconds (first one at once with IMMEDIATE, after one interval with DELAYED).
//!
//! Replicated for everyone: state and progress (the site is public in phase 3 and the radio is
//! audible), and which side transmits. The operator's name is not replicated.
//!
//! Prefab: this component, an ActionsManagerComponent with GRAD_Endgame_TransmitAction and,
//! with militia on radio sites, a GRAD_Endgame_GarrisonComponent on the same entity.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-10): not compiled, not run.
class GRAD_Endgame_RadioSiteComponent : ScriptComponent
{
	[RplProp(onRplName: "OnStateReplicated")]
	protected GRAD_Endgame_ETransmissionState m_eState;

	//! Seconds transmitted so far.
	[RplProp()]
	protected float m_fProgress;

	//! Side whose progress m_fProgress is.
	[RplProp()]
	protected string m_sProgressFaction;

	//! Chosen for this match (at game start, balanced like the document sites).
	[RplProp()]
	protected bool m_bActive = true;

	protected static ref array<GRAD_Endgame_RadioSiteComponent> s_aAll = {};

	// Server only
	protected int m_iOperatorId;
	protected float m_fNextDfIn;
	protected int m_iIndex;   //!< position in s_aAll at selection time, for marker ids

	protected const int TICK_MS = 1000;

	//------------------------------------------------------------------------------------------------
	static array<GRAD_Endgame_RadioSiteComponent> GetAll()
	{
		return s_aAll;
	}

	//------------------------------------------------------------------------------------------------
	GRAD_Endgame_ETransmissionState GetState()
	{
		return m_eState;
	}

	//------------------------------------------------------------------------------------------------
	float GetProgressSeconds()
	{
		return m_fProgress;
	}

	//------------------------------------------------------------------------------------------------
	//! 0..1 of the configured transmission time.
	float GetProgressFraction()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg || cfg.GetTransmissionSeconds() <= 0)
			return 0;
		return Math.Clamp(m_fProgress / cfg.GetTransmissionSeconds(), 0, 1);
	}

	//------------------------------------------------------------------------------------------------
	string GetProgressFaction()
	{
		return m_sProgressFaction;
	}

	//------------------------------------------------------------------------------------------------
	bool IsActive()
	{
		return m_bActive;
	}

	//------------------------------------------------------------------------------------------------
	void SetActive(bool active, int index)
	{
		if (!Replication.IsServer())
			return;

		m_iIndex = index;
		if (m_bActive == active)
			return;
		m_bActive = active;
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	string GetMarkerId()
	{
		return "radio_" + m_iIndex.ToString();
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_aAll.Insert(this);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(ServerTick);
		s_aAll.RemoveItem(this);
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	//! Client: state arrived. TODO (WP4): start or stop the radio loop sound here, and once after
	//! joining, because the callback does not fire for a late joiner.
	protected void OnStateReplicated()
	{
	}

	//------------------------------------------------------------------------------------------------
	//! Why this character cannot start (or stop) a transmission here, or empty. Server and client.
	string GetRefusalReason(IEntity user)
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg)
			return "No config";
		if (!m_bActive)
			return "No signal here";

		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (gameMode && gameMode.GetPhase() != GRAD_Endgame_EPhase.RADIO)
			return "Not in this phase";

		if (m_eState == GRAD_Endgame_ETransmissionState.DONE)
			return "Transmission complete";

		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(user);
		if (!cfg.IsPlayerFaction(GRAD_Endgame_InfoService.GetFactionKey(playerId)))
			return "Not for your side";

		// The operator may always stop his own transmission.
		if (m_eState == GRAD_Endgame_ETransmissionState.TRANSMITTING)
		{
			if (Replication.IsServer() && playerId != m_iOperatorId)
				return "Already transmitting";
			return string.Empty;
		}

		GRAD_Endgame_CipherComponent cipher = GRAD_Endgame_CipherComponent.GetInstance();
		if (!cipher || !cipher.IsCarriedBy(user))
			return "You need the cipher";

		if (cfg.GetTransmissionRole() == GRAD_Endgame_ETransmissionRole.SIGNALLER_ONLY && !IsSignaller(playerId))
			return "Only the radio operator";

		return string.Empty;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsSignaller(int playerId)
	{
		COA_SlottingManager slottingManager = COA_SlottingManager.GetInstance();
		if (!slottingManager)
			return false;
		COA_SlotData slot = slottingManager.GetPlayerSlotData(playerId);
		return slot && slot.GetSlotRole() == COA_EGearRole.RADIO_TELEPHONE_OPERATOR;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: the transmit action was performed. Starts, resumes or stops.
	void Toggle(IEntity user)
	{
		if (!Replication.IsServer())
			return;

		string reason = GetRefusalReason(user);
		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(user);
		if (!reason.IsEmpty())
		{
			GRAD_Endgame_InfoService.HintPlayer(playerId, "Radio", reason);
			return;
		}

		if (m_eState == GRAD_Endgame_ETransmissionState.TRANSMITTING)
		{
			Interrupt("operator stopped");
			return;
		}

		Start(playerId);
	}

	//------------------------------------------------------------------------------------------------
	protected void Start(int playerId)
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		string factionKey = GRAD_Endgame_InfoService.GetFactionKey(playerId);

		if (m_sProgressFaction != factionKey)
		{
			m_fProgress = 0;
			m_sProgressFaction = factionKey;
		}

		m_iOperatorId = playerId;
		m_eState = GRAD_Endgame_ETransmissionState.TRANSMITTING;
		Replication.BumpMe();

		// IMMEDIATE: the first fix goes out now; DELAYED: after one interval.
		m_fNextDfIn = 0;
		if (cfg.GetDirectionFinding() == GRAD_Endgame_EDirectionFinding.DELAYED)
			m_fNextDfIn = cfg.GetDfIntervalSeconds();

		GetGame().GetCallqueue().Remove(ServerTick);
		GetGame().GetCallqueue().CallLater(ServerTick, TICK_MS, true);

		GRAD_Endgame_MatchLog.Write("transmission", factionKey, playerId, GetOwner().GetOrigin(), string.Format("started at %1 s", Math.Round(m_fProgress)));
		GRAD_Endgame_InfoService.HintFaction(factionKey, "Radio", "Transmission started. Hold the site.");
	}

	//------------------------------------------------------------------------------------------------
	protected void Interrupt(string cause)
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		GetGame().GetCallqueue().Remove(ServerTick);

		if (cfg && cfg.GetTransmissionInterrupt() == GRAD_Endgame_ETransmissionInterrupt.RESET)
			m_fProgress = 0;

		m_eState = GRAD_Endgame_ETransmissionState.INTERRUPTED;
		Replication.BumpMe();

		GRAD_Endgame_MatchLog.Write("transmission", m_sProgressFaction, m_iOperatorId, GetOwner().GetOrigin(), string.Format("interrupted (%1) at %2 s", cause, Math.Round(m_fProgress)));
		GRAD_Endgame_InfoService.HintFaction(m_sProgressFaction, "Radio", "Transmission interrupted: " + cause);
		m_iOperatorId = 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Server, once per second while transmitting.
	protected void ServerTick()
	{
		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (!cfg || m_eState != GRAD_Endgame_ETransmissionState.TRANSMITTING)
		{
			GetGame().GetCallqueue().Remove(ServerTick);
			return;
		}

		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (gameMode && gameMode.GetPhase() != GRAD_Endgame_EPhase.RADIO)
		{
			Interrupt("phase over");
			return;
		}

		string cause = CheckOperator(cfg);
		if (!cause.IsEmpty())
		{
			Interrupt(cause);
			return;
		}

		float step = TICK_MS * 0.001;
		m_fProgress += step;
		Replication.BumpMe();

		if (cfg.GetDirectionFinding() != GRAD_Endgame_EDirectionFinding.OFF)
		{
			m_fNextDfIn -= step;
			if (m_fNextDfIn <= 0)
			{
				SendDirectionFinding(cfg);
				m_fNextDfIn = cfg.GetDfIntervalSeconds();
			}
		}

		if (m_fProgress >= cfg.GetTransmissionSeconds())
			Complete();
	}

	//------------------------------------------------------------------------------------------------
	//! Why the operator can no longer transmit, or empty.
	protected string CheckOperator(notnull GRAD_Endgame_Config cfg)
	{
		IEntity operatorEntity = GetGame().GetPlayerManager().GetPlayerControlledEntity(m_iOperatorId);
		if (!operatorEntity)
			return "operator gone";

		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(operatorEntity);
		if (!character || character.GetDamageManager().GetState() == EDamageState.DESTROYED)
			return "operator killed";

		if (vector.Distance(operatorEntity.GetOrigin(), GetOwner().GetOrigin()) > cfg.GetOperatorRange())
			return "operator left the set";

		GRAD_Endgame_CipherComponent cipher = GRAD_Endgame_CipherComponent.GetInstance();
		if (!cipher || !cipher.IsCarriedBy(operatorEntity))
			return "cipher lost";

		return string.Empty;
	}

	//------------------------------------------------------------------------------------------------
	protected void SendDirectionFinding(notnull GRAD_Endgame_Config cfg)
	{
		string enemyKey = cfg.GetFactionKeyOne();
		if (m_sProgressFaction == enemyKey)
			enemyKey = cfg.GetFactionKeyTwo();

		// TODO: BEARING accuracy (a line from a listening post) is not built yet; both settings
		// send a search area for now.
		float radius = cfg.GetDfAreaRadius();
		vector fix = GRAD_Endgame_InfoService.Blur(GetOwner().GetOrigin(), radius);
		GRAD_Endgame_InfoService.SetMarker(enemyKey, "df_" + GetMarkerId(), fix, radius, "Enemy transmission");
		GRAD_Endgame_MatchLog.Write("df_sent", enemyKey, 0, GetOwner().GetOrigin(), string.Format("radius %1", Math.Round(radius)));
	}

	//------------------------------------------------------------------------------------------------
	protected void Complete()
	{
		GetGame().GetCallqueue().Remove(ServerTick);
		m_eState = GRAD_Endgame_ETransmissionState.DONE;
		Replication.BumpMe();

		GRAD_Endgame_MatchLog.Write("transmission", m_sProgressFaction, m_iOperatorId, GetOwner().GetOrigin(), "done");

		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (gameMode)
			gameMode.EndMatch(m_sProgressFaction, "transmission");
	}
}

//------------------------------------------------------------------------------------------------
//! "Start / stop transmission" at a radio site. A short hold (the prefab's Duration, e.g. 3 s),
//! then the server toggles. The long wait is the transmission itself, during which the operator
//! has to stay at the set. Same replication pattern as the copy action.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-10): not compiled, not run.
class GRAD_Endgame_TransmitAction : SCR_ScriptedUserAction
{
	protected GRAD_Endgame_RadioSiteComponent m_Site;
	protected bool m_bActionCompleted;

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		super.Init(pOwnerEntity, pManagerComponent);
		m_Site = GRAD_Endgame_RadioSiteComponent.Cast(pOwnerEntity.FindComponent(GRAD_Endgame_RadioSiteComponent));
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
		return m_Site && m_Site.IsActive();
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		if (!m_Site)
			return false;

		string reason = m_Site.GetRefusalReason(user);
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
		if (m_Site)
			m_Site.Toggle(pUserEntity);
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		if (!m_Site)
			return false;

		int percent = Math.Round(m_Site.GetProgressFraction() * 100);
		if (m_Site.GetState() == GRAD_Endgame_ETransmissionState.TRANSMITTING)
			outName = string.Format("Stop transmission (%1 \%)", percent);
		else
			outName = string.Format("Transmit (%1 \%)", percent);
		return true;
	}
}
