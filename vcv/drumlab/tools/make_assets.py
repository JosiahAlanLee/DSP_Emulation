"""Regenerate the Rack panel (fontTools; DejaVu Sans, license in docs/).

NanoSVG does not render text. All panel labels are emitted as font outlines.
"""
from pathlib import Path
import argparse
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / 'shared' / 'panels'))
from panel import Panel, DEFAULT_FONT, ACCENT, DIM, write_layout


def panel(font_path):
    p = Panel(300, font_path)
    p.title('DrumLab', 'PERCUSSION / 16 VOICES', 10, 20, mark_x=212)
    p.component('TAP', 'button', 272, 24)
    p.component('HIT', 'light', 244, 24)
    p.text('TAP', 272, 43, 6, DIM)
    p.text('HIT', 244, 43, 6, DIM)
    p.component('MACHINE', 'selector', 16, 74, 106, 24, '4-LANE KIT')
    p.text('MACHINE', 69, 116)
    for name, x in [('TUNE', 171), ('DECAY', 256)]:
        p.component(name, 'knob', x, 86)
        p.text(name, x, 116)
    for name, x in [('COLOR', 35), ('CONTOUR', 92), ('DRIVE', 150), ('BITS', 208), ('RATE', 265)]:
        p.component(name, 'knob', x, 156)
        p.text(name, x, 186, 7.5)
    p.rule(196)
    p.text('HIT CV', 18, 210, 7, ACCENT, 'start')
    p.text('CAPTURED AT EACH TRIGGER', 282, 210, 6, DIM, 'end')
    x = [35, 112, 188, 265]
    inputs = ['TRIG', 'PITCH', 'VEL', 'MACHINE_CV', 'DECAY_CV', 'COLOR_CV', 'CONTOUR_CV',
              'DRIVE_CV', 'BITS_CV', 'RATE_CV', 'CHOKE', 'RESET']
    labels = ['TRIG', 'V/OCT', 'VEL', 'MACHINE', 'DECAY', 'COLOR', 'CONTOUR', 'DRIVE', 'BITS', 'RATE', 'CHOKE', 'RESET']
    for i, (name, label) in enumerate(zip(inputs, labels)):
        p.jack(name, x[i % 4], 239 + 40*(i//4), label)
    p.rule(334)
    p.block(46, 338, 208, 36)
    for i, label in enumerate(['POLY', 'LEFT', 'RIGHT']):
        p.jack(label, 70 + 80*i, 360, label, True)
    return p


def example():
    import json
    def module(ident, plugin, model, version, pos, params=(), data=None):
        result = dict(id=ident, plugin=plugin, model=model, version=version, pos=pos,
                      params=[dict(id=i, value=value) for i, value in enumerate(params)])
        if data is not None:
            result['data'] = data
        return result
    modules = [module(1, 'GorgonBridge', 'Gorgon', '2.2.0', [0, 0]),
               module(2, 'DrumLab', 'Percussion', '2.0.0', [24, 0], [6, 0, -1.51457317, .5, .5, .15, 16, 0, 0], {'velocityHigh': 10}),
               module(3, 'Core', 'AudioInterface2', '2.6.4', [44, 0])]
    # Gorgon gates, pitch and velocity are already polyphonic four-channel ports.
    connections = [(1, 11, 2, 0), (1, 12, 2, 1), (1, 3, 2, 2), (1, 1, 2, 11), (2, 1, 3, 0), (2, 2, 3, 1)]
    cables = [dict(id=i + 1, outputModuleId=a, outputId=b, inputModuleId=c, inputId=d, color='#4f6f7a')
              for i, (a, b, c, d) in enumerate(connections)]
    return json.dumps(dict(version='2.6.4', modules=modules, cables=cables), indent=2) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', default=DEFAULT_FONT)
    args = parser.parse_args()
    (ROOT / 'res').mkdir(exist_ok=True)
    (ROOT / 'examples').mkdir(exist_ok=True)
    p = panel(args.font)
    p.write(ROOT, 'Percussion')
    write_layout(ROOT / 'src/Layout.hpp', 'drumlab_layout', {'percussion': p})
    (ROOT / 'examples/Gorgon-percussion.vcv').write_text(example())
