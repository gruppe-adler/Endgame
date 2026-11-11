//------------------------------------------------------------------------------------------------
//! What the local player's side knows, as received from the server. The HUD and the map layer
//! read it; both still have to be built in Workbench (layouts). Until then every change is also
//! shown as an on-screen hint, so the rules can be tested.
class GRAD_Endgame_ClientInfo
{
	protected static ref map<string, ref GRAD_Endgame_MarkerData> s_mMarkers = new map<string, ref GRAD_Endgame_MarkerData>();
	protected static string s_sOrder;
	protected static ref map<string, ref SCR_MapMarkerBase> s_mMapMarkers = new map<string, ref SCR_MapMarkerBase>();   //!< the same markers as vanilla map markers, local to this client
	protected static ref ScriptInvoker s_OnChanged = new ScriptInvoker();

	// Notices for the HUD (widget C) and the field log
	protected static ref array<string> s_aNoticeTitles = {};
	protected static ref array<string> s_aNoticeTexts = {};
	protected static int s_iNoticeCount;
	protected static const int NOTICE_HISTORY = 12;

	//------------------------------------------------------------------------------------------------
	//! A server notice arrived. Goes to the field-log HUD; without the HUD (not registered yet, or
	//! another game mode) it falls back to a vanilla hint so nothing is lost.
	static void PushNotice(string title, string text, float hintSeconds = 8)
	{
		s_aNoticeTitles.Insert(title);
		s_aNoticeTexts.Insert(text);
		if (s_aNoticeTitles.Count() > NOTICE_HISTORY)
		{
			s_aNoticeTitles.RemoveOrdered(0);
			s_aNoticeTexts.RemoveOrdered(0);
		}
		s_iNoticeCount++;

		if (!GRAD_Endgame_FieldLogDisplay.GetInstance())
			SCR_HintManagerComponent.ShowCustomHint(text, title, hintSeconds);
	}

	//------------------------------------------------------------------------------------------------
	//! Counts every notice ever pushed; the HUD compares it to know when a new one came.
	static int GetNoticeCount()
	{
		return s_iNoticeCount;
	}

	//------------------------------------------------------------------------------------------------
	static void GetLatestNotice(out string title, out string text)
	{
		title = "";
		text = "";
		if (s_aNoticeTitles.IsEmpty())
			return;
		title = s_aNoticeTitles[s_aNoticeTitles.Count() - 1];
		text = s_aNoticeTexts[s_aNoticeTexts.Count() - 1];
	}

	//------------------------------------------------------------------------------------------------
	//! Newest first, "TITLE: text", for the field log.
	static void GetRecentNotices(notnull array<string> lines)
	{
		lines.Clear();
		for (int i = s_aNoticeTitles.Count() - 1; i >= 0; i--)
			lines.Insert(string.Format("%1: %2", s_aNoticeTitles[i], s_aNoticeTexts[i]));
	}

	//------------------------------------------------------------------------------------------------
	static map<string, ref GRAD_Endgame_MarkerData> GetMarkers()
	{
		return s_mMarkers;
	}

	//------------------------------------------------------------------------------------------------
	//! The decoded order, or empty if this player has none.
	static string GetOrder()
	{
		return s_sOrder;
	}

	//------------------------------------------------------------------------------------------------
	//! Fired on the client whenever a marker or the order changes.
	static ScriptInvoker GetOnChanged()
	{
		return s_OnChanged;
	}

	//------------------------------------------------------------------------------------------------
	static void Set(string id, vector position, float radius, string label)
	{
		s_mMarkers.Set(id, new GRAD_Endgame_MarkerData(id, position, radius, label));
		ShowOnMap(id, position, label);
		s_OnChanged.Invoke();
	}

	//------------------------------------------------------------------------------------------------
	//! Show a marker on the player's map as a vanilla custom marker that only this client has.
	protected static void ShowOnMap(string id, vector position, string label)
	{
		HideOnMap(id);

		SCR_MapMarkerManagerComponent manager = SCR_MapMarkerManagerComponent.GetInstance();
		if (!manager)
		{
			Print("[GRAD_Endgame] No map marker manager; marker " + id + " only shown as a hint.", LogLevel.WARNING);
			return;
		}

		SCR_MapMarkerBase marker = new SCR_MapMarkerBase();
		marker.SetType(SCR_EMapMarkerType.PLACED_CUSTOM);
		marker.SetWorldPos(position[0], position[2]);
		marker.SetCustomText(label);
		manager.InsertStaticMarker(marker, true);
		s_mMapMarkers.Set(id, marker);
	}

