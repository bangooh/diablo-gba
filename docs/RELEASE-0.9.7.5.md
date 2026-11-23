# Diablo GBA 0.9.7.5

## Changes

- Add a corner minimap. Options > Minimap chooses **Right** (new default) or **Center** (the original full-screen map). START then DOWN still turns the map on and off. The Right map is a 64×64 box in the top-right corner. Cells are drawn top-down, turned 45 degrees clockwise to follow the isometric screen, at a quarter of the full map's width. Walls are one-pixel diagonals that use the full map's explored-edge rule. A door, or the notch above the cathedral's down stairs, opens as a five-pixel cross. As on the full map, the hero arrow stays at the centre and the map moves under it one pixel at a time. The box is eight by eight fixed BG2 tiles that never scroll, so nothing is drawn outside it, also while hardware lighting owns the windows. Moving updates only the entering rows and columns. Exploring updates only the pixels a newly seen block can change. Town NPC dots and co-op player arrows are shown inside the box. Equipment repair icons keep their place and draw over the top of the box.
- The Minimap setting is stored in bit 2 of the existing D-pad save word. Older saves keep their D-pad rotation and load as Right. A save made with Center and loaded in 0.9.7.4 or earlier falls back to the original D-pad rotation.
- Colour the Sorcerer ending movie. The movie data is unchanged: 16 gray levels at 240×117, 5FPS. Each frame now has its own 16-colour palette instead of one red ramp. Entry k is the mean original DiabVic1.smk colour of the pixels at gray level k, with gaps interpolated and 1-2-1 temporal smoothing. The palette changes in the same VBlank as the page flip. The palettes add 11,936 bytes of ROM.
- Animate the epilogue. The first still is replaced by a five-frame ping-pong loop of native Diabend.smk frames 147–159 at 5FPS, with one shared 240-colour palette. Each loop step decodes one XOR delta, and the same delta steps both forward and back. "The End" (frame 824) now also uses 240 colours. The unused first still is removed. The epilogue grows from 47,296 to 71,145 bytes.
- Restore 75% fixed-wall stippling on floors 1–4. The public fixed-wall art tool cleared every other pixel (50%), so 0.9.7.1–0.9.7.4 walls looked more see-through than the 0.9.7 release. Wall art now clears one pixel in four (odd row, odd column), the pattern floors 5–16 already used. The ground diamond stays opaque.
- Display V 0.9.7.5 in Standard and EZ4 builds.

## Validation and limits

- Corner minimap: on the reporter's floor-1 save, an mGBA screenshot of the box matches an independent model of the drawing rules, including doors, except one pixel under the arrow. No map-coloured pixel appears outside the box in 20 walking frames. Floors 5 and 13, Tristram and switching between Right and Center while the map is open were checked in mGBA.
- Ending: with the completion save bypassed in a test-only build, the tinted movie, the animated loop and "The End" play in mGBA. The tint palettes and every video frame are validated through the production C decoder. The packer checks the loop's ping-pong round trip.
- Fixed walls: floor-1 screenshots now show the 0.9.7 stipple pattern. Floor 5 is pixel-identical to the build before this change.

Measured scenes, FPS (map on, Right):

| Scene | 0.9.7.4 (Center) | 0.9.7.5 (Right) |
| --- | ---: | ---: |
| Floor 1 walking | 49.34 | 49.28 |
| Floor 5 walking | 38.42 | 39.09 |
| Floor 13 walking | 43.14 | 43.53 |

IWRAM code is 24,468 bytes and static EWRAM is 262,084 bytes. The corner map reuses the full map's buffers. Asset-free checks pass. `tests/test_build_regressions.py` covers the Minimap save word, the corner box wiring and the 75% wall rule.

Co-op player arrows in the corner map were not checked over a link cable. No new physical-hardware or link-cable validation is claimed for 0.9.7.5.

Source-only release. ROMs, DIABDAT.MPQ and generated game assets are not included. Build with your own supported DIABDAT.MPQ using docs/BUILDING.md.
