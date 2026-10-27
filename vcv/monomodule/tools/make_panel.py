#!/usr/bin/env python3
"""Generate Monomodule's outlined panel, widget layout and component preview."""
from pathlib import Path
import argparse
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / 'shared' / 'panels'))
from panel import Panel, DEFAULT_FONT, ACCENT, DIM, write_layout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', default=DEFAULT_FONT)
    args = parser.parse_args()
    p = Panel(270, args.font)  # 18 HP, Machinemodule's width
    p.title('Monomodule', '6 TRACKS / NOTES + PER-NOTE SYN CV', 6, 17)
    for name, x in [('READY', 183), ('LINK', 210)]:
        p.component(name, 'light', x, 24)
        p.text(name, x, 42, 5.5, DIM)
    for name, x, label in [('MACHINE', 48, 'MACHINE'), ('TEMPO', 135, 'TEMPO'), ('GAIN', 222, 'GAIN')]:
        p.component(name, 'knob', x, 78)
        p.text(label, x, 108)
    p.component('MACHINE_NAME', 'readout', 18, 117, 234, 20, 'LOCAL 04 / SWAVE SAW')
    for i, (name, label) in enumerate([('TRIG', 'TRIG'), ('VOCT', 'V/OCT'), ('MACHINE_CV', 'MACHINE CV'), ('RESET', 'RESET')]):
        p.jack(name, 32 + 69*i, 166, label)
    p.rule(183)
    for i in range(8):
        x, y = 30 + 131*(i%2), 211 + 36*(i//2)
        p.component(f'P{i+1}', 'small_knob', x, y)
        p.text(f'P{i+1}', x, y-18, 8, ACCENT)
        p.jack(f'P{i+1}_CV', x+65, y, 'CV')
    p.rule(338)
    p.block(18, 341, 234, 38)
    for name, x, label in [('LEFT', 40, 'LEFT'), ('RIGHT', 103, 'RIGHT'), ('TRACKS_L', 166, 'TRACKS L'), ('TRACKS_R', 229, 'TRACKS R')]:
        p.jack(name, x, 365, label, True)
    p.write(ROOT, 'Monomodule')
    write_layout(ROOT / 'src/Layout.hpp', 'monomodule_layout', {'voice': p})
    print('Wrote Monomodule panel, preview and src/Layout.hpp')


if __name__ == '__main__':
    main()
