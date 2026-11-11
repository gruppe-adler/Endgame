STUB written without Workbench (2026-11-11). Widget names are what GRAD_Endgame_FieldLogDisplay
looks up; open in the Layout Editor, then set the typewriter font, the paper texture on each
*Paper image and the final sizes. Positions follow the HUD reference page. If the editor refuses
to open it, rebuild it with the same widget names.

| Layout | Used by | Widgets the script looks up |
| --- | --- | --- |
| `GRAD_Endgame_FieldLog.layout` | `GRAD_Endgame_FieldLogDisplay` (HUD manager of the player controller) | OrdersCard (OrdersTitle, OrdersBody), Notice (NoticeTitle, NoticeBody), ActionSlip (ActionLabel, ActionBar), WavePanel (WaveCountdown, WaveDetail), DecodedOrder (DecodedOrderBody), TransmissionLog (TransmissionTitle, TransmissionBar, TransmissionDetail), SpectatorStrip (SpectatorText), ResultSheet (ResultTitle, ResultBody), FieldLog (FieldLogBody) |
| `GRAD_Endgame_MapStatusLine.layout` | `GRAD_Endgame_MapStatusLine` (map configuration) | StatusLineText |

All cards start hidden; the script shows them. Every card has an image `<Card>Paper` behind its text for the paper texture.
