"""Regenerate the Rack panel (fontTools; DejaVu Sans, license in docs/).

NanoSVG does not render text. All panel labels are emitted as font outlines.
"""
from pathlib import Path
import argparse
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen

ROOT = Path(__file__).resolve().parents[1]


def panel(font_path):
    font = TTFont(font_path)
    glyphs = font.getGlyphSet()
    cmap = font.getBestCmap()
    em = font['head'].unitsPerEm
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" width="300" height="380" viewBox="0 0 300 380">',
             '<rect width="300" height="380" fill="#e8e2d2"/>',
             '<rect x="9" y="10" width="282" height="360" rx="5" fill="none" stroke="#4f6f7a" stroke-width="1"/>']

    def text(value, x, y, size=8, fill='#161918'):
        scale = size / em
        width = sum(glyphs[cmap[ord(c)]].width for c in value) * scale
        cursor = x - width / 2
        for c in value:
            glyph = glyphs[cmap[ord(c)]]
            pen = SVGPathPen(glyphs)
            glyph.draw(pen)
            parts.append(f'<path fill="{fill}" transform="translate({cursor:.4f} {y}) scale({scale:.8f} {-scale:.8f})" d="{pen.getCommands()}"/>')
            cursor += glyph.width * scale

    text('PERCUSSION', 150, 24, 14)
    text('DRUMLAB', 150, 40, 8, '#4f6f7a')
    text('HIT', 268, 23, 6)
    x = [35, 112, 188, 265]
    labels = ['MACHINE', 'TUNE', 'DECAY', 'COLOR', 'CONTOUR', 'DRIVE', 'BITS', 'RATE']
    for i, label in enumerate(labels):
        cy = 85 if i < 4 else 155
        text(label, x[i % 4], cy - 23, 8)
        parts.append(f'<circle cx="{x[i % 4]}" cy="{cy}" r="20" fill="none" stroke="#4f6f7a" stroke-width=".6"/>')
    text('SOUND CONTROLS ARE CAPTURED AT EACH HIT', 150, 192, 6.5, '#a32b24')
    inputs = ['TRIG', 'V/OCT', 'VEL', 'MACHINE', 'DECAY', 'COLOR', 'CONTOUR', 'DRIVE', 'BITS', 'RATE', 'CHOKE', 'RESET']
    for i, label in enumerate(inputs):
        text(label, x[i % 4], 207 + 44 * (i // 4), 7.5)
    parts.append('<path d="M24 332H276" stroke="#4f6f7a" stroke-width=".7"/>')
    for i, label in enumerate(['POLY', 'LEFT', 'RIGHT']):
        text(label, 70 + 80 * i, 342, 7.5, '#a32b24')
    parts.append('</svg>')
    return '\n'.join(parts) + '\n'


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
    parser.add_argument('--font', default='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
    args = parser.parse_args()
    (ROOT / 'res').mkdir(exist_ok=True)
    (ROOT / 'examples').mkdir(exist_ok=True)
    (ROOT / 'res/Percussion.svg').write_text(panel(args.font))
    (ROOT / 'examples/Gorgon-percussion.vcv').write_text(example())
