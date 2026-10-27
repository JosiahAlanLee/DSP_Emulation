#!/usr/bin/env python3
"""Generate ByteBusLab's ten outlined panels, layouts and component previews."""
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


def fan(font):
    p = Panel(240, font)
    p.title('Fan', '32-WAY ROUTER / 5-BIT ADDRESS', 5)
    for bit in range(5):
        x = 30 + 24 * (4-bit)
        p.component(f'BIT{bit}', 'light', x, 64)
        p.text(str(bit), x, 79, 6, DIM)
    p.component('ADDRESS', 'readout', 160, 53, 64, 25, '1')
    p.text('STEP', 192, 86, 6, DIM)
    p.component('SOURCE', 'selector', 12, 98, 70, 22, 'AUTO')
    p.text('SOURCE', 47, 134, 8)
    p.component('DIR', 'selector', 88, 98, 46, 22, 'UP')
    p.text('DIR', 111, 134, 8)
    p.component('UNSELECTED', 'selector', 140, 98, 50, 22, 'ZERO')
    p.text('UNSEL', 165, 134, 8)
    p.component('GLITCH', 'button', 212, 109)
    p.text('GLITCH', 212, 134, 7)
    for name, x in [('RATE', 34), ('LENGTH', 91), ('SEED', 148)]:
        p.component(name, 'knob', x, 158)
        p.text(name, x, 188)
    p.text('Hz', 34, 198, 6, DIM)
    p.component('LOAD', 'button', 198, 158)
    p.text('LOAD', 198, 188)
    p.component('RESET_LIGHT', 'light', 219, 158)
    p.rule(205)
    for name, x, label in [('CLOCK_IN', 34, 'CLOCK'), ('RESET_IN', 91, 'RESET'), ('DIR_IN', 148, 'DIR'), ('BUS_IN', 205, 'BUS IN')]:
        p.jack(name, x, 234, label)
    for name, x, label in [('COM_IN', 34, 'COM IN'), ('IN_A', 91, 'IN 1-16'), ('IN_B', 148, 'IN 17-32')]:
        p.jack(name, x, 276, label)
    p.block(12, 296, 216, 78)
    for name, x, label in [('BUS_OUT', 34, 'BUS OUT'), ('CLOCK_OUT', 91, 'CLK OUT'), ('EOC_OUT', 148, 'EOC')]:
        p.jack(name, x, 318, label, True)
    p.component('CLOCK_LIGHT', 'light', 109, 306)
    p.component('EOC_LIGHT', 'light', 166, 306)
    for name, x, label in [('COM_OUT', 34, 'COM OUT'), ('OUT_A', 91, 'OUT 1-16'), ('OUT_B', 148, 'OUT 17-32')]:
        p.jack(name, x, 358, label, True)
    p.text('16 CH', 205, 352, 6, DIM)
    p.text('EACH', 205, 361, 6, DIM)
    return p


def lattice(font):
    p = Panel(390, font)
    p.title('Lattice', '4 X 4 GRID / TWO COUNTERS / CROSS-COUPLED JUMPS', 11)
    for row in range(4):
        for col in range(4):
            step = 4 * row + col
            x, y = 34 + 46 * col, 74 + 44 * row
            p.component(f'STEP{step}', 'small_knob', x, y)
            p.component(f'STEP{step}_LIGHT', 'light', x + 17, y - 11)
            p.text(str(step + 1), x, y + 22, 6, DIM)
    p.component('STATE', 'readout', 214, 56, 52, 24, '00')
    p.text('V H', 240, 90, 6, DIM)
    p.component('JUMP', 'selector', 278, 56, 96, 22, 'FOLLOW')
    p.text('JUMP', 326, 92, 8)
    p.component('SOURCE', 'selector', 214, 104, 78, 22, 'AUTO')
    p.text('H SOURCE', 253, 140, 7)
    p.component('RANGE', 'selector', 300, 104, 74, 22, '0-10 V')
    p.text('CV RANGE', 337, 140, 7)
    for name, x, label in [('RATE', 232, 'RATE'), ('SEED', 284, 'SEED')]:
        p.component(name, 'knob', x, 168)
        p.text(label, x, 198)
    p.text('Hz', 232, 208, 6, DIM)
    p.component('LOAD', 'button', 334, 168)
    p.text('LOAD', 334, 198)
    p.component('RESET_LIGHT', 'light', 356, 168)
    for name, x, label in [('SLEW_A', 232, 'SLEW A'), ('SLEW_B', 284, 'SLEW B')]:
        p.component(name, 'small_knob', x, 227)
        p.text(label, x, 247, 7)
    p.rule(253)
    for k, (name, label) in enumerate([('H_CLOCK_IN', 'H CLOCK'), ('V_CLOCK_IN', 'V CLOCK'), ('DIR_H_IN', 'DIR H'),
                                       ('DIR_V_IN', 'DIR V'), ('JUMP_H_IN', 'JUMP H'), ('JUMP_V_IN', 'JUMP V'),
                                       ('RESET_IN', 'RESET'), ('BUS_IN', 'BUS IN')]):
        p.jack(name, 28 + 47.7 * k, 279, label)
    p.jack('SLEW_A_CV_IN', 28, 320, 'A CV')
    p.jack('SLEW_B_CV_IN', 75.7, 320, 'B CV')
    p.block(100, 300, 280, 76)
    for k, (name, label) in enumerate([('CV_OUT', 'CV'), ('INV_OUT', 'INV'), ('SLEW_A_OUT', 'SLEW A'), ('SLEW_B_OUT', 'SLEW B')]):
        p.jack(name, 123.4 + 47.7 * k, 320, label, True)
    for k, (name, label) in enumerate([('BUS_OUT', 'BUS OUT'), ('CLOCK_OUT', 'CLK OUT'), ('STEPS_A_OUT', 'STEPS 1-8'), ('STEPS_B_OUT', 'STEPS 9-16')]):
        p.jack(name, 123.4 + 47.7 * k, 360, label, True)
    p.component('CLOCK_LIGHT', 'light', 189, 348)
    p.text('V = ROW, H = COLUMN', 52, 368, 6, DIM)
    return p


