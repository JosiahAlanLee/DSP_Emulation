#!/usr/bin/env python3
"""Generate ByteBusLab's four outlined panels, layouts and component previews."""
from pathlib import Path
import argparse
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / 'shared' / 'panels'))
from panel import Panel, DEFAULT_FONT, ACCENT, DIM, write_layout


def register(font):
    p = Panel(180, font)
    p.title('Register', 'CLOCKED BYTE / 8 BITS', 6)
    for bit in range(8):
        x = 27 + 18 * (7-bit)
        p.component(f'BIT{bit}', 'light', x, 64)
        p.text(str(bit), x, 79, 6, DIM)
    p.component('SOURCE', 'selector', 16, 101, 94, 22, 'AUTO')
    p.text('SOURCE', 63, 139, 8)
    p.component('RATE', 'knob', 140, 112)
    p.text('RATE', 140, 140)
    p.text('Hz', 140, 151, 6, DIM)
    p.component('RESET_WORD', 'knob', 44, 181)
    p.text('RESET WORD', 44, 212, 7)
    p.component('LOAD', 'button', 136, 181)
    p.text('LOAD', 136, 212)
    p.component('RESET_LIGHT', 'light', 157, 181)
    p.component('WORD', 'readout', 66, 176, 48, 25, '00')
    p.text('HEX', 90, 214, 6, DIM)
    p.rule(234)
    p.block(108, 240, 48, 48)
    p.jack('BUS_IN', 48, 267, 'BUS IN')
    p.jack('BUS_OUT', 132, 267, 'BUS OUT', True)
    p.block(124, 303, 44, 57)
    p.jack('CLOCK_IN', 34, 333, 'CLOCK')
    p.jack('RESET_IN', 90, 333, 'RESET')
    p.jack('CLOCK_OUT', 146, 333, 'CLK OUT', True)
    p.component('CLOCK_LIGHT', 'light', 146, 355)
    p.text('LSB FIRST / 8 BITS + CLK', 90, 375, 6, DIM)
    return p


def lookup(font):
    p = Panel(240, font)
    p.title('Lookup', 'BYTE TRANSFORM / ORIGINAL TABLES', 9)
    for bit in range(8):
        x = 32 + 25 * (7-bit)
        p.component(f'BIT{bit}', 'light', x, 64)
        p.text(str(bit), x, 79, 6, DIM)
    p.component('FAMILY', 'selector', 16, 104, 104, 23, 'HARMONIC')
    p.text('FAMILY', 68, 145)
    p.component('SHAPE', 'knob', 185, 115)
    p.text('SHAPE', 185, 145)
    p.text('CIRCULAR', 185, 156, 6, DIM)
    p.component('PHASE', 'knob', 55, 187)
    p.text('PHASE', 55, 217)
    p.component('SEED', 'knob', 185, 187)
    p.text('PATTERN SEED', 185, 217)
    p.component('MODE', 'selector', 16, 242, 90, 23, 'MAP')
    p.text('MODE', 61, 237, 7, ACCENT)
    p.component('WORD', 'readout', 114, 242, 44, 23, '80')
    p.text('OUT HEX', 136, 237, 6, DIM)
    p.component('RESET', 'button', 198, 249)
    p.text('RESET', 198, 276, 7)
    p.component('LOCK', 'light', 222, 249)
    p.text('LOCK', 222, 266, 5.5, DIM)
    for name, x, label in [('FAMILY_CV', 34, 'FAM CV'), ('SHAPE_CV', 91, 'SHAPE CV'),
                           ('PHASE_CV', 148, 'PHASE CV'), ('SIGNAL_IN', 205, 'SIGNAL')]:
        p.jack(name, x, 307, label)
    p.rule(323)
    p.block(66, 327, 107, 43)
    for name, x, label, output in [('BUS_IN', 34, 'BUS IN', False), ('BUS_OUT', 91, 'BUS OUT', True),
                                   ('AUDIO_OUT', 148, 'AUDIO', True), ('RESET_IN', 205, 'RESET', False)]:
        p.jack(name, x, 353, label, output)
    p.text('8 BITS / EXACT BYTE OUTPUT', 120, 376, 6, DIM)
    return p


