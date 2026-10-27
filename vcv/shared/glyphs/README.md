# Panel glyphs

The twelve Revision 3 sketching marks from *Clavicula Matricis* (Tabula XIII,
[Systems_Practice_Grimoire](https://github.com/JosiahAlanLee/Systems_Practice_Grimoire)),
drawn by that project's generator with the seal ornament left off.
`render_glyphs.py` regenerates `mark01.svg`–`mark12.svg` from a checkout of it;
`glyph.py` places one on a panel in the panel's colours.

| Module | Mark | Meaning |
|---|---|---|
| CZish | VII Revision | returns displaced (the phase bends within a cycle and returns at its end) |
| DAC Stage | V Bottleneck | fails the bypass test (the signal passes through a constrained converter word and clock) |
| Spoon Bender | VII Revision | returns displaced (each shifted repeat comes back moved) |
| OTIS | IX Substitutive change | old route struck out (REWORK replaces the loop) |
| Otis Capture | VIII Additive change | new branch, old kept (a take lands while the replaced one still sounds) |
| Otis Kit | IX Substitutive change | as OTIS: each hit substitutes its slot for whatever the voice was playing |
| Vastish | X Generative substrate | conditions beneath (the synthesis layer under the chain) |
| Gorgon ([its own repository](https://github.com/JosiahAlanLee/Gorgon)) | XI Reflexive loop | the map moves it (Rack follows Gorgon's clock and moves it back) |
| Séance | III Boundary | commons or institution (the frame is a commons the row works inside) |
| Séance Freeze | VI Regulation | returns to set point (the held frame) |
| Séance Scramble | XII Learned invariant | hard walls, one door (one seed opens onto one scatter) |
| Séance Cross | IV Centre of decision | authority, contestable (B decides A's levels; DEPTH contests) |
| Séance Wipe | V Bottleneck | fails the bypass test (bins past the edge do not get through) |
| Séance Bridge | III Boundary | the mask and activity buses cross the spectral frame boundary |
| Clocked Register | VI Regulation | reset returns the held word and internal clock to their set point |
| Lookup | IX Substitutive change | a table substitutes a new word for the incoming address |
| Lookup2 | IX Substitutive change | a computed word substitutes for the pair of incoming words |
| Counter | VIII Additive change | new branch, old kept (each edge adds to the held count rather than replacing it) |
| Switch | IV Centre of decision | authority, contestable (C decides, bit by bit, whether A or B passes) |
| Pipeline | VII Revision | returns displaced (each word comes back DELAY edges later, its bits SPREAD apart) |
| Counter 2 | VIII Additive change | as Counter: each edge adds N/D of a cycle to the held phase |
| Converter | III Boundary | voltage and byte words cross between representations |
| DrumLab Percussion | X Generative substrate | six synthesis models supply the material for each hit |
| Machinemodule | X Generative substrate | the drum engine supplies the material for each track |

They identify the modules at a glance; per the source text, a mark's meaning is
stated wherever it is used, which is this table.