def parallel_sum(font):
    p = Panel(300, font)
    p.title('Parallel Sum', '8-STAGE SHIFT REGISTER / WEIGHTED SUM', 10)
    p.component('WORD', 'readout', 12, 56, 56, 24, 'B8')
    p.text('WORD', 40, 92, 7)
    p.component('MASK_DISPLAY', 'readout', 76, 56, 56, 24, 'B8')
    p.text('MASK', 104, 92, 7)
    p.component('SOURCE', 'selector', 140, 57, 82, 22, 'AUTO')
    p.text('SOURCE', 181, 92, 7)
    p.component('SENSE', 'selector', 228, 57, 60, 22, 'XNOR')
    p.text('SENSE', 258, 92, 7)
    for name, x, label in [('RATE', 34, 'RATE'), ('MASK', 88, 'MASK'), ('THRESHOLD', 142, 'THRESH'), ('SEED', 196, 'SEED')]:
        p.component(name, 'knob', x, 124)
        p.text(label, x, 154)
    p.text('Hz', 34, 164, 6, DIM)
    p.text('INJECT V', 142, 164, 6, DIM)
    p.component('LOAD', 'button', 238, 124)
    p.text('LOAD', 238, 154)
    p.component('RESET_LIGHT', 'light', 254, 112)
    p.component('HOT', 'button', 276, 124)
    p.text('HOT', 276, 154)
    p.text('STAGE WEIGHTS  (1 = NEWEST)', 150, 178, 6, DIM)
    for k in range(8):
        x = 27.5 + 35 * k
        p.component(f'STAGE{k}_LIGHT', 'light', x, 190)
        p.component(f'W{k}', 'small_knob', x, 211)
        p.text(str(k + 1), x, 236, 7)
    p.rule(247)
    for name, x, label in [('CLOCK_IN', 34, 'CLOCK'), ('RESET_IN', 92, 'RESET'), ('INJECT_IN', 150, 'INJECT'),
                           ('MASK_IN', 208, 'MASK BUS'), ('W_CV_IN', 266, 'W CV')]:
        p.jack(name, x, 278, label)
    p.component('INJECT_LIGHT', 'light', 168, 264)
    p.text('POLY 1-8', 266, 299, 6, DIM)
    p.block(12, 306, 276, 66)
    for name, x, label in [('BUS_OUT', 34, 'BUS OUT'), ('CLOCK_OUT', 92, 'CLK OUT'), ('GATE_OUT', 150, 'GATE'),
                           ('CV_OUT', 208, 'CV'), ('AUDIO_OUT', 266, 'AUDIO')]:
        p.jack(name, x, 344, label, True)
    p.component('CLOCK_LIGHT', 'light', 110, 330)
    return p


def wheel(font):
    p = Panel(210, font)
    p.title('Wheel', '24-STAGE LFSR / LOOP / CAPTURE', 11)
    p.component('LOOP_DISPLAY', 'readout', 12, 56, 52, 24, '00')
    p.text('LOOP', 38, 92, 7)
    for k in range(8):
        x = 184 - 16 * k
        p.component(f'BIT{k}_LIGHT', 'light', x, 64)
    p.text('LOOP BITS 8 ... 1', 128, 82, 6, DIM)
    p.component('SOURCE', 'selector', 12, 100, 76, 22, 'AUTO')
    p.text('SOURCE', 50, 134, 7)
    p.component('STRIDE', 'selector', 96, 100, 40, 22, '1')
    p.text('STRIDE', 116, 134, 7)
    p.component('LOAD', 'button', 156, 111)
    p.text('LOAD', 156, 134, 7)
    p.component('RESET_LIGHT', 'light', 156, 96)
    p.component('CAPTURE', 'button', 188, 111)
    p.text('CAPTURE', 188, 134, 7)
    p.component('ARMED_LIGHT', 'light', 188, 96)
    for name, x, label in [('RATE', 30, 'RATE'), ('LENGTH', 80, 'LENGTH'), ('OFFSET', 130, 'OFFSET'), ('SEED', 180, 'SEED')]:
        p.component(name, 'knob', x, 160)
        p.text(label, x, 190, 7)
    p.text('Hz', 30, 199, 6, DIM)
    p.rule(207)
    for name, x, label in [('CLOCK_IN', 35, 'CLOCK'), ('RESET_IN', 81.5, 'RESET'), ('CAPTURE_IN', 128, 'CAPTURE')]:
        p.jack(name, x, 236, label)
    p.block(12, 256, 186, 118)
    for name, x, label in [('LOOP_OUT', 35, 'LOOP BUS'), ('FREE_OUT', 81.5, 'FREE BUS'), ('CLOCK_OUT', 128, 'CLK OUT'), ('U_OUT', 174.5, 'U')]:
        p.jack(name, x, 292, label, True)
    p.component('CLOCK_LIGHT', 'light', 146, 280)
    for name, x, label in [('G_OUT', 35, 'G'), ('LOOP_GATE_OUT', 81.5, 'LOOP GATE'), ('NOISE_GATE_OUT', 128, 'NOISE')]:
        p.jack(name, x, 350, label, True)
    return p


