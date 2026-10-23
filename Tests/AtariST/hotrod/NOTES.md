# Hot Rod (Activision, 1990) from the KryoFlux dump — testing branch

Branch `hotrod-kf` is a testing area. `atari-st` keeps the converter that makes the working
Hot Rod IPF from the CTR (median index, weak gap from the write splice to the sync).

## What this branch changes (all switchable by environment variable)
- The run of 0x00 bytes just before the stream sync stays fixed
  (`SUGARCONV_STREAM_WEAK_PRESYNC=1` makes it weak again).
- Stream tracks are made a multiple of 8 cells by removing or adding up to 4 cells in the middle
  of the write splice. capsimg drops the cells past the last whole byte of a track, so a 100149-cell
  KryoFlux revolution lost 5 cells and the 0x00 run read back as 0xFF
  (`SUGARCONV_STREAM_KEEP_LENGTH=1` keeps the dump's length).
- `SUGARCONV_STREAM_WEAK_BOTHWAYS=1` marks only cells that differ both forwards and backwards
  from the sync (only the splice itself) — this corrupted the city screen even from the CTR.

## Steem SSE results (TOS 1.02)
Data decoded from the KryoFlux and CTR dumps is identical on every track; only the layout around
the index and write splice differs. Tracks 2–38 hold the city title screen, 39–76 the level splash
screens and game data.

| IPF | Change | Result |
|---|---|---|
| CTR, `atari-st` | — | works fully |
| KF, `atari-st` | — | blank city title screen |
| E | KF weak bytes frozen | works |
| F | KF index moved to the CTR position | halt on city screen |
| G | KF random bytes stop before the 0x00 run | city OK, in-game graphics missing |
| H | G + CTR index position | halt on city screen |
| CTR, both-ways weak | only splice cells weak | corrupt city screen |
| CTR, this branch | | works |
| KF, this branch | | halt on city screen |
| K | this branch, index phase as in G | in-game graphics missing |
| L | this branch, index phase 10 on every track | graphics OK, cars don't follow the track in level 2 |

"Index phase" = cells left over between the last whole byte after the sync and the next index.
The working CTR has 6–15 on tracks 2–31; the halting KF sets had 0–5 on early tracks. Hatari
halted on one CTR track at phases 2 and 6. The CTR is probably a different capture: all tracks are
100152 cells, and the KF syncs sit 3–7 cells later.

Idea not tried yet: lay KF tracks out like the CTR (100152 cells, sync where the CTR has it).

## Tools (Python 3; set `CAPSIMG` to the path of libcapsimage.so)
- `cells.py IPF CYL HEAD [0]` — cells, weak count and sync positions of one track.
- `streamsurvey.py REF OTHER... [--from A --to B]` — decoded stream bytes per track against REF,
  weak bytes per image (`LIM=` limits the compared bytes, `NS=1` accepts a single sync word).
- `ipfsplice.py BASE DONOR OUT C.H...` — IPF with some tracks taken from another IPF.
- `rawtrack.py IN OUT CYL HEAD BITSFILE` — replace one track with raw cells.
- `ipfstart.py IN OUT C:BIT...` — set a track's index position (IMGE start bit).
- `ipfsplitfuzzy.py IN OUT K` — end each trailing fuzzy element K bytes early, with 0x00 bytes.
- `ipffix.py IN OUT CYL HEAD HEX` — replace a track's fuzzy elements with fixed bytes.
