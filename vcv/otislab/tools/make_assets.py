"""Rebuild the original demo WAV and outlined SVG panel. Requires numpy/fontTools."""
from pathlib import Path
import wave
import numpy as np
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen

root=Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'shared' / 'glyphs'))
from glyph import glyph  # the twelve marks (Clavicula Matricis, Tabula XIII)
font=TTFont('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
glyphs=font.getGlyphSet(); cmap=font.getBestCmap(); units=font['head'].unitsPerEm
def text(label,x,y,size=10,color='#d5dfde',center=True):
    advances=[font['hmtx'][cmap[ord(c)]][0] for c in label]
    scale=size/units; width=sum(advances)*scale
    if center: x-=width/2
    paths=[]
    for char,advance in zip(label,advances):
        pen=SVGPathPen(glyphs); glyphs[cmap[ord(char)]].draw(pen)
        paths.append(f'<path d="{pen.getCommands()}" transform="translate({x:.3f},{y}) scale({scale},-{scale})" fill="{color}"/>')
        x+=advance*scale
    return ''.join(paths)

# 36 HP: original controls plus CLOCK, WINDOW, and REWORK columns.
parts=['<svg xmlns="http://www.w3.org/2000/svg" width="540" height="380" viewBox="0 0 540 380">',
 '<rect width="540" height="380" fill="#202829"/>',
 '<rect x="1" y="1" width="538" height="378" fill="none" stroke="#465353"/>',
 '<rect x="331" y="100" width="208" height="262" fill="#262f2c"/>',
 '<path d="M330 100V362" stroke="#465353" fill="none"/>',
 '<rect x="11" y="47" width="518" height="46" rx="3" fill="#101818" stroke="#465353"/>',
 '<path d="M15 100H315M15 218H315M15 262H315M15 317H315" stroke="#465353" fill="none"/>',
 text('OTIS',17,30,22,'#80ddc7',False),text('SAMPLER',87,30,12,'#d5dfde',False),text('LAB / 01',283,30,10,'#f1b875',True)]
for row,labels in [(151,['PITCH','START','END','K1']),(211,['REC RATE','REC GAIN','LEVEL','K2'])]:
    for i,label in enumerate(labels): parts.append(text(label,45+80*i,row,10))
# Switch row (band y=218..262): titles above, controls centred at y=246.
# Rack's CKSSThree is 13.5x28.3 px with value 0 at the bottom, so position
# names sit beside the lever at its three stops; CKSS is 14x20.6 px.
for i,label in enumerate(['PLAY','REC','LOOP','REV']): parts.append(text(label,45+80*i,229,10,'#f1b875'))
for label,y in [('PING',238.5),('FWD',248),('OFF',257.5)]: parts.append(text(label,216,y,6,center=False))
parts.append(text('FULL',302,258,6,'#f1b875'))
# Record row: AUDIO L/R (R patched = stereo take), then V/OCT .. END, 45 px apart.
for i,label in enumerate(['AUDIO L','AUDIO R','V/OCT','GATE','REC','START','END']):
    parts.append(text(label,30+45*i,277,8 if i<2 else 9))
# Bottom row: 3 inputs (K1, K2, VCA) + 4 outputs, 46 px apart.
for i,label in enumerate(['K1','K2','VCA','L','R','EOC','POS']):
    parts.append(text(label,27+46*i,327,9,'#80ddc7' if i>=3 else '#d5dfde'))
parts.append(text('ES5505 PLAYBACK / CV CONTROL',165,370,8,'#899e9b'))
# LAB column: non-authentic extensions. Knobs at y=125/187, CV jacks at y=292/341.
parts.append(text('LAB',360,85,10,'#f1b875'))
parts.append(text('WINDOW',435,85,10,'#f1b875'))
parts.append(text('REWORK',506,85,9,'#f1b875'))  # stays inside the display box
parts.append(text('CLOCK',360,151,9)); parts.append(text('INTERP',360,211,9))
parts.append(text('CLK CV',360,277,8)); parts.append(text('INT CV',360,327,8))
parts.append(text('FRAME BUS',360,229,8,'#f1b875'))
parts.append(text('POSITION',435,151,9)); parts.append(text('SIZE',435,211,9))
parts.append(text('MODE',435,229,9,'#f1b875'))
for label,y in [('FRM',238.5),('SCAN',248),('SEG',257.5)]: parts.append(text(label,446,y,6,center=False))
parts.append(text('POS CV',435,277,8)); parts.append(text('SIZE CV',435,327,8))
parts.append(text('COARSE / FINE',508,151,6.5)); parts.append(text('AMOUNT',510,211,9))
parts.append(text('REWORK',510,229,9,'#f1b875')); parts.append(text('BUSY',529,258,5))
parts.append(text('TRIG',510,277,8)); parts.append(text('AMT CV',510,327,8))
parts.append(text('ASR-INSPIRED / ORIGINAL LAB ALGORITHMS',435,370,6,'#899e9b'))
parts.append(glyph(9,478,25,32,'#d5dfde','#f1b875'))  # IX Substitutive change: old route struck out
panel=''.join(parts)+'</svg>'
(root/'res/OtisSampler.svg').write_text(panel)
preview=parts.copy()
preview.append(text('bright-demo.wav',20,64,10,'#80ddc7',False))
preview.append(text('PCM 1.40s  29.76k / CHIP 29.76k',20,83,9,'#80ddc7',False))
for y in [125,187]:
    for x in [45,125,205,285,360,435,510]:
        preview.append(f'<circle cx="{x}" cy="{y}" r="15" fill="#111717" stroke="#81918e" stroke-width="1.4"/><path d="M{x} {y-12}V{y-4}" stroke="#d5dfde" stroke-width="2"/>')
for y,xs in [(246,[360]),(292,[30+45*i for i in range(7)]+[360,435,510]),(341,[27+46*i for i in range(7)]+[360,435,510])]:
    for x in xs:
        preview.append(f'<circle cx="{x}" cy="{y}" r="11" fill="#9ba6a1"/><circle cx="{x}" cy="{y}" r="7.5" fill="#313939"/><circle cx="{x}" cy="{y}" r="5" fill="#090e0d"/>')
# Preview switches at Rack's real component sizes (CKSS 14x20.6, CKSSThree 13.5x28.3).
preview.append('<circle cx="45" cy="246" r="7" fill="#4dc595"/>')
for x,h in [(125,20.6),(205,28.3),(285,20.6),(435,28.3)]:
    preview.append(f'<rect x="{x-7}" y="{246-h/2:.1f}" width="14" height="{h}" rx="2" fill="#111"/><rect x="{x-5}" y="{246+h/2-9:.1f}" width="10" height="7" fill="#777"/>')
preview.append('<circle cx="510" cy="246" r="7" fill="#303b35"/><circle cx="529" cy="246" r="3" fill="#756a21"/>')
preview.append('<circle cx="143" cy="246" r="3" fill="#5a2020"/><circle cx="302" cy="246" r="3" fill="#5a2020"/>')
preview.append(text('REWORK: READY',335,64,9,'#80ddc7',False))
(root/'docs/panel-preview.svg').write_text(''.join(preview)+'</svg>')

# Original synthetic bright percussion, voiced partials and decaying noise.
rate=round(10000000/336); t=np.arange(round(rate*1.4))/rate
rng=np.random.default_rng(1605)
signal=(.20*np.sin(2*np.pi*220*t)+.15*np.sin(2*np.pi*440*t)+.08*np.sin(2*np.pi*1760*t)+.06*np.sin(2*np.pi*7040*t))*np.exp(-3*t)
signal+=.12*rng.normal(size=len(t))*np.exp(-17*t)
signal*=np.minimum(1,t*2000)
pcm=np.clip(np.rint(signal*32768),-32768,32767).astype('<i2')
with wave.open(str(root/'examples/bright-demo.wav'),'wb') as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm.tobytes())
print('Created panel, panel preview and original demo WAV')
