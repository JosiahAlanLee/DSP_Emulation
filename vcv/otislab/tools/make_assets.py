"""Rebuild the original demo WAV and outlined SVG panel. Requires numpy/fontTools."""
from pathlib import Path
import wave
import numpy as np
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen

root=Path(__file__).resolve().parents[1]
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

parts=['<svg xmlns="http://www.w3.org/2000/svg" width="330" height="380" viewBox="0 0 330 380">',
 '<rect width="330" height="380" fill="#202829"/>',
 '<rect x="1" y="1" width="328" height="378" fill="none" stroke="#465353"/>',
 '<rect x="11" y="47" width="308" height="46" rx="3" fill="#101818" stroke="#465353"/>',
 '<path d="M15 100H315M15 218H315M15 262H315M15 317H315" stroke="#465353" fill="none"/>',
 text('OTIS',17,30,22,'#80ddc7',False),text('SAMPLER',87,30,12,'#d5dfde',False),text('LAB / 01',283,30,10,'#f1b875',True)]
for row,labels in [(151,['PITCH','START','END','K1']),(211,['REC RATE','REC GAIN','LEVEL','K2'])]:
    for i,label in enumerate(labels): parts.append(text(label,45+80*i,row,10))
for i,label in enumerate(['PLAY','REC','LOOP','REV']): parts.append(text(label,45+80*i,233,10,'#f1b875'))
parts.append(text('OFF / FWD / PING',205,255,6.5))
parts.append(text('FULL',302,255,6,'#f1b875'))
for y,labels in [(277,['AUDIO','V/OCT','GATE','REC','START','END']),(327,['K1','K2','L','R','EOC','POS'])]:
    for i,label in enumerate(labels):
        parts.append(text(label,30+54*i,y,9,'#80ddc7' if (y==327 and i>=2) else '#d5dfde'))
parts.append(text('ES5505 PLAYBACK / CV CONTROL',165,370,8,'#899e9b'))
panel=''.join(parts)+'</svg>'
(root/'res/OtisSampler.svg').write_text(panel)
preview=parts.copy()
preview.append(text('bright-demo.wav',20,64,10,'#80ddc7',False))
preview.append(text('PCM 1.40s  29.76k / CHIP 29.76k',20,83,9,'#80ddc7',False))
for y in [125,187]:
    for x in [45,125,205,285]:
        preview.append(f'<circle cx="{x}" cy="{y}" r="15" fill="#111717" stroke="#81918e" stroke-width="1.4"/><path d="M{x} {y-12}V{y-4}" stroke="#d5dfde" stroke-width="2"/>')
for y in [292,341]:
    for x in [30,84,138,192,246,300]:
        preview.append(f'<circle cx="{x}" cy="{y}" r="11" fill="#9ba6a1"/><circle cx="{x}" cy="{y}" r="7.5" fill="#313939"/><circle cx="{x}" cy="{y}" r="5" fill="#090e0d"/>')
preview.append('<circle cx="45" cy="242" r="5" fill="#4dc595"/>')
for x in [125,205,285]: preview.append(f'<rect x="{x-5}" y="236" width="10" height="14" rx="2" fill="#aeb7b3"/><circle cx="{x}" cy="239" r="3" fill="#2c3635"/>')
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
