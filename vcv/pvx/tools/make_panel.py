#!/usr/bin/env python3
"""Generate PVX panel with outlined labels supported by Rack's SVG renderer."""
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
font=TTFont('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
glyphs=font.getGlyphSet();cmap=font.getBestCmap();units=font['head'].unitsPerEm

def text(label,x,y,size=8,color='#e9e7df'):
    scale=size/units;x-=sum(font['hmtx'][cmap[ord(c)]][0] for c in label)*scale/2;paths=[]
    for c in label:
        glyph=cmap[ord(c)];pen=SVGPathPen(glyphs);glyphs[glyph].draw(pen)
        paths.append(f'<path d="{pen.getCommands()}" transform="translate({x:.3f},{y}) scale({scale},-{scale})" fill="{color}"/>')
        x+=font['hmtx'][glyph][0]*scale
    return ''.join(paths)
labels=['WAVE 1','PW 1','LEVEL 1','WAVE 2','PW 2','LEVEL 2','DETUNE','SYNC','NOISE','CUTOFF','RESONANCE','FC DRIVE','FILTER ENV','LP / BP','DRIVE','AR / ADSR','LEVEL','1x / 2x / 4x','ENV 1 A','ENV 1 D','ENV 1 S','ENV 1 R','ENV 2 A','ENV 2 D','ENV 2 S','ENV 2 R']
svg=['<svg xmlns="http://www.w3.org/2000/svg" width="360" height="380" viewBox="0 0 360 380">','<rect width="360" height="380" fill="#22262a"/>','<path d="M10 23H350 M10 292H350 M10 338H350" stroke="#f28b39"/>',text('PVX',180,17,15,'#f28b39')]
xs=[];ys=[]
for i,label in enumerate(labels):
    x=30+(i%6)*60 if i<18 else 55+((i-18)%4)*83
    y=55+(i//6)*50 if i<18 else 215+((i-18)//4)*50
    xs.append(x);ys.append(y)
    svg.append(text(label if i<18 else ['ATTACK','DECAY','SUSTAIN','RELEASE'][(i-18)%4],x,y-17))
svg.append(text('ENVELOPE 1 / FILTER',180,185,8,'#f2c29d'))
svg.append(text('ENVELOPE 2 / AMPLIFIER',180,238,8,'#f2c29d'))
(Path(__file__).resolve().parents[1]/'src/Layout.hpp').write_text('// Generated with the SVG by tools/make_panel.py.\n#pragma once\nnamespace pvx_layout {\nconstexpr float x[]={'+','.join(map(str,xs))+'};\nconstexpr float y[]={'+','.join(map(str,ys))+'};\n}\n')
for y,words in [(301,['V/OCT','GATE','AUDIO IN','FC CV','RES CV']),(346,['VOICE','LOW PASS','BAND PASS','ENV 1','ENV 2'])]:
    for i,label in enumerate(words):svg.append(text(label,36+i*72,y,8,'#f2c29d'))
svg.append('</svg>')
(Path(__file__).resolve().parents[1]/'res/PVX.svg').write_text('\n'.join(svg)+'\n')
