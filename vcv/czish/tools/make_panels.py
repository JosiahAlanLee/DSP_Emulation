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
    for name, x, label in [('RATE',45,'RATE'),('DRIVE',120,'DRIVE'),('TRACK',195,'CZ TRACK')]:
        p.component(name,'knob',x,139);p.text(label,x,168,7)
    for name, x, label in [('LEVEL',45,'LEVEL'),('TONE',120,'TONE'),('NOISE',195,'NOISE')]:
        p.component(name,'knob',x,202);p.text(label,x,231,7)
    p.component('WORD','readout',16,240,78,20,'00 / 3')
    p.component('RATE_READ','readout',104,240,120,20,'55930 Hz / 12 BIT')
    for name,x,label in [('SIGNAL',24,'IN'),('RATE_CV',72,'RATE CV'),('DRIVE_CV',120,'DRIVE CV'),('XOR_BUS',168,'XOR BUS'),('BITS_CV',216,'BITS CV')]:
        p.jack(name,x,286,label)
    p.block(42,317,156,38)
    p.jack('AUDIO',75,341,'OUT',True);p.jack('BUS_OUT',165,341,'BUS OUT',True)
    p.component('AA','switch',24,341);p.text('AA',24,320,7,ACCENT)
    p.component('RECON','switch',216,341);p.text('CLEAN',216,320,6,ACCENT);p.text('HOLD',216,366,6,DIM)
    p.text('ENCODED HIGH BYTE / CLOCK DIVIDED',120,374,6,DIM)
    return p


def voice(font):
    p = Panel(450,font)
    p.title('CZish','uPD933 / TWO LINES / SIX ENVELOPES',7,19)
    for name,x,w,label in [('MODE',16,100,'1'),('MOD',128,100,'OFF'),('EDIT',240,88,'LINE 1')]:
        p.component(name,'selector',x,64,w,23,label)
    for x,label in [(66,'LINE SELECT'),(178,'MOD'),(284,'EDIT LINE')]:p.text(label,x,103,7)
    p.component('RECON','switch',350,75);p.text('CLEAN',350,56,6,ACCENT);p.text('HOLD',350,101,6,DIM)
    p.component('SHAPE','knob',410,75);p.text('DCW DEPTH',410,103,7)
    for name,x,w,label in [('WAVE',16,100,'SAW'),('SECOND',128,100,'OFF'),('WINDOW',240,88,'NONE')]:
        p.component(name,'selector',x,116,w,23,label)
    for x,label in [(66,'FIRST WAVE'),(178,'SECOND WAVE'),(284,'WINDOW')]:p.text(label,x,155,7)
    for name,x,label in [('DCA_KEY',350,'AMP KEY'),('DCW_KEY',410,'WAVE KEY')]:
        p.component(name,'knob',x,128);p.text(label,x,155,7)
    for name,x,w,label in [('ENV',16,90,'DCA 1'),('STEP',116,48,'1'),('SUSTAIN',304,62,'1'),('END',378,56,'2')]:
        p.component(name,'selector',x,174,w,23,label)
    for x,label in [(61,'ENVELOPE'),(140,'STEP'),(335,'SUSTAIN'),(406,'END')]:p.text(label,x,211,6)
    for name,x,label in [('ENV_RATE',210,'RATE'),('ENV_LEVEL',274,'LEVEL')]:
        p.component(name,'knob',x,185);p.text(label,x,212,7)
    p.component('DISPLAY','readout',16,223,328,35,'DCA 1  S1  R99 L99 SUS')
    p.component('LEVEL','knob',398,236);p.text('GAIN',398,263,7)
    for name,x,label in [('OCTAVE',35,'OCTAVE'),('DETUNE',111,'DETUNE'),('FINE',187,'FINE'),('VIB_DELAY',263,'VIB DELAY'),('VIB_RATE',339,'VIB RATE'),('VIB_DEPTH',415,'VIB DEPTH')]:
        p.component(name,'knob',x,291);p.text(label,x,319,7)
    for name,x,label in [('VOCT',24,'V/OCT'),('GATE',74,'GATE'),('DCW_CV',124,'DCW CV'),('VIB_CV',174,'VIB CV'),('BEND_CV',224,'BEND'),('XOR_BUS',274,'XOR BUS')]:
        p.jack(name,x,350,label)
    p.block(302,329,143,38)
    for name,x,label in [('AUDIO',324,'OUT'),('POLY_AUDIO',374,'POLY'),('BUS_OUT',424,'BUS OUT')]:p.jack(name,x,350,label,True)
    p.component('OVERFLOW','light',93,334)
    p.text('CZ CONTROL CURVES / ANALOG DAC: PROVISIONAL',225,375,6,DIM)
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
