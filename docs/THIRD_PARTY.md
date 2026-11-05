# Third-party scope

| Component | Evidence / treatment |
| --- | --- |
| Devilution | https://github.com/diasurgical/devilution ; pinned reference 9f01757243412bb154d02d9af65c804159e6e0ea; unchanged SUL in LICENSES/Devilution-LICENSE.md |
| DevilutionX | https://github.com/diasurgical/DevilutionX ; archived early controller reference dac104babfb6187415432f428ac2516747ffc154; unchanged archived SUL in LICENSES/DevilutionX-LICENSE.md; all retained DevilutionX references matched this revision; runtime adaptation scope still needs review |
| source_assets C/C++ references | Recovered code references, not MPQ art; retain local LICENSE.md where recovered, otherwise the upstream license in LICENSES applies to identified upstream material. All 58 retained reference files now match pinned upstream bytes; see reference-code-allowlist.json. Historical provenance.json notes are retained unchanged. |
| Zig/Clang/LLD | External build tools; not bundled |
| Python, Pillow, numpy, mpyq, dclimplode, ffmpeg | External conversion/build dependencies; not bundled; distribution of these packages requires their own notices |
| mGBA | External optional runtime validation tool; not bundled |
| team Mupung | Credit retained; no blanket assertion that code was imported from this team |

Preserve upstream notices and modification notices. The root license cannot grant
rights in material owned by others. This inventory is not an exhaustive chain-of-title
opinion. Exact attribution for later controller/attack changes and generator functions
remains a publication-review item. Do not apply an MIT or public-domain label to this tree.
