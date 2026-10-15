#!/usr/bin/env python3
"""Outlined panel and a layout preview (not a Rack screenshot). Requires fontTools."""
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
root=Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'shared' / 'glyphs'))
from glyph import glyph  # the twelve marks (Clavicula Matricis, Tabula XIII)
font=TTFont('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
glyphs=font.getGlyphSet(); cmap=font.getBestCmap(); units=font['head'].unitsPerEm
def text(s,x,y,size=9,color='#d9e2e5',center=True):
    advances=[font['hmtx'][cmap[ord(c)]][0] for c in s]; scale=size/units
    if center: x-=sum(advances)*scale/2
    paths=[]
    for c,a in zip(s,advances):
        pen=SVGPathPen(glyphs); glyphs[cmap[ord(c)]].draw(pen)
        paths.append(f'<path d="{pen.getCommands()}" transform="translate({x:.3f},{y}) scale({scale},-{scale})" fill="{color}"/>'); x+=a*scale
    return ''.join(paths)
W=840  # 56 HP: six VAST columns, then AUX CV; FUN 3/4 extend the FUN block
a=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="380" viewBox="0 0 {W} 380">', f'<rect width="{W}" height="380" fill="#151e24"/>', f'<rect x="1" y="1" width="{W-2}" height="378" fill="none" stroke="#607077"/>']
a += [text('VASTISH',20,28,20,'#87e1c8',False),text('NATIVE / 8 VOICES',420,369,9,'#7b929c'),text('MASTER',697,23,7)]
a += [text('PW',103,81,8)]
for c,n in enumerate(['PITCH','F1','F2','F3','AMP','PAN']):
    x=c*120
    a += [f'<path d="M{x+4} 36H{x+116}M{x+4} 234H{x+116}" stroke="#4b6470"/>',text(n,x+60,47,11,'#f0c28b')]
    if c in [1,2,3]: a += [text('B',x+103,81,8)]
    a += [text('ADJUST',x+60,124),text('KEY',x+30,164,8),text('VEL',x+90,164,8),text('SOURCE 1',x+43,171,7,'#7b929c'),text('DEPTH',x+101,205,7),text('SOURCE 2',x+43,206,7,'#7b929c'),text('DEPTH',x+101,239,7)]
for e,n in enumerate(['AMP ENV','ENV 2','ENV 3']):
    a += [text(n,e*120+60,254,10,'#f0c28b')]
    for k,label in enumerate('ADSR'): a += [text(label,e*120+24+24*k,303,8)]
for l in range(2):
    x=(l+3)*120
    a += [text('LFO '+str(l+1),x+60,254,10,'#f0c28b'),text('RATE',x+27,303,8),text('SHAPE',x+82,303,8)]
a += [text(f'FUN {f+1}',633+50*f,246,9,'#f0c28b') for f in range(4)]
a += ['<path d="M724 36H836M724 234H836" stroke="#4b6470"/>', text('AUX CV',780,47,11,'#f0c28b'), text('POLY  ±10 V',780,60,6,'#7b929c')]
a += [text(f'AUX {k+1}',780,92+40*k-15,8) for k in range(4)]
for row,label in enumerate(['A','B','EQ']): a += [text(label,604,265+24*row,6)]
for i,label in enumerate(['V/OCT','GATE','VEL','MOD']): a += [text(label,30+48*i,331,8)]
for i,label in enumerate(['L','R','AMP','ENV2','ENV3','LFO1','LFO2','FUN1','FUN2','FUN3','FUN4']): a += [text(label,245+40*i,331,7,'#87e1c8')]
a.append(glyph(10,618,15,32,'#d9e2e5','#87e1c8'))  # X Generative substrate: conditions beneath
panel=''.join(a)+'</svg>'
(root/'res/Vastish.svg').write_text(panel)
# Match widget positions using neutral representations of knobs, sockets and selectors.
b=a.copy()
def knob(x,y):
    b.append(f'<circle cx="{x}" cy="{y}" r="10" fill="#0c1114" stroke="#84949d"/><path d="M{x} {y-8}v5" stroke="#d9e2e5" stroke-width="2"/>')
def selector(x,y,w,label):
    b.append(f'<rect x="{x}" y="{y}" width="{w}" height="20" rx="3" fill="#1d272d"/>'); b.append(text(label,x+w/2,y+13,9,'#87e1c8'))
selector(200,8,174,'Ladder Bass'); selector(386,8,194,'1 / SERIAL'); knob(660,20)
selector(15,52,77,'SAW'); knob(103,62)
for c,label in enumerate(['LP4','DIST','GAIN'],1): selector(c*120+15,52,76,label); knob(c*120+103,62)
for c in range(6):
    x=c*120
    for xx,yy in [(60,103),(30,143),(90,143),(101,183),(101,218)]: knob(x+xx,yy)
    selector(x+10,173,70,'ENV2' if c==1 else 'OFF'); selector(x+10,208,70,'OFF')
for e in range(3):
    for k in range(4): knob(e*120+24+24*k,281)
for l in range(2): knob((l+3)*120+27,281); selector((l+3)*120+49,271,65,'COSINE')
for f in range(4):
    for k,label in enumerate(['OFF','OFF','A+B']): selector(610+50*f,252+24*k,47,label)
for x,y in [(30+48*i,348) for i in range(4)]+[(245+40*i,348) for i in range(11)]+[(780,92+40*k) for k in range(4)]:
    b.append(f'<circle cx="{x}" cy="{y}" r="11" fill="#bdc4c5"/><circle cx="{x}" cy="{y}" r="7" fill="#0b1013"/>')
(root/'docs/panel-preview.svg').write_text(''.join(b)+'</svg>')
print('Created outlined panel and layout preview')
