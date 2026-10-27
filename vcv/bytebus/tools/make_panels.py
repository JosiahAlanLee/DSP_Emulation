#!/usr/bin/env python3
"""Generate ByteBusLab's eight outlined panels, layouts and component previews."""
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
    p.jack('JUMP_IN', 120, 180, 'JUMP')
    p.component('JUMP_LIGHT', 'light', 138, 160)
    p.component('JUMP_BIT', 'selector', 96, 198, 48, 18, 'BIT 0')
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


def byte_switch(font):
    p = Panel(180, font)
    p.title('Switch', 'BYTE COMMUTATOR / A B C', 4)
    for bit in range(8):
        x = 27 + 18 * (7-bit)
        p.component(f'BIT{bit}', 'light', x, 64)
        p.text(str(bit), x, 79, 6, DIM)
    p.component('SOURCE', 'selector', 12, 92, 74, 22, 'AUTO')
    p.text('SOURCE', 49, 128, 8)
    p.component('MODE', 'selector', 94, 92, 74, 22, 'SELECT')
    p.text('MODE', 131, 128, 8)
    for name, x, label in [('RATE', 32, 'RATE'), ('B_WORD', 71, 'B WORD'), ('C_WORD', 109, 'C WORD')]:
        p.component(name, 'knob', x, 167)
        p.text(label, x, 197, 7 if ' ' in label else 8)
    p.text('Hz', 32, 208, 6, DIM)
    p.component('SEL_BIT', 'selector', 128, 158, 40, 18, 'BIT 0')
    p.text('SEL BIT', 148, 197, 7)
    p.component('WORD', 'readout', 64, 211, 52, 25, '00')
    p.text('HEX', 90, 246, 6, DIM)
    p.rule(252)
    for name, x, label in [('A_IN', 31, 'A'), ('B_IN', 70, 'B'), ('C_IN', 110, 'C'), ('CLOCK_IN', 149, 'CLOCK')]:
        p.jack(name, x, 282, label)
    p.block(42, 312, 96, 52)
    for name, x, label in [('BUS_OUT', 66, 'BUS OUT'), ('CLOCK_OUT', 114, 'CLK OUT')]:
        p.jack(name, x, 342, label, True)
    p.component('CLOCK_LIGHT', 'light', 114, 359)
    p.text('LSB FIRST / 8 BITS + CLK', 90, 375, 6, DIM)
    return p


def pipeline(font):
    p = Panel(180, font)
    p.title('Pipeline', 'BYTE DELAY / 64 EDGES', 7)
    for bit in range(8):
        x = 27 + 18 * (7-bit)
        p.component(f'BIT{bit}', 'light', x, 64)
        p.text(str(bit), x, 79, 6, DIM)
    p.component('SOURCE', 'selector', 16, 92, 94, 22, 'AUTO')
    p.text('SOURCE', 63, 128, 8)
    p.component('RATE', 'knob', 140, 103)
    p.text('RATE', 140, 131)
    p.text('Hz', 140, 142, 6, DIM)
    for name, x in [('DELAY', 34), ('SPREAD', 90)]:
        p.component(name, 'knob', x, 167)
        p.text(name, x, 197)
    p.component('LOOP', 'button', 146, 167)
    p.text('LOOP', 146, 197)
    p.component('WORD', 'readout', 22, 207, 52, 25, '00')
    p.text('HEX', 48, 244, 6, DIM)
    p.component('CLEAR', 'button', 128, 219)
    p.text('CLEAR', 128, 244)
    p.component('RESET_LIGHT', 'light', 149, 219)
    p.rule(252)
    for name, x, label in [('BUS_IN', 31, 'BUS IN'), ('CLOCK_IN', 70, 'CLOCK'), ('RESET_IN', 110, 'RESET'), ('LOOP_IN', 149, 'LOOP')]:
        p.jack(name, x, 282, label)
    p.block(42, 312, 96, 52)
    for name, x, label in [('BUS_OUT', 66, 'BUS OUT'), ('CLOCK_OUT', 114, 'CLK OUT')]:
        p.jack(name, x, 342, label, True)
    p.component('CLOCK_LIGHT', 'light', 114, 359)
    p.text('LSB FIRST / 8 BITS + CLK', 90, 375, 6, DIM)
    return p