	//------------------------------------------------------------------------------------------------
	protected static void HideOnMap(string id)
	{
		SCR_MapMarkerBase marker = s_mMapMarkers.Get(id);
		if (!marker)
			return;

		s_mMapMarkers.Remove(id);
		SCR_MapMarkerManagerComponent manager = SCR_MapMarkerManagerComponent.GetInstance();
		if (manager)
			manager.RemoveStaticMarker(marker);
	}

	//------------------------------------------------------------------------------------------------
	static void Remove(string id)
	{
		s_mMarkers.Remove(id);
		HideOnMap(id);
		s_OnChanged.Invoke();
	}

	//------------------------------------------------------------------------------------------------
	static void Clear()
	{
		array<string> ids = {};
		foreach (string id, SCR_MapMarkerBase marker : s_mMapMarkers)
			ids.Insert(id);
		foreach (string id : ids)
			HideOnMap(id);
		s_mMarkers.Clear();
		s_OnChanged.Invoke();
	}

	//------------------------------------------------------------------------------------------------
	static void SetOrder(string text)
	{
		s_sOrder = text;
		s_OnChanged.Invoke();
	}
}

//------------------------------------------------------------------------------------------------
//! Owner RPCs for Endgame, added to COALITION's server-to-owner component on each player's own
//! controller, so no prefab has to change.
//!
//! WRITTEN WITHOUT WORKBENCH (2026-11-10): not compiled, not run. To confirm there: that a
//! modded class may add RplRpc methods to this component (the same question as AdminRequests).
//! TODO (Workbench): draw the markers on the map (circle for an area, icon for a point). For now
//! a new marker shows a hint with its grid position.
modded class COA_PlayerRplToOwnerManager
{
	//------------------------------------------------------------------------------------------------
	void EndgameSetMarker(string id, vector position, float radius, string label)
	{
		Rpc(RpcDo_EndgameSetMarker, id, position, radius, label);
	}

	//------------------------------------------------------------------------------------------------
	void EndgameRemoveMarker(string id)
	{
		Rpc(RpcDo_EndgameRemoveMarker, id);
	}

	//------------------------------------------------------------------------------------------------
	void EndgameClearMarkers()
	{
		Rpc(RpcDo_EndgameClearMarkers);
	}

	//------------------------------------------------------------------------------------------------
	void EndgameShowHint(string title, string text)
	{
		Rpc(RpcDo_EndgameShowHint, title, text);
	}

	//------------------------------------------------------------------------------------------------
	void EndgameReceiveOrder(string text)
	{
		Rpc(RpcDo_EndgameReceiveOrder, text);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_EndgameSetMarker(string id, vector position, float radius, string label)
	{
		bool isNew = !GRAD_Endgame_ClientInfo.GetMarkers().Contains(id);
		GRAD_Endgame_ClientInfo.Set(id, position, radius, label);

		if (!isNew)
			return;

		string where = string.Format("near %1 / %2", Math.Round(position[0]), Math.Round(position[2]));
		if (radius > 0)
			where = string.Format("within %1 m of %2 / %3", Math.Round(radius), Math.Round(position[0]), Math.Round(position[2]));
		SCR_HintManagerComponent.ShowCustomHint(where, label, 8);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_EndgameRemoveMarker(string id)
	{
		GRAD_Endgame_ClientInfo.Remove(id);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_EndgameClearMarkers()
	{
		GRAD_Endgame_ClientInfo.Clear();
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_EndgameShowHint(string title, string text)
	{
		GRAD_Endgame_ClientInfo.PushNotice(title, text);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_EndgameReceiveOrder(string text)
	{
		GRAD_Endgame_ClientInfo.SetOrder(text);
		// The HUD pins the order (widget F); without it, fall back to a long hint.
		if (!GRAD_Endgame_FieldLogDisplay.GetInstance())
			SCR_HintManagerComponent.ShowCustomHint(text, "Decoded order", 20);
	}
}
