# Diablo GBA 0.9.7.1

## Fixes

- Correct monster lightning, flame and acid-puddle damage to use the original Q6 (1/64 HP) damage path. Retain resistance and Mana Shield processing before conversion to the port's integer HP. Each stationary hazard keeps separate fractional carries for all four players without allocating additional RAM.
- Separate acid projectile damage from the low-damage puddle; restore its splash delay, intelligence-based duration and slower projectile speed. Acid continues to use magic resistance, as in retail PlayerMHit.
- Use three short flame segments instead of applying repeated whole-HP damage at a single impact point. Restore the Balrog-family special attack's additional 14 held ticks and emit only once per attack.
- Restore the missing lightning-resistance row on the character screen. The resistance calculation was present; the drawing loop omitted the sixth row.
- Seed both single-player New Game entry paths with the continuously running Timer0, previous campaign seed and session counters instead of relying on the audio Timer3's narrow reload range. Quest combinations may still repeat naturally; difficulty does not force a particular combination.
- Display V 0.9.7.1 in Standard and EZ4 builds.

Special thanks to @honkytonk-w1d for the detailed report on monster spell damage and attack timing, which helped identify and correct these issues.

## Validation and limits

Production-code host regressions cover direct versus continuous damage, 0% and 75% resistance, separate four-player fractional carries, acid splash/puddle duration, flame segment creation, lightning segment damage and lifetime, and one cast during the Balrog hold. Source checks cover all six character rows and both seed entry paths. The existing core test suite also passes.

Standard and EZ4 ROMs were built privately from the previously generated assets, with header and static memory-budget checks. These checks do not establish complete gameplay equivalence: the GBA port retains its bounded projectile pool, grid collision and reduced AI decision cadence. The reported 200-HP death and exact 2–3x attack-rate comparison have not been replayed from the tester's save. No new physical-hardware or link-play validation is claimed for 0.9.7.1.

Reference: diasurgical/devilution, commit 9f01757 (Source/missiles.cpp: PlayerMHit, AddAcid, MI_Acidsplat, AddAcidpud, AddFlame, MI_Flamec, MI_Lightctrl; Source/monster.cpp: M_DoRSpAttack).

Source-only release. ROMs, DIABDAT.MPQ and generated game assets are not included. Build with your own supported DIABDAT.MPQ using docs/BUILDING.md.
