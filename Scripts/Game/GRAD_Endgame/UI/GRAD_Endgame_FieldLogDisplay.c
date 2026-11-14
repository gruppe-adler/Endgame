//------------------------------------------------------------------------------------------------
//! The field-log HUD (see the HUD reference page "Endgame 1989: HUD states"). One info display that
//! owns every on-screen widget; the map status line is separate (GRAD_Endgame_MapStatusLine).
//!
//! Widgets, as in the reference (layout: UI/layouts/GRAD_Endgame_FieldLog.layout):
//!   B OrdersCard       top-left, 8 s at each phase start, then only in the field log
//!   C Notice           top-centre, 8 s, no key (server hints arrive here, see ClientInfo)
//!   D ActionSlip       centre-below, while a copy or transmission runs at a site you stand at
//!   E WavePanel        centre-right, while dead and waiting for a wave
//!   F DecodedOrder     left-middle, leaders of the informed side, until dismissed [K]
//!   H TransmissionLog  bottom-right, while any radio site transmits or is interrupted [T]
//!   I SpectatorStrip   bottom-centre, dead in phase 3 [U]
//!   J ResultSheet      centred, 15 s at match end, no key
//!   FieldLog           the full log on [J]: phase, time left, own count, order, last notices
//!
//! Everything here only reads state that already reaches the client: the phase and permadeath
//! (game-mode component), site states and progress (replicated on the sites), wave countdown
//! (COALITION's respawn manager), notices and the order (owner RPCs into GRAD_Endgame_ClientInfo).
//! No widget keeps game state of its own.
//!
//! How player actions stay in step with the display ("uploading"):
//!  - Copying: the site replicates who copies and the start time on the server clock; every client
//!    draws (now - start) / copy time. The copying player's own engine progress and the team's bar
//!    therefore agree, and a cancel clears both through the same replicated "in use" id.
//!  - Transmitting: the site replicates state and seconds transmitted once per second; the bar
//!    reads those, the native task gets whole percents. Interruptions arrive as a state change, so
//!    every client sees the bar stop at the same value.
//!
//! Registration (Workbench): add this display to the HUD manager of the player controller. Our
//! controller is COALITION's COA_PlayerController.et: inherit it in this addon (or override it) and
//! add GRAD_Endgame_FieldLogDisplay to its SCR_HUDManagerComponent info displays with
//! m_LayoutPath = UI/layouts/GRAD_Endgame_FieldLog.layout, as COALITION does for its safe-start card.
//!
//! Keys (proposals from the HUD page; add the actions to the input config in Workbench):
//!   GRAD_Endgame_FieldLog [J], GRAD_Endgame_DismissOrder [K],
//!   GRAD_Endgame_ToggleTransmission [T], GRAD_Endgame_ToggleSpectator [U]
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-11): not compiled, not run. Pattern from COALITION's
//! COA_SafeStartInfoDisplay (SCR_InfoDisplayExtended, DisplayUpdate, m_wRoot.FindAnyWidget).
class GRAD_Endgame_FieldLogDisplay : SCR_InfoDisplayExtended
{
	protected const float ORDERS_SECONDS = 8;
	protected const float NOTICE_SECONDS = 8;
	protected const float RESULT_SECONDS = 15;
	protected const float SITE_RANGE = 15;   //!< metres: you see a site's action slip within this

	protected bool m_bInitialized;
	protected static GRAD_Endgame_FieldLogDisplay s_Instance;

	// Widgets
	protected Widget m_wOrdersCard, m_wNotice, m_wActionSlip, m_wWavePanel, m_wDecodedOrder;
	protected Widget m_wTransmissionLog, m_wSpectatorStrip, m_wResultSheet, m_wFieldLog;
	protected TextWidget m_wOrdersTitle, m_wOrdersBody, m_wNoticeTitle, m_wNoticeBody;
	protected TextWidget m_wActionLabel, m_wActionBar, m_wWaveCountdown, m_wWaveDetail;
	protected TextWidget m_wOrderBody, m_wTxTitle, m_wTxBar, m_wTxDetail, m_wSpectatorText;
	protected TextWidget m_wResultTitle, m_wResultBody, m_wFieldLogBody;