def lookup2(font):
    p = Panel(240, font)
    p.title('Lookup2', 'TWO-WORD TRANSFORM / A B', 9)
    for bit in range(8):
        x = 32 + 25 * (7-bit)
        p.component(f'BIT{bit}', 'light', x, 64)
        p.text(str(bit), x, 79, 6, DIM)
    p.component('FAMILY', 'selector', 16, 104, 104, 23, 'LOGIC')
    p.text('FAMILY', 68, 145)
    p.component('SHAPE', 'knob', 185, 115)
    p.text('SHAPE', 185, 145)
    p.text('CIRCULAR', 185, 156, 6, DIM)
    p.component('B_WORD', 'knob', 55, 187)
    p.text('B WORD', 55, 217)
    p.jack('JUMP_IN', 120, 180, 'JUMP')
    p.component('JUMP_LIGHT', 'light', 138, 160)
    p.component('JUMP_BIT', 'selector', 96, 198, 48, 18, 'BIT 0')
    p.component('RESET', 'button', 185, 187)
    p.text('RESET', 185, 211, 7)
    p.text('JUMP STEPS', 185, 221, 6, DIM)
    p.component('WORD', 'readout', 94, 236, 52, 25, '00')
    p.text('OUT HEX', 120, 271, 6, DIM)
    for name, x, label in [('A_IN', 28, 'A'), ('B_IN', 74, 'B'), ('FAMILY_CV', 120, 'FAM CV'),
                           ('SHAPE_CV', 166, 'SHAPE CV'), ('RESET_IN', 212, 'RESET')]:
        p.jack(name, x, 307, label)
    p.rule(323)
    p.block(66, 327, 107, 43)
    for name, x, label in [('BUS_OUT', 91, 'BUS OUT'), ('AUDIO_OUT', 148, 'AUDIO')]:
        p.jack(name, x, 353, label, True)
    p.text('8 BITS / A CLOCKS THE OUTPUT', 120, 376, 6, DIM)
    return p


def counter2(font):
    p = Panel(180, font)
    p.title('Counter 2', 'PHASE AT N/D OF THE CLOCK', 8)
    for bit in range(8):
        x = 27 + 18 * (7-bit)
        p.component(f'BIT{bit}', 'light', x, 64)
        p.text(str(bit), x, 79, 6, DIM)
    p.component('SOURCE', 'selector', 16, 92, 94, 22, 'AUTO')
    p.text('SOURCE', 63, 128, 8)
    p.component('RATE', 'knob', 140, 103)
    p.text('RATE', 140, 131)
    p.text('Hz', 140, 142, 6, DIM)
    for name, x in [('N', 34), ('D', 90), ('SEED', 146)]:
        p.component(name, 'knob', x, 167)
        p.text(name, x, 197)
    p.component('WORD', 'readout', 16, 207, 44, 25, '00')
    p.text('HEX', 38, 244, 6, DIM)
    p.component('RATIO', 'readout', 66, 207, 50, 25, '1/16')
    p.text('N/D', 91, 244, 6, DIM)
    p.component('LOAD', 'button', 140, 219)
    p.text('LOAD', 140, 244)
    p.component('RESET_LIGHT', 'light', 161, 219)
    p.rule(252)
    for name, x, label in [('CLOCK_IN', 34, 'CLOCK'), ('RESET_IN', 90, 'RESET'), ('RATIO_IN', 146, 'RATIO')]:
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
              'counter': counter(args.font), 'byte_switch': byte_switch(args.font),
              'pipeline': pipeline(args.font), 'lookup2': lookup2(args.font),
              'counter2': counter2(args.font)}
    for name, filename in [('reg', 'Register'), ('lookup', 'Lookup'), ('converter', 'Converter'), ('counter', 'Counter'),
                           ('byte_switch', 'Switch'), ('pipeline', 'Pipeline'),
                           ('lookup2', 'Lookup2'), ('counter2', 'Counter2')]:
        panels[name].write(ROOT, filename)
    write_layout(ROOT / 'src/Layout.hpp', 'bytebus_layout', panels)
    print('Wrote eight ByteBusLab panels, previews and src/Layout.hpp')


if __name__ == '__main__':
    main()
