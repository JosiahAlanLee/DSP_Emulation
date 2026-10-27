#!/usr/bin/env python3
"""Generate the outlined panels of Otis Kit and Otis Capture, their layout header and component previews."""
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


def capture(font):
    p = Panel(120, font)
    p.title('Capture', 'TAKES INTO OTIS KIT', 8, 15, 100)
    p.component('STATUS', 'readout', 10, 54, 100, 34, 'NO KIT')
    for name, y, label, preview in [('SLOT', 104, 'SLOT', 'SLOT 1'), ('LENGTH', 142, 'LENGTH', '4 BEATS'), ('SOURCE', 180, 'SOURCE', 'IN')]:
        p.text(label, 60, y - 4, 6.5, ACCENT)
        p.component(name, 'selector', 10, y, 100, 18, preview)
    for name, x, label in [('GAIN', 24, 'GAIN'), ('TEMPO', 62, 'TEMPO')]:
        p.component(name, 'knob', x, 224)
        p.text(label, x, 252, 7)
    p.text('NO CLOCK', 62, 261, 5.5, DIM)
    p.component('ARM', 'button', 98, 224)
    p.component('ARM_LIGHT', 'light', 98, 208)
    p.text('ARM', 98, 252, 7)
    p.rule(268)
    for name, x, label in [('AUDIO_IN', 22, 'IN'), ('CLOCK_IN', 60, 'CLOCK'), ('ARM_IN', 98, 'ARM')]:
        p.jack(name, x, 296, label)
    p.block(10, 314, 100, 50)
    for name, x, label in [('REC_OUT', 38, 'REC'), ('DONE_OUT', 82, 'DONE')]:
        p.jack(name, x, 342, label, True)
    p.component('REC_LIGHT', 'light', 38, 359)
    p.text('RIGHT OF OTIS KIT', 60, 376, 5.5, DIM)
    return p


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', default=DEFAULT_FONT)
    args = parser.parse_args()
    panels = {'kit': kit(args.font), 'capture': capture(args.font)}
    panels['kit'].write(ROOT, 'OtisKit')
    panels['capture'].write(ROOT, 'OtisCapture')
    write_layout(ROOT / 'src/KitLayout.hpp', 'otis_kit_layout', panels)
    print('Wrote the Otis Kit and Otis Capture panels, previews and src/KitLayout.hpp')


if __name__ == '__main__':
    main()
