#!/usr/bin/env python3
"""Generate Otis Kit's outlined panel, its layout header and a component preview."""
from pathlib import Path
import argparse
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / 'shared' / 'panels'))
from panel import Panel, DEFAULT_FONT, ACCENT, DIM, write_layout

COLUMNS = [('TUNE', 70), ('DECAY', 106), ('LEVEL', 142), ('FILTER', 178), ('START', 214)]
REV_X, ROW0, ROW = 246, 84, 27


def kit(font):
    p = Panel(270, font)
    p.title('Otis Kit', 'BANK / 8 SLOTS / ES5505 PLAYBACK', 9)
    p.text('SLOT', 26, 64, 7, ACCENT)
    for name, x in COLUMNS:
        p.text(name, x, 64, 7, ACCENT)
    p.text('REV', REV_X, 64, 7, ACCENT)
    for s in range(8):
        y = ROW0 + ROW * s
        p.text(str(s + 1), 20, y + 3, 9)
        p.component(f'HIT{s}', 'light', 36, y)
        for name, x in COLUMNS:
            p.component(f'{name}{s}', 'small_knob', x, y)
        p.component(f'REV{s}', 'switch', REV_X, y)
    p.rule(300)
    for name, x, label in [('GATE_IN', 34, 'GATE'), ('SLOT_IN', 82, 'SLOT'), ('PITCH_IN', 130, 'PITCH'), ('LEVEL_IN', 178, 'LEVEL')]:
        p.jack(name, x, 334, label)
    p.block(212, 308, 46, 56)
    p.jack('OUT', 235, 334, 'OUT', True)
    p.text('GATE CHANNEL n PLAYS SLOT n UNLESS SLOT IS PATCHED', 135, 372, 5.5, DIM)
    return p


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', default=DEFAULT_FONT)
    args = parser.parse_args()
    panel = kit(args.font)
    panel.write(ROOT, 'OtisKit')
    write_layout(ROOT / 'src/KitLayout.hpp', 'otis_kit_layout', {'kit': panel})
    print('Wrote Otis Kit panel, preview and src/KitLayout.hpp')


if __name__ == '__main__':
    main()