	// Typewriter state, one per text that types itself
	protected ref GRAD_Endgame_TypedText m_TypedOrders = new GRAD_Endgame_TypedText();
	protected ref GRAD_Endgame_TypedText m_TypedNotice = new GRAD_Endgame_TypedText();
	protected ref GRAD_Endgame_TypedText m_TypedOrder = new GRAD_Endgame_TypedText(20);
	protected ref GRAD_Endgame_TypedText m_TypedResult = new GRAD_Endgame_TypedText(12);
	protected ref GRAD_Endgame_TypedBar m_ActionBar = new GRAD_Endgame_TypedBar(20);
	protected ref GRAD_Endgame_TypedBar m_TxBar = new GRAD_Endgame_TypedBar(20);

	// Timers and toggles (all local)
	protected float m_fOrdersLeft;
	protected float m_fNoticeLeft;
	protected float m_fResultLeft;
	protected bool m_bFieldLogOpen;
	protected bool m_bOrderDismissed;
	protected bool m_bTxHidden;
	protected bool m_bSpectatorHidden;
	protected GRAD_Endgame_EPhase m_eShownPhase = GRAD_Endgame_EPhase.WAITING;
	protected string m_sShownOrder;
	protected int m_iSeenNotice;

	//------------------------------------------------------------------------------------------------
	//! The display that is on screen, or null. ClientInfo uses it to decide whether a notice can go
	//! to our widget or must fall back to a vanilla hint.
	static GRAD_Endgame_FieldLogDisplay GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	override protected void DisplayUpdate(IEntity owner, float timeSlice)
	{
		super.DisplayUpdate(owner, timeSlice);

		if (!m_bInitialized)
		{
			if (!InitWidgets())
				return;
			m_bInitialized = true;
			s_Instance = this;
			AddKeyListeners();
		}

		GRAD_Endgame_GameModeComponent gameMode = GRAD_Endgame_GameModeComponent.GetInstance();
		if (!gameMode)
		{
			HideAllCards();
			return;
		}

		UpdateOrders(gameMode, timeSlice);
		UpdateNotice(timeSlice);
		UpdateActionSlip(timeSlice);
		UpdateWavePanel(gameMode);
		UpdateDecodedOrder(timeSlice);
		UpdateTransmissionLog(timeSlice);
		UpdateSpectator(gameMode);
		UpdateResult(gameMode, timeSlice);
		UpdateFieldLog(gameMode);
	}

