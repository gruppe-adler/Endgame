//------------------------------------------------------------------------------------------------
//! Phases of an Endgame match. Order matters: AdvancePhase() moves to the next value.
enum GRAD_Endgame_EPhase
{
	WAITING,     //!< COALITION is in briefing or slotting
	FOB,         //!< Phase 1: each side takes its FOB
	DOCUMENTS,   //!< Phase 2: copy documents at militia-held sites
	RADIO,       //!< Phase 3: cipher and transmission, permadeath
	ENDED        //!< Match over
}