def roulette(font):
    p = Panel(210, font)
    p.title('Roulette', 'PLL / 12-BIT DIVIDER / SPIN AND COAST', 6)
    p.component('FREQUENCY', 'readout', 12, 56, 90, 24, 'REST')
    p.text('VCO HZ', 57, 92, 7)
    p.component('SOURCE', 'selector', 110, 57, 88, 22, 'AUTO')
    p.text('SOURCE', 154, 92, 7)
    for k in range(12):
        p.component(f'BIT{k}_LIGHT', 'light', 22 + 15 * k, 104)
    p.text('/2', 22, 117, 6, DIM)
    p.text('BITS', 105, 117, 6, DIM)
    p.text('/4096', 187, 117, 6, DIM)
    p.component('COMPARATOR', 'selector', 12, 126, 88, 22, 'XOR')
    p.text('COMPARATOR', 56, 160, 7)
    p.component('SCALE', 'selector', 110, 126, 88, 22, 'X1')
    p.text('SCALE', 154, 160, 7)
    p.component('RANGE', 'knob', 55, 190)
    p.text('RANGE', 55, 220, 7)
    p.text('VCO MAX HZ', 55, 229, 6, DIM)
    p.component('FILTER', 'knob', 155, 190)
    p.text('FILTER', 155, 220, 7)
    p.text('FRICTION', 155, 229, 6, DIM)
    p.rule(238)
    for name, x, label in [('TRACK_IN', 27, 'TRACK'), ('ROULETTE_IN', 66, 'ROULETTE'), ('DIVIDE_IN', 105, 'DIVIDE'),
                           ('RANGE_CV_IN', 144, 'RANGE CV'), ('RESET_IN', 183, 'RESET')]:
        p.component(name, 'input', x, 264)
        p.text(label, x, 246, 6.5)
    p.block(12, 284, 186, 90)
    for name, x, label in [('LO_OUT', 35, 'LO BUS'), ('HI_OUT', 81.5, 'HI BUS'), ('RESULT_OUT', 128, 'RESULT'), ('VCO_OUT', 174.5, 'VCO')]:
        p.jack(name, x, 312, label, True)
    p.component('RESULT_LIGHT', 'light', 146, 300)
    p.jack('CLOCK_OUT', 35, 356, 'CLK OUT', True)
    p.component('CLOCK_LIGHT', 'light', 53, 344)
    p.text('LO: /2 ... /256', 128, 352, 6, DIM)
    p.text('HI: /32 ... /4096', 128, 362, 6, DIM)
    return p


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', default=DEFAULT_FONT)
    args = parser.parse_args()
    panels = {'reg': register(args.font), 'lookup': lookup(args.font), 'converter': converter(args.font),
              'counter': counter(args.font), 'byte_switch': byte_switch(args.font),
              'pipeline': pipeline(args.font), 'lookup2': lookup2(args.font),
              'counter2': counter2(args.font), 'fan': fan(args.font), 'lattice': lattice(args.font),
              'parallel_sum': parallel_sum(args.font), 'wheel': wheel(args.font),
              'roulette': roulette(args.font)}
    for name, filename in [('reg', 'Register'), ('lookup', 'Lookup'), ('converter', 'Converter'), ('counter', 'Counter'),
                           ('byte_switch', 'Switch'), ('pipeline', 'Pipeline'),
                           ('lookup2', 'Lookup2'), ('counter2', 'Counter2'), ('fan', 'Fan'), ('lattice', 'Lattice'),
                           ('parallel_sum', 'ParallelSum'), ('wheel', 'Wheel'), ('roulette', 'Roulette')]:
        panels[name].write(ROOT, filename)
    write_layout(ROOT / 'src/Layout.hpp', 'bytebus_layout', panels)
    print('Wrote thirteen ByteBusLab panels, previews and src/Layout.hpp')


if __name__ == '__main__':
    main()