	//------------------------------------------------------------------------------------------------
	override void DisplayStopDraw(IEntity owner)
	{
		RemoveKeyListeners();
		if (s_Instance == this)
			s_Instance = null;
		super.DisplayStopDraw(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected bool InitWidgets()
	{
		if (!m_wRoot)
			return false;

		m_wOrdersCard = m_wRoot.FindAnyWidget("OrdersCard");
		m_wNotice = m_wRoot.FindAnyWidget("Notice");
		m_wActionSlip = m_wRoot.FindAnyWidget("ActionSlip");
		m_wWavePanel = m_wRoot.FindAnyWidget("WavePanel");
		m_wDecodedOrder = m_wRoot.FindAnyWidget("DecodedOrder");
		m_wTransmissionLog = m_wRoot.FindAnyWidget("TransmissionLog");
		m_wSpectatorStrip = m_wRoot.FindAnyWidget("SpectatorStrip");
		m_wResultSheet = m_wRoot.FindAnyWidget("ResultSheet");
		m_wFieldLog = m_wRoot.FindAnyWidget("FieldLog");

		m_wOrdersTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("OrdersTitle"));
		m_wOrdersBody = TextWidget.Cast(m_wRoot.FindAnyWidget("OrdersBody"));
		m_wNoticeTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("NoticeTitle"));
		m_wNoticeBody = TextWidget.Cast(m_wRoot.FindAnyWidget("NoticeBody"));
		m_wActionLabel = TextWidget.Cast(m_wRoot.FindAnyWidget("ActionLabel"));
		m_wActionBar = TextWidget.Cast(m_wRoot.FindAnyWidget("ActionBar"));
		m_wWaveCountdown = TextWidget.Cast(m_wRoot.FindAnyWidget("WaveCountdown"));
		m_wWaveDetail = TextWidget.Cast(m_wRoot.FindAnyWidget("WaveDetail"));
		m_wOrderBody = TextWidget.Cast(m_wRoot.FindAnyWidget("DecodedOrderBody"));
		m_wTxTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("TransmissionTitle"));
		m_wTxBar = TextWidget.Cast(m_wRoot.FindAnyWidget("TransmissionBar"));
		m_wTxDetail = TextWidget.Cast(m_wRoot.FindAnyWidget("TransmissionDetail"));
		m_wSpectatorText = TextWidget.Cast(m_wRoot.FindAnyWidget("SpectatorText"));
		m_wResultTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("ResultTitle"));
		m_wResultBody = TextWidget.Cast(m_wRoot.FindAnyWidget("ResultBody"));
		m_wFieldLogBody = TextWidget.Cast(m_wRoot.FindAnyWidget("FieldLogBody"));

		// The layout carries sample text so the sizes are visible in the editor; the script owns
		// these texts at runtime, so start every one of them empty.
		array<TextWidget> dynamicTexts = {m_wOrdersBody, m_wNoticeTitle, m_wNoticeBody, m_wActionLabel, m_wActionBar, m_wWaveCountdown, m_wWaveDetail, m_wOrderBody, m_wTxTitle, m_wTxBar, m_wTxDetail, m_wSpectatorText, m_wResultTitle, m_wResultBody, m_wFieldLogBody};
		foreach (TextWidget textWidget : dynamicTexts)
			SetWidgetText(textWidget, "");

		HideAllCards();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void HideAllCards()
	{
		ShowWidget(m_wOrdersCard, false);
		ShowWidget(m_wNotice, false);
		ShowWidget(m_wActionSlip, false);
		ShowWidget(m_wWavePanel, false);
		ShowWidget(m_wDecodedOrder, false);
		ShowWidget(m_wTransmissionLog, false);
		ShowWidget(m_wSpectatorStrip, false);
		ShowWidget(m_wResultSheet, false);
		ShowWidget(m_wFieldLog, false);
	}

	//------------------------------------------------------------------------------------------------
	protected static void ShowWidget(Widget w, bool visible)
	{
		if (w)
			w.SetVisible(visible);
	}

	//------------------------------------------------------------------------------------------------
	protected static void SetWidgetText(TextWidget w, string text)
	{
		if (w)
			w.SetText(text);
	}

	//------------------------------------------------------------------------------------------------
	// B ORDERS CARD
	//------------------------------------------------------------------------------------------------
	protected void UpdateOrders(notnull GRAD_Endgame_GameModeComponent gameMode, float timeSlice)
	{
		GRAD_Endgame_EPhase phase = gameMode.GetPhase();
		if (phase != m_eShownPhase)
		{
			m_eShownPhase = phase;
			if (phase != GRAD_Endgame_EPhase.WAITING && phase != GRAD_Endgame_EPhase.ENDED)
			{
				m_fOrdersLeft = ORDERS_SECONDS;
				SetWidgetText(m_wOrdersTitle, GRAD_Endgame_HudText.PhaseTitle(phase));
				m_TypedOrders.SetText(GRAD_Endgame_HudText.Orders(phase, gameMode));
			}
		}

		m_fOrdersLeft -= timeSlice;
		bool visible = m_fOrdersLeft > 0;
		ShowWidget(m_wOrdersCard, visible);
		if (!visible)
			return;

		m_TypedOrders.Update(timeSlice);
		SetWidgetText(m_wOrdersBody, m_TypedOrders.GetShown());
	}

	//------------------------------------------------------------------------------------------------
	// C NOTICE
	//------------------------------------------------------------------------------------------------
	protected void UpdateNotice(float timeSlice)
	{
		int latest = GRAD_Endgame_ClientInfo.GetNoticeCount();
		if (latest != m_iSeenNotice)
		{
			m_iSeenNotice = latest;
			string title, text;
			GRAD_Endgame_ClientInfo.GetLatestNotice(title, text);
			SetWidgetText(m_wNoticeTitle, title);
			m_TypedNotice.SetText(text);
			m_fNoticeLeft = NOTICE_SECONDS;
		}

		m_fNoticeLeft -= timeSlice;
		bool visible = m_fNoticeLeft > 0;
		ShowWidget(m_wNotice, visible);
		if (!visible)
			return;

		m_TypedNotice.Update(timeSlice);
		SetWidgetText(m_wNoticeBody, m_TypedNotice.GetShown());
	}

	//------------------------------------------------------------------------------------------------
	// D ACTION SLIP: a copy or transmission at a site the local player stands at
	//------------------------------------------------------------------------------------------------
	protected void UpdateActionSlip(float timeSlice)
	{
		IEntity me = SCR_PlayerController.GetLocalControlledEntity();
		if (!me)
		{
			ShowWidget(m_wActionSlip, false);
			return;
		}

		vector myPos = me.GetOrigin();
		string myKey = GRAD_Endgame_DocumentSiteComponent.GetFactionKeyOfPlayer(SCR_PlayerController.GetLocalPlayerId());

		foreach (GRAD_Endgame_DocumentSiteComponent site : GRAD_Endgame_DocumentSiteComponent.GetAll())
		{
			if (vector.Distance(site.GetOwner().GetOrigin(), myPos) > SITE_RANGE)
				continue;

			float fraction = site.GetCopyFraction();
			if (fraction < 0)
				continue;

			// Only your own side's copying is shown; the enemy at the same table is not your news.
			if (GRAD_Endgame_DocumentSiteComponent.GetFactionKeyOfPlayer(site.GetUserId()) != myKey)
				continue;

			ShowWidget(m_wActionSlip, true);
			SetWidgetText(m_wActionLabel, "COPYING DOCUMENTS");
			SetWidgetText(m_wActionBar, m_ActionBar.Update(fraction));
			return;
		}

		ShowWidget(m_wActionSlip, false);
		m_ActionBar.Reset();
	}

	//------------------------------------------------------------------------------------------------
	// E WAVE PANEL: dead, waiting for a wave (COALITION's countdown, fed by our faction wave)
	//------------------------------------------------------------------------------------------------
	protected void UpdateWavePanel(notnull GRAD_Endgame_GameModeComponent gameMode)
	{
		COA_RespawnManager respawn = COA_RespawnManager.GetInstance();
		bool waiting = respawn && respawn.IsLocalRespawnPending() && !gameMode.IsPermadeathActive();
		ShowWidget(m_wWavePanel, waiting);
		if (!waiting)
			return;

		float seconds = respawn.GetWaveSecondsRemaining();
		if (respawn.EndgameIsLocalReleased())
		{
			SetWidgetText(m_wWaveCountdown, "RELEASED");
			SetWidgetText(m_wWaveDetail, "Choose your spawn point.");
		}
		else if (seconds > 0)
		{
			SetWidgetText(m_wWaveCountdown, GRAD_Endgame_HudText.Clock(seconds));
			// TODO (M3): replicate "waiting / wave size" per faction, then show "2 / 3 waiting" here.
			SetWidgetText(m_wWaveDetail, "Next wave");
		}
		else
		{
			SetWidgetText(m_wWaveCountdown, "--:--");
			SetWidgetText(m_wWaveDetail, "Waiting for more players");
		}
	}

	//------------------------------------------------------------------------------------------------
	// F DECODED ORDER: pinned until dismissed
	//------------------------------------------------------------------------------------------------
	protected void UpdateDecodedOrder(float timeSlice)
	{
		string order = GRAD_Endgame_ClientInfo.GetOrder();
		if (order != m_sShownOrder)
		{
			m_sShownOrder = order;
			m_bOrderDismissed = false;
			m_TypedOrder.SetText(order);
		}

		bool visible = !order.IsEmpty() && !m_bOrderDismissed;
		ShowWidget(m_wDecodedOrder, visible);
		if (!visible)
			return;

		m_TypedOrder.Update(timeSlice);
		SetWidgetText(m_wOrderBody, m_TypedOrder.GetShown());
	}

	//------------------------------------------------------------------------------------------------
	// H TRANSMISSION LOG
	//------------------------------------------------------------------------------------------------
	protected void UpdateTransmissionLog(float timeSlice)
	{
		GRAD_Endgame_RadioSiteComponent active;
		foreach (GRAD_Endgame_RadioSiteComponent site : GRAD_Endgame_RadioSiteComponent.GetAll())
		{
			GRAD_Endgame_ETransmissionState state = site.GetState();
			if (state == GRAD_Endgame_ETransmissionState.TRANSMITTING || (state == GRAD_Endgame_ETransmissionState.INTERRUPTED && site.GetProgressSeconds() > 0))
			{
				active = site;
				if (state == GRAD_Endgame_ETransmissionState.TRANSMITTING)
					break;
			}
		}

		bool visible = active && !m_bTxHidden;
		ShowWidget(m_wTransmissionLog, visible);
		if (!visible)
		{
			if (!active)
				m_TxBar.Reset();
			return;
		}

		string myKey = GRAD_Endgame_DocumentSiteComponent.GetFactionKeyOfPlayer(SCR_PlayerController.GetLocalPlayerId());
		bool own = active.GetProgressFaction() == myKey;
		bool running = active.GetState() == GRAD_Endgame_ETransmissionState.TRANSMITTING;

		string title = "ENEMY TRANSMISSION";
		if (own)
			title = "OUR TRANSMISSION";
		if (!running)
			title += " · PAUSED";
		SetWidgetText(m_wTxTitle, title);
		SetWidgetText(m_wTxBar, m_TxBar.Update(active.GetProgressFraction(), running));

		GRAD_Endgame_Config cfg = GRAD_Endgame_ConfigLoader.Get();
		if (cfg)
		{
			float left = cfg.GetTransmissionSeconds() - active.GetProgressSeconds();
			string detail = GRAD_Endgame_HudText.Clock(left) + " to go";
			if (own && running)
				detail += " · the enemy can direction-find you";
			SetWidgetText(m_wTxDetail, detail);
		}
	}

	//------------------------------------------------------------------------------------------------
	// I SPECTATOR STRIP
	//------------------------------------------------------------------------------------------------
	protected void UpdateSpectator(notnull GRAD_Endgame_GameModeComponent gameMode)
	{
		COA_SlottingManager slotting = COA_SlottingManager.GetInstance();
		bool dead = slotting && slotting.IsPlayerConsideredDead(SCR_PlayerController.GetLocalPlayerId());
		bool visible = dead && gameMode.IsPermadeathActive() && !m_bSpectatorHidden;
		ShowWidget(m_wSpectatorStrip, visible);
		if (visible)
			SetWidgetText(m_wSpectatorText, "SPECTATING · NO RESPAWN IN PHASE 3");
	}

	//------------------------------------------------------------------------------------------------
	// J RESULT SHEET
	//------------------------------------------------------------------------------------------------
	protected void UpdateResult(notnull GRAD_Endgame_GameModeComponent gameMode, float timeSlice)
	{
		if (gameMode.GetPhase() != GRAD_Endgame_EPhase.ENDED)
		{
			m_fResultLeft = -1;
			ShowWidget(m_wResultSheet, false);
			return;
		}

		if (m_fResultLeft < 0)
		{
			m_fResultLeft = RESULT_SECONDS;
			string myKey = GRAD_Endgame_DocumentSiteComponent.GetFactionKeyOfPlayer(SCR_PlayerController.GetLocalPlayerId());
			string winner = gameMode.GetWinnerKey();
			string title = "DRAW";
			if (!winner.IsEmpty() && winner == myKey)
				title = "VICTORY";
			else if (!winner.IsEmpty())
				title = "DEFEAT";
			SetWidgetText(m_wResultTitle, title);
			m_TypedResult.SetText(GRAD_Endgame_HudText.ResultLine(winner, myKey));
		}

		m_fResultLeft -= timeSlice;
		bool visible = m_fResultLeft > 0;
		ShowWidget(m_wResultSheet, visible);
		if (!visible)
			return;

		m_TypedResult.Update(timeSlice);
		SetWidgetText(m_wResultBody, m_TypedResult.GetShown());
	}

	//------------------------------------------------------------------------------------------------
	// FIELD LOG [J]
	//------------------------------------------------------------------------------------------------
	protected void UpdateFieldLog(notnull GRAD_Endgame_GameModeComponent gameMode)
	{
		ShowWidget(m_wFieldLog, m_bFieldLogOpen);
		if (m_bFieldLogOpen)
			SetWidgetText(m_wFieldLogBody, GRAD_Endgame_HudText.FieldLog(gameMode));
	}

	//------------------------------------------------------------------------------------------------
	// KEYS
	//------------------------------------------------------------------------------------------------
	protected void AddKeyListeners()
	{
		InputManager input = GetGame().GetInputManager();
		if (!input)
			return;
		input.AddActionListener("GRAD_Endgame_FieldLog", EActionTrigger.DOWN, OnToggleFieldLog);
		input.AddActionListener("GRAD_Endgame_DismissOrder", EActionTrigger.DOWN, OnDismissOrder);
		input.AddActionListener("GRAD_Endgame_ToggleTransmission", EActionTrigger.DOWN, OnToggleTransmission);
		input.AddActionListener("GRAD_Endgame_ToggleSpectator", EActionTrigger.DOWN, OnToggleSpectator);
	}

	//------------------------------------------------------------------------------------------------
	protected void RemoveKeyListeners()
	{
		InputManager input = GetGame().GetInputManager();
		if (!input)
			return;
		input.RemoveActionListener("GRAD_Endgame_FieldLog", EActionTrigger.DOWN, OnToggleFieldLog);
		input.RemoveActionListener("GRAD_Endgame_DismissOrder", EActionTrigger.DOWN, OnDismissOrder);
		input.RemoveActionListener("GRAD_Endgame_ToggleTransmission", EActionTrigger.DOWN, OnToggleTransmission);
		input.RemoveActionListener("GRAD_Endgame_ToggleSpectator", EActionTrigger.DOWN, OnToggleSpectator);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnToggleFieldLog(float value = 0.0, EActionTrigger reason = 0)
	{
		m_bFieldLogOpen = !m_bFieldLogOpen;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnDismissOrder(float value = 0.0, EActionTrigger reason = 0)
	{
		m_bOrderDismissed = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnToggleTransmission(float value = 0.0, EActionTrigger reason = 0)
	{
		m_bTxHidden = !m_bTxHidden;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnToggleSpectator(float value = 0.0, EActionTrigger reason = 0)
	{
		m_bSpectatorHidden = !m_bSpectatorHidden;
	}
}

//------------------------------------------------------------------------------------------------
//! The words on the HUD, in one place so the field log, the map status line and the widgets say
//! the same thing. Client side; reads only replicated state and ClientInfo.
class GRAD_Endgame_HudText
{
	//------------------------------------------------------------------------------------------------
	static string Clock(float seconds)
	{
		int s = Math.Max(Math.Round(seconds), 0);
		int m = s / 60;
		s = s % 60;
		string secs = s.ToString();
		if (s < 10)
			secs = "0" + secs;
		return string.Format("%1:%2", m, secs);
	}

	//------------------------------------------------------------------------------------------------
	static string PhaseTitle(GRAD_Endgame_EPhase phase)
	{
		switch (phase)
		{
			case GRAD_Endgame_EPhase.FOB: return "ORDERS · PH 1";
			case GRAD_Endgame_EPhase.DOCUMENTS: return "ORDERS · PH 2";
			case GRAD_Endgame_EPhase.RADIO: return "ORDERS · PH 3";
		}
		return "ORDERS";
	}

	//------------------------------------------------------------------------------------------------
	static string Orders(GRAD_Endgame_EPhase phase, notnull GRAD_Endgame_GameModeComponent gameMode)
	{
		switch (phase)
		{
			case GRAD_Endgame_EPhase.FOB:
				return "Take your FOB.\nClear the militia; your spawn moves there.";
			case GRAD_Endgame_EPhase.DOCUMENTS:
				return string.Format("Documents located.\nCopy %1 sites before the enemy does.", gameMode.GetDocumentThreshold());
			case GRAD_Endgame_EPhase.RADIO:
				return "Radio sites revealed.\nTransmit with the cipher, or stop the enemy.\nNo respawn after the grace period.";
		}
		return "";
	}

	//------------------------------------------------------------------------------------------------
	static string ResultLine(string winnerKey, string myKey)
	{
		if (winnerKey.IsEmpty())
			return "Time is up. Nobody got the message out.";
		if (winnerKey == myKey)
			return "The message is out. Well done.";
		return "The enemy got the message out.";
	}

	//------------------------------------------------------------------------------------------------
	//! One line for the map: phase, time left, own count or transmission.
	static string StatusLine(notnull GRAD_Endgame_GameModeComponent gameMode)
	{
		string line = "PH" + gameMode.GetPhase().ToString();
		float left = gameMode.GetPhaseSecondsRemaining();
		if (left >= 0)
			line += " · " + Clock(left) + " left";

		foreach (GRAD_Endgame_RadioSiteComponent site : GRAD_Endgame_RadioSiteComponent.GetAll())
		{
			if (site.GetState() == GRAD_Endgame_ETransmissionState.TRANSMITTING)
				line += string.Format(" · TX %1 \%", Math.Round(site.GetProgressFraction() * 100));
		}

		return line + " · [J] log";
	}

	//------------------------------------------------------------------------------------------------
	static string FieldLog(notnull GRAD_Endgame_GameModeComponent gameMode)
	{
		string text = StatusLine(gameMode) + "\n\n";
		text += Orders(gameMode.GetPhase(), gameMode) + "\n";

		string order = GRAD_Endgame_ClientInfo.GetOrder();
		if (!order.IsEmpty())
			text += "\nDECODED ORDER\n" + order + "\n";

		array<string> notices = {};
		GRAD_Endgame_ClientInfo.GetRecentNotices(notices);
		if (!notices.IsEmpty())
		{
			text += "\nLOG\n";
			foreach (string line : notices)
				text += line + "\n";
		}
		return text;
	}
}
