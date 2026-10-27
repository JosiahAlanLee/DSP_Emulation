# Monomodule

The engine this plugin runs is **Monomodule** by **Shnolk**
(https://github.com/shnolk/monomodule), free software under the GNU Affero
General Public License v3.0. Its engine (`src/core`: the OS decoder, the DSP
harness, the host model and the resampler) is compiled unmodified from the pinned
commit in `SOURCE_REVISIONS.txt`, with the dsp56300 emulator (GPL-3.0) at the
revision Monomodule pins and with Monomodule's patch to it. The source archive
next to each release (`MonomoduleRack-*-source.tar.gz`) carries all of it,
including Monomodule's own `THIRD_PARTY_NOTICES.md`.

Monomodule is not affiliated with, endorsed by or sponsored by Elektron, and
neither is this plugin. "Elektron" and "Monomachine" are trademarks of Elektron
Music Machines MAQ AB, used only to describe what is emulated. No Elektron
software is included; the OS file is the user's own download.