def converter(font):
    p = Panel(210, font)
    p.title('Converter', 'BYTE BUS / ADC + DAC', 3, 18)
    p.component('WORD', 'readout', 18, 65, 174, 25, '80                         80')
    for side, x in [('ADC', 50), ('DAC', 160)]:
        p.text(side, x, 61, 7, ACCENT)
        p.component(f'{side}_GAIN', 'knob', x, 117)
        p.text('GAIN', x, 146)
        p.component(f'{side}_OFFSET', 'knob', x, 180)
        p.text('OFFSET', x, 209)
        p.component(f'{side}_RANGE', 'switch', x, 235)
        p.text('±10 V', x+12, 231, 6.5, anchor='start')
        p.text('±5 V', x+12, 246, 6.5, anchor='start')
    p.component('CLIP', 'light', 105, 235)
    p.text('CLIP', 105, 254, 6, DIM)
    p.rule(265)
    p.block(18, 321, 174, 49)
    for name, x, label in [('SIGNAL_IN', 42, 'SIGNAL'), ('CLOCK_IN', 105, 'CLOCK'), ('BUS_IN', 168, 'BUS IN')]:
        p.jack(name, x, 297, label)
    for name, x, label in [('BUS_OUT', 42, 'ADC BUS'), ('CLOCK_OUT', 105, 'CLK OUT'), ('SIGNAL_OUT', 168, 'DAC OUT')]:
        p.jack(name, x, 352, label, True)
    p.text('8 BITS / INDEPENDENT PATHS', 105, 377, 6, DIM)
    return p


def counter(font):
    p = Panel(180, font)
    p.title('Counter', 'BYTE COUNTER / 8 BITS', 8)
    for bit in range(8):
        x = 27 + 18 * (7-bit)
        p.component(f'BIT{bit}', 'light', x, 64)
        p.text(str(bit), x, 79, 6, DIM)
    p.component('SOURCE', 'selector', 12, 92, 74, 22, 'AUTO')
    p.text('SOURCE', 49, 128, 8)
    p.component('MODE', 'selector', 94, 92, 74, 22, 'LOOP')
    p.text('MODE', 131, 128, 8)
    for name, x in [('RATE', 32), ('INC', 71), ('MOD', 109), ('SEED', 148)]:
        p.component(name, 'knob', x, 167)
        p.text(name, x, 197)
    p.text('Hz', 32, 208, 6, DIM)
    p.component('WORD', 'readout', 56, 207, 52, 25, '00')
    p.text('HEX', 82, 244, 6, DIM)
    p.component('LOAD', 'button', 140, 219)
    p.text('LOAD', 140, 244)
    p.component('RESET_LIGHT', 'light', 161, 219)
    p.rule(252)
    for name, x, label in [('BUS_IN', 34, 'BUS IN'), ('CLOCK_IN', 90, 'CLOCK'), ('RESET_IN', 146, 'RESET')]:
        p.jack(name, x, 282, label)
    p.block(12, 312, 156, 52)
    for name, x, label in [('BUS_OUT', 34, 'BUS OUT'), ('CLOCK_OUT', 90, 'CLK OUT'), ('CARRY_OUT', 146, 'CARRY')]:
        p.jack(name, x, 342, label, True)
    p.component('CLOCK_LIGHT', 'light', 90, 359)
    p.component('CARRY_LIGHT', 'light', 146, 359)
    p.text('LSB FIRST / 8 BITS + CLK', 90, 375, 6, DIM)
    return p


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', default=DEFAULT_FONT)
    args = parser.parse_args()
    panels = {'reg': register(args.font), 'lookup': lookup(args.font), 'converter': converter(args.font),
              'counter': counter(args.font)}
    for name, filename in [('reg', 'Register'), ('lookup', 'Lookup'), ('converter', 'Converter'), ('counter', 'Counter')]:
        panels[name].write(ROOT, filename)
    write_layout(ROOT / 'src/Layout.hpp', 'bytebus_layout', panels)
    print('Wrote four ByteBusLab panels, previews and src/Layout.hpp')


if __name__ == '__main__':
    main()
