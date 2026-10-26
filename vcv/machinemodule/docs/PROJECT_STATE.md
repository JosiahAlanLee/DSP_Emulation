# Repository assessment — 2026-10-26

| Repository | Main reviewed | Integration finding |
| --- | --- | --- |
| Gorgon | `f78a47d6ffc6d894a1bd6050e2d6b95b3199074f` | Shared contract and Rack follower live here. Sound-lock design merged; no sound-control packet yet. HydraPy pin updated in #24. |
| HydraPy | `903403d` | Separate Audio/Visual editor pages, recording/fullscreen fixes and note normalization already merged. Legacy score stores note/velocity only; no per-hit sound defaults. |
| DSP_Emulation | `32232c6` | DrumLab's original synthesis prototype merged in #24; per-hit CV capture exists there, but it does not contain the Machinedrum engine. OTIS/Seance/ByteBus use separate plugins. |
| Machinemodule | main `6aa2c88d41da4ad7e5ef98327db2d37983aa1bb4` | MPC wrapper and engine available. Use desktop v0.2.1 `a2b6cf2478cbe94dfe30bc42da39e221df0e2556` for this native Rack prototype. |

The existing MPC wrapper is not an appropriate timing API for Gorgon locks:
its note messages do not carry a complete parameter snapshot. This adapter
owns its event/control boundary instead of sharing independent trigger and
parameter queues. The voice core and upstream sample-rate converter remain
pinned and unmodified. DrumLab remains independent.

The cross-repository sequence is Gorgon contract → HydraPy implementation →
Gorgon Rack implementation/pin. Gorgon `AGENTS.md` requires HydraPy to merge
before the final pin move. The pending Rack PR stays a draft until that move
and fixture regeneration are recorded. Machinemodule's separate adapter can
be reviewed and built against the proposed branches meanwhile.

First scope: eight native SYN controls and machine selection per hit, four
linked Gorgon tracks or sixteen externally triggered tracks, plus optional
MDS1 ROM sample memory. Next substantial work is UW live sampling/RAM playback,
continuous control behavior, master-effect controls and hardware audio/performance
validation. Strudel-like pattern expressions and triggerless locks have not
been quietly introduced as part of the sound-link change.
