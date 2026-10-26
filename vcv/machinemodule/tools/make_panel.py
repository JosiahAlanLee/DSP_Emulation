"""Outline panel labels for Rack's NanoSVG (fontTools, DejaVu Sans)."""
from pathlib import Path
import argparse
import xml.etree.ElementTree as ET
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen

ROOT = Path(__file__).resolve().parents[1]
NS = 'http://www.w3.org/2000/svg'
ET.register_namespace('', NS)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--font', default='/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
    args = parser.parse_args()
    font = TTFont(args.font)
    glyphs, cmap, em = font.getGlyphSet(), font.getBestCmap(), font['head'].unitsPerEm
    tree = ET.parse(ROOT/'tools/panel-source.svg')
    for parent in tree.getroot().iter():
        for text in list(parent):
            if text.tag != f'{{{NS}}}text':
                continue
            attrs = dict(parent.attrib, **text.attrib)
            scale = float(attrs.get('font-size', 2.3))/em
            label = text.text or ''
            width = sum(glyphs[cmap[ord(c)]].width for c in label)*scale
            cursor, y = float(attrs.get('x', 0))-width/2, float(attrs['y'])
            group = ET.Element(f'{{{NS}}}g')
            for c in label:
                glyph = glyphs[cmap[ord(c)]]
                pen = SVGPathPen(glyphs); glyph.draw(pen)
                ET.SubElement(group, f'{{{NS}}}path', {'fill':attrs.get('fill', '#e4e7db'),
                    'transform':f'translate({cursor:.5f} {y}) scale({scale:.9f} {-scale:.9f})', 'd':pen.getCommands()})
                cursor += glyph.width*scale
            parent.insert(list(parent).index(text), group); parent.remove(text)
    tree.write(ROOT/'res/Machinemodule.svg', encoding='unicode')


if __name__ == '__main__':
    main()
