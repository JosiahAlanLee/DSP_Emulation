import numpy as np, wave, subprocess, json
from pathlib import Path
from scipy.signal import periodogram
root=Path(__file__).resolve().parents[1]
chip=10000000/336
rates=[44100,48000,96000]
source_rate=round(chip)
n=round(source_rate*2)
f=source_rate*.4
samples=(.5*np.sin(2*np.pi*f*np.arange(n)/source_rate)*32767).astype('<i2')
path=root/'tests/build/alias-input.wav'
with wave.open(str(path),'wb') as w:
 w.setnchannels(1);w.setsampwidth(2);w.setframerate(source_rate);w.writeframes(samples.tobytes())
results=[]
for rate in rates:
 for semi in [-12,0,12,24]:
  output=root/f'tests/build/alias-{rate}-{semi}.wav'
  subprocess.run([str(root/'tests/build/render'),str(path),str(output),str(semi),'0',str(rate)],check=True)
  with wave.open(str(output),'rb') as w: x=np.frombuffer(w.readframes(w.getnframes()),dtype='<i2').astype(float)
  frequencies,power=periodogram(x[int(rate*.1):],rate,nfft=1<<19)
  measured=frequencies[np.argmax(power)]
  ratio=2**(semi/12)*source_rate/chip
  increment=np.clip(np.rint(ratio*512)*4,4,131068)/2048
  expected=(.4*chip*increment)%chip
  if expected>chip/2: expected=chip-expected
  error=abs(measured-expected)
  assert error<3,(rate,semi,measured,expected)
  results.append(dict(hostRate=rate,semitones=semi,expectedHz=round(expected,3),measuredHz=round(measured,3),errorHz=round(error,3)))
(root/'tests/build/spectral-results.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS: 12 spectral checks; native pitch aliases retained across 44.1/48/96 kHz hosts')
# Audition uses the actual engine, -24/-12/0/+12/+24 semitones, with short silences.
clips=[]
for semi in [-24,-12,0,12,24]:
 output=root/f'tests/build/demo-{semi}.wav'
 subprocess.run([str(root/'tests/build/render'),str(root/'examples/bright-demo.wav'),str(output),str(semi),'0','48000'],check=True)
 with wave.open(str(output),'rb') as w: clips.append(w.readframes(w.getnframes()))
 clips.append(np.zeros(12000,dtype='<i2').tobytes())
with wave.open(str(root/'examples/pitch-audition.wav'),'wb') as w:
 w.setnchannels(1);w.setsampwidth(2);w.setframerate(48000);w.writeframes(b''.join(clips))
