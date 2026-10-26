#!/usr/bin/env python3
"""Generate Machinemodule's outlined panel, widget layout and component preview."""
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
    p = Panel(270, args.font)  # 18 HP, unchanged from the original mm-sized panel.
    p.title('Machinemodule', '16 TRACKS / PER-HIT SYN CV', 10, 17)
    for name, x in [('READY', 183), ('LINK', 210)]:
        p.component(name, 'light', x, 24)
        p.text(name, x, 42, 5.5, DIM)
    for name, x, label in [('MACHINE', 48, 'MACHINE ID'), ('TEMPO', 135, 'TEMPO'), ('GAIN', 222, 'GAIN')]:
        p.component(name, 'knob', x, 78)
        p.text(label, x, 108)
    p.component('MACHINE_NAME', 'readout', 18, 117, 234, 20, 'LOCAL 016 / LOAD OS FROM MENU')
    for i, label in enumerate(['TRIG', 'VELOCITY', 'MACHINE CV', 'RESET']):
        p.jack(label.replace(' ', '_'), 32 + 69*i, 166, label)
    p.rule(183)
    for i in range(8):
        x, y = 30 + 131*(i%2), 211 + 36*(i//2)
        p.component(f'P{i+1}', 'small_knob', x, y)
        p.text(f'P{i+1}', x, y-18, 8, ACCENT)
        p.jack(f'P{i+1}_CV', x+65, y, 'CV')
    p.rule(338)
    p.block(26, 341, 218, 38)
    for name, x, label in [('LEFT', 48, 'LEFT'), ('RIGHT', 135, 'RIGHT'), ('TRACKS', 222, 'TRACKS ×16')]:
        p.jack(name, x, 365, label, True)
    p.write(ROOT, 'Machinemodule')
    write_layout(ROOT / 'src/Layout.hpp', 'machinemodule_layout', {'voice': p})
    print('Wrote Machinemodule panel, preview and src/Layout.hpp')


if __name__ == '__main__':
    main()
