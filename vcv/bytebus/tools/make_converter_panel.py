"""Build Rack-compatible outlined labels for Converter (requires fontTools)."""
from pathlib import Path
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen

ROOT=Path(__file__).resolve().parents[1]
font=TTFont('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
glyphs=font.getGlyphSet(); cmap=font.getBestCmap(); units=font['head'].unitsPerEm

def text(label,x,y,size):
    scale=size/units
    advances=[font['hmtx'][cmap[ord(c)]][0] for c in label]
    x-=sum(advances)*scale/2
    result=[]
    for char,advance in zip(label,advances):
        pen=SVGPathPen(glyphs); glyphs[cmap[ord(char)]].draw(pen)
        result.append(f'<path d="{pen.getCommands()}" transform="translate({x:.3f},{y}) scale({scale},-{scale})" fill="#273638"/>')
        x+=advance*scale
    return ''.join(result)

parts=['<svg xmlns="http://www.w3.org/2000/svg" width="210" height="380" viewBox="0 0 210 380">',
       '<rect width="210" height="380" fill="#d9d8ce"/>',
       '<rect x="8" y="8" width="194" height="364" rx="5" fill="none" stroke="#343d3d"/>',
       '<path d="M20 304H190" fill="none" stroke="#586361"/>']
labels=[('CONVERTER',105,29,17),('ADC',50,46,9),('DAC',160,46,9),
        ('GAIN',50,137,10),('GAIN',160,137,10),('OFFSET',50,197,10),('OFFSET',160,197,10),
        ('±10 V',72,219,8),('±5 V',72,235,8),('±10 V',182,219,8),('±5 V',182,235,8),
        ('CLIP',105,244,8),('SIGNAL',42,265,9),('CLOCK',105,265,9),('BUS IN',168,265,9),
        ('ADC BUS',42,321,9),('CLK OUT',105,321,9),('DAC OUT',168,321,9),
        ('8 BITS · INDEPENDENT PATHS',105,363,8)]
parts += [text(*label) for label in labels]
(ROOT/'res/Converter.svg').write_text(''.join(parts)+'</svg>\n')
print('Created outlined Converter panel')
