"""Build outlined Rack panels, matching C++ coordinates and layout illustrations.

The palette follows Spoon Bender and Séance. Generated SVGs need no installed
fonts in Rack: NanoSVG does not support <text>. Requires fontTools only.
"""
from html import escape
from pathlib import Path
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.ttLib import TTFont

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'glyphs'))
from glyph import glyph

BG, EDGE, RULE, BLOCK = '#161d22', '#5c6d74', '#41545c', '#0e1417'
LABEL, DIM, ACCENT, OUT = '#d9e2e5', '#7f949c', '#f0b46a', '#8fdcc4'
DEFAULT_FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'


class Panel:
    def __init__(self, width, font=DEFAULT_FONT):
        self.width = width
        self.font = TTFont(font)
        self.glyphs = self.font.getGlyphSet()
        self.cmap = self.font.getBestCmap()
        self.units = self.font['head'].unitsPerEm
        self.parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="380" viewBox="0 0 {width} 380">',
                      f'<rect width="{width}" height="380" fill="{BG}"/>',
                      f'<rect x="1" y="1" width="{width-2}" height="378" fill="none" stroke="{EDGE}"/>']
        self.components = {}

    def text(self, label, x, y, size=8, color=LABEL, anchor='middle'):
        scale = size / self.units
        advances = [self.font['hmtx'][self.cmap[ord(c)]][0] for c in label]
        width = sum(advances) * scale
        if anchor == 'middle':
            x -= width / 2
        elif anchor == 'end':
            x -= width
        # aria-label is also useful when inspecting a generated SVG's source.
        self.parts.append(f'<g aria-label="{escape(label, quote=True)}">')
        for char, advance in zip(label, advances):
            pen = SVGPathPen(self.glyphs)
            self.glyphs[self.cmap[ord(char)]].draw(pen)
            self.parts.append(f'<path d="{pen.getCommands()}" transform="translate({x:.4f},{y}) scale({scale:.8f},-{scale:.8f})" fill="{color}"/>')
            x += advance * scale
        self.parts.append('</g>')

    def rule(self, y):
        self.parts.append(f'<path d="M12 {y}H{self.width-12}" stroke="{RULE}"/>')

    def block(self, x, y, width, height):
        self.parts.append(f'<rect x="{x}" y="{y}" width="{width}" height="{height}" rx="4" fill="{BLOCK}"/>')

    def title(self, title, subtitle, mark, size=19, mark_x=None):
        self.text(title, 18, 28, size, OUT, 'start')
        self.text(subtitle, 18, 42, 7, DIM, 'start')
        self.parts.append(glyph(mark, self.width-26 if mark_x is None else mark_x, 24, 26, LABEL, ACCENT))
        self.rule(49)

    def component(self, name, kind, x, y, width=0, height=0, preview=''):
        """Coordinates are centres, except selector/readout areas (top left)."""
        if name in self.components:
            raise ValueError(f'Duplicate component {name}')
        self.components[name] = (kind, x, y, width, height, preview)

    def jack(self, name, x, y, label, output=False):
        self.component(name, 'output' if output else 'input', x, y)
        self.text(label, x, y-18, 7, OUT if output else LABEL)

    def layout(self, namespace):
        lines = [f'namespace {namespace} {{', f'constexpr float WIDTH = {self.width};']
        for name, (_, x, y, width, height, _) in self.components.items():
            lines.append(f'constexpr float {name}_X = {x}, {name}_Y = {y};')
            if width:
                lines.append(f'constexpr float {name}_W = {width}, {name}_H = {height};')
        return '\n'.join(lines + [f'}} // namespace {namespace}', ''])

    def svg(self, preview=False):
        parts = self.parts.copy()
        if preview:
            # Neutral component representations, deliberately not a Rack screenshot.
            for kind, x, y, width, height, label in self.components.values():
                if kind in ('input', 'output'):
                    fill = '#687d85' if kind == 'output' else '#bdc4c5'
                    parts += [f'<circle cx="{x}" cy="{y}" r="11.8" fill="{fill}"/>',
                              f'<circle cx="{x}" cy="{y}" r="7" fill="#0b1013"/>']
                elif kind in ('knob', 'small_knob'):
                    r = 15 if kind == 'knob' else 11
                    parts += [f'<circle cx="{x}" cy="{y}" r="{r}" fill="#0c1114" stroke="#84949d"/>',
                              f'<path d="M{x} {y-r+3}v6" stroke="{LABEL}" stroke-width="2"/>']
                elif kind == 'button':
                    parts.append(f'<circle cx="{x}" cy="{y}" r="9" fill="#777d80" stroke="#a7b5bb"/>')
                elif kind == 'switch':
                    parts += [f'<rect x="{x-4}" y="{y-9}" width="8" height="18" rx="2" fill="#84949d"/>',
                              f'<circle cx="{x}" cy="{y+4}" r="4" fill="#c3cdce"/>']
                elif kind == 'light':
                    parts.append(f'<circle cx="{x}" cy="{y}" r="2" fill="{OUT}"/>')
                elif kind in ('selector', 'readout'):
                    parts.append(f'<rect x="{x}" y="{y}" width="{width}" height="{height}" rx="3" fill="#1d272d"/>')
                    saved = self.parts
                    self.parts = parts
                    self.text(label, x+width/2, y+height/2+3, 9, OUT)
                    self.parts = saved
        return '\n'.join(parts + ['</svg>', ''])

    def write(self, root, filename):
        (root / 'res').mkdir(exist_ok=True)
        (root / 'docs').mkdir(exist_ok=True)
        (root / f'res/{filename}.svg').write_text(self.svg())
        (root / f'docs/{filename}-preview.svg').write_text(self.svg(preview=True))


def write_layout(path, namespace, panels):
    text = ['// Generated with res/*.svg by the panel generator; do not edit.',
            '#pragma once', f'namespace {namespace} {{']
    text += [panel.layout(name) for name, panel in panels.items()]
    text += [f'}} // namespace {namespace}', '']
    path.write_text('\n'.join(text))
