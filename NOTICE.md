# Modification and attribution notice

This project contains modified adaptations of the reverse-engineered Devilution
project and behavior studied in DevilutionX. It is not an official release of
Blizzard's source code. The code has been modified substantially for GBA memory,
rendering, input, audio, saving and link-cable multiplayer.

Devilution reference revision: 9f01757243412bb154d02d9af65c804159e6e0ea.
The historical archive records DevilutionX reference revision
 dac104babfb6187415432f428ac2516747ffc154 for early controller work. This does not
assert that every later DevilutionX-related change came from that revision.

Both archived upstream Sustainable Use License texts are included unchanged in
LICENSES/. Their SHA-256 is
049382c17367e384c622369abbeda0cab1d65658c28611717e18afead341e586.
The Devilution text was also checked against the pinned upstream revision.

Notable adaptation evidence in current source:
- src/theme_rooms.inc, src/worldgen/theme_arch.inc: explicit modified adaptations.
- src/world_rooms.inc: pinned Devilution algorithm reference.
- src/monster_ai.c: explicitly adapted monster AI routines.
- src/controller.c: describes an independent implementation informed by DevilutionX.
- src/worldgen/l2.c, l3.c, l4.c: function-body token comparisons recorded in
  the privately retained function comparison record. These are modified upstream adaptations,
  not independent code to relicense as MIT.
Paths above are relative to game-source/.

Diablo names, artwork, audio, movies and extracted game data retain their original
ownership. The code license does not grant permission to redistribute those assets.
The GBA build recipe contains the standard boot-header logo bytes required by the
BIOS; those bytes are not claimed as original project artwork or relicensed here.

Retained C/C++ reference files are pinned by per-file hashes in
`docs/reference-code-allowlist.json`. Existing provenance notes are preserved.
Excluded reference data hashes and reasons are recorded in
`docs/excluded-reference-data.json` to guard against accidental reintroduction.
