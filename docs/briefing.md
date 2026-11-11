# Endgame 1989: briefing

Two sides. One message that has to get out. The winner is the side that transmits it first.

BLUFOR and OPFOR both fight the same local militia (FIA), which holds every site on the map and is hostile to both of you. Nobody controls the ground until they have cleared it.

Numbers below are the defaults. The server admin can change them.

## How a match runs

| Phase | What your side does | How it ends |
| --- | --- | --- |
| 1. FOB | Clear the militia from your FOB. | The first FOB cleared on the map starts phase 2. |
| 2. Documents | Copy documents at 3 of the marked sites. | The first side with 3 copies starts phase 3. |
| 3. Radio | Find the cipher, take it to a radio site and transmit. | One side finishes the transmission, or the match ends otherwise. |

You always know the current phase and the time left from the map status line, and the field log on **[J]** shows the rest.

## Phase 1: take your FOB

- Your FOB is marked on your map. A garrison of militia holds it. Clear every group.
- When it is clear, your FOB is yours: your spawn moves there, everyone waiting to respawn is released at once, and an ammunition box and light vehicles stand ready (a few vehicles per side; a lost or abandoned one is replaced after about 7 minutes).
- Taking your FOB first is an advantage, because the first cleared FOB starts phase 2 for both sides. The other side is told, and still has its own FOB to take.
- You do not see the documents until your own FOB is taken. You cannot copy documents until then either.

## Phase 2: documents

- Documents lie at several sites, but only 5 are active in a match. The map shows each active site as an **area** about 150 m across, not an exact point, so you have to search inside it.
- A site is guarded by militia. Clear it, then hold the **copy action** at the table for about 10 seconds. The action stays visible to teammates, and a second player cannot start a copy at the same site while one runs.
- **One copy per site per side.** You need **3 copies from 3 different sites**. The opponent can copy the same site later.
- You do not see how many copies the other side has.
- When one side reaches 3, it learns where the **cipher** lies (see phase 3) and phase 3 starts for both sides.
- The sites are chosen so that both sides have to travel about the same distance.

## Phase 3: the radio phase

### What happens at the start
- The radio sites are revealed on the map to both sides (2 sites per match).
- The **cipher** appears as an item at a hidden place. The side that finished the documents first gets its position as a decoded order, delivered to its squad leaders. The other side is not told where it is and has to find it or take it from someone.
- Each side gets one **free respawn**. After about 30 seconds, respawn is switched off: if you die now you stay dead and can only spectate.

### Transmitting
- Only the **radio operator** can start a transmission, and only while carrying the cipher.
- Walk to a radio site and hold the transmit action for about 3 seconds. The transmission then runs on its own for **12 minutes**.
- The operator must stay within **5 m** of the set the whole time. If the operator leaves, dies or loses the cipher, the transmission is **interrupted** and the progress is paused. Starting it again continues from there. The cipher can be taken from a dead body.
- The progress belongs to the side that made it. If the other side transmits at the same set, it starts at 0.
- A transmitting radio set can be **heard** at about 150 m.
- **Direction finding:** the enemy gets a search area of about 300 m around a transmitting site every minute. Expect them to come.
- When a transmission reaches its end, the transmitting side wins.

### Defending
- Hold the site you transmit from. Kill the other operator. Take the cipher.
- Both sides may transmit at any radio site.

## Respawn

- **Dead players respawn in waves, not at once.** Your side's wave is released about 4.5 minutes after the oldest waiting death, once at least a third of your team (rounded up) is waiting. Everybody waiting then goes together. The longest anyone waits is 20 minutes.
- You do not choose when to return: the wave clock is shown in the HUD while you wait.
- You can pick where to spawn from the spawn points your side holds. Before your FOB is taken that is the start point; after, it moves to your FOB.
- Nobody can spectate while waiting for a wave.

## How the match ends

- **Transmission finished:** that side wins.
- **Elimination in phase 3:** a side with no living players loses. A match between two sides that both started the radio phase with players can end this way.
- **Time:** the whole match is limited to **60 minutes**. At zero it ends as a **draw**.
- After the end, the match is shown once on the map as a **replay** in fast forward, then the after-action screen opens.

## Keys and screens

| Key | What it shows |
| --- | --- |
| **J** | Field log: phase, time left, your document count, the order, recent notices |
| **K** | Dismiss the decoded order card |
| **T** | Show or hide the transmission card |
| **U** | Spectator card (dead in phase 3) |

Your objectives are also in the vanilla **task list** and on the map. Only your side sees its own objectives.

## Rules of thumb

- Do not leave your FOB empty. The first cleared FOB starts the clock for everyone.
- Documents are won by travel, not by fighting alone: check the area, then clear the site.
- A squad leader and a radio operator are worth more than a rifleman in phase 3. Keep them alive.
- The militia does not respawn. A cleared site stays cleared, for both sides.
