#!/usr/bin/env python3
"""Generate outlined CZishLab panels and their matching Rack coordinates."""
from pathlib import Path
import argparse
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / 'shared' / 'panels'))
from panel import Panel, DEFAULT_FONT, ACCENT, DIM, write_layout


def dac(font):
    p = Panel(240, font)
    p.title('DAC Stage', 'YM / CZ MODEL / LINEAR', 5, 17)
    p.component('PROFILE', 'selector', 16, 64, 108, 23, 'YM FP')
    p.text('PROFILE', 70, 103)
    p.component('BITS', 'knob', 187, 75)
    p.text('LINEAR BITS', 187, 103, 7)
    for name, x, label in [('RATE', 45, 'RATE'), ('DRIVE', 120, 'DRIVE'), ('TRACK', 195, 'CZ TRACK')]:
        p.component(name, 'knob', x, 139)
        p.text(label, x, 168, 7)
    p.component('LEVEL', 'knob', 45, 202)
    p.text('LEVEL', 45, 231)
    p.component('TONE', 'knob', 120, 202)
    p.text('TONE', 120, 231)
    p.component('WORD', 'readout', 166, 192, 58, 23, '00 / 3')
    p.text('BUS / DIV', 195, 228, 6, DIM)
    p.rule(238)
    for name, x, label in [('SIGNAL', 30, 'IN'), ('RATE_CV', 90, 'RATE CV'),
                           ('DRIVE_CV', 150, 'DRIVE CV'), ('XOR_BUS', 210, 'XOR BUS')]:
        p.jack(name, x, 275, label)
    p.block(42, 301, 156, 53)
    p.jack('AUDIO', 75, 332, 'OUT', True)
    p.jack('BUS_OUT', 165, 332, 'BUS OUT', True)
    p.component('AA', 'switch', 24, 332)
    p.text('AA', 24, 307, 7, ACCENT)
    p.component('RECON', 'switch', 216, 332)
    p.text('CLEAN', 216, 307, 6, ACCENT)
    p.text('HOLD', 216, 358, 6, DIM)
    p.text('ENCODED HIGH BYTE / CLOCK DIVIDED', 120, 376, 6, DIM)
    return p


def voice(font):
    p = Panel(240, font)
    p.title('CZish', 'uPD933 / SINGLE LINE / EARLY', 7, 19)
    p.component('WAVE', 'selector', 16, 64, 100, 23, 'SAW')
    p.component('WINDOW', 'selector', 128, 64, 96, 23, 'NONE')
    p.text('WAVE', 66, 103)
    p.text('WINDOW', 176, 103)
    for name, x, label in [('SHAPE', 45, 'DCW'), ('ATTACK', 120, 'ATTACK'), ('RELEASE', 195, 'RELEASE')]:
        p.component(name, 'knob', x, 139)
        p.text(label, x, 168, 7)
    p.component('LEVEL', 'knob', 45, 202)
    p.text('LEVEL', 45, 231)
    p.component('TRACK', 'knob', 120, 202)
    p.text('DAC TRACK', 120, 231, 7)
    p.component('WORD', 'readout', 166, 192, 58, 23, '00 / 2')
    p.text('BUS / DIV', 195, 228, 6, DIM)
    p.rule(238)
    for name, x, label in [('VOCT', 30, 'V/OCT'), ('GATE', 90, 'GATE'),
                           ('DCW_CV', 150, 'DCW CV'), ('XOR_BUS', 210, 'XOR BUS')]:
        p.jack(name, x, 275, label)
    p.block(42, 301, 156, 53)
    p.jack('AUDIO', 75, 332, 'OUT', True)
    p.jack('BUS_OUT', 165, 332, 'BUS OUT', True)
    p.component('RECON', 'switch', 24, 332)
    p.text('CLEAN', 24, 307, 6, ACCENT)
    p.text('HOLD', 24, 358, 6, DIM)
    p.text('8 VOICES / PROVISIONAL DAC MODEL', 120, 376, 6, DIM)
    return p


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--font', default=DEFAULT_FONT)
    args = parser.parse_args()
    panels = {'dac': dac(args.font), 'voice': voice(args.font)}
    for key, name in [('dac', 'DacStage'), ('voice', 'Czish')]:
        panels[key].write(ROOT, name)
    write_layout(ROOT / 'src/Layout.hpp', 'czish_layout', panels)
    print('Wrote CZish and DAC Stage panels, previews and coordinates')


if __name__ == '__main__':
    main()
