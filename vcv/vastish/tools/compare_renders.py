#!/usr/bin/env python3
"""Report full-program statistics; no waveform-identity claim across hosts/seeds."""
import argparse, json
from pathlib import Path
import numpy as np
from scipy.io import wavfile
p=argparse.ArgumentParser(); p.add_argument('pd_directory'); p.add_argument('native_directory'); p.add_argument('output'); args=p.parse_args()
rows=[]
for i in range(1,6):
    ra,a=wavfile.read(Path(args.pd_directory)/f'prog{i}.wav'); rb,b=wavfile.read(Path(args.native_directory)/f'prog{i}.wav')
    assert ra==rb and len(a)>0 and len(b)>0
    if np.issubdtype(a.dtype,np.integer): a=a.astype(float)/32768
    if np.issubdtype(b.dtype,np.integer): b=b.astype(float)/32768
    assert np.isfinite(a).all() and np.isfinite(b).all()
    arms=float(np.sqrt(np.mean(a*a))); brms=float(np.sqrt(np.mean(b*b)))
    row={'program':i,'sample_rate':ra,'pd_frames':len(a),'native_frames':len(b),'pd_peak':float(np.max(abs(a))),'native_peak':float(np.max(abs(b))),'pd_stereo_rms':arms,'native_stereo_rms':brms,'rms_difference_percent':100*(brms/arms-1)}
    rows.append(row); print(f"Program {i}: RMS delta {row['rms_difference_percent']:+.3f}%")
Path(args.output).write_text(json.dumps({'reference':'Pure Data 0.55-2; real-time headless render of pd/vastish tools/mkseq.py events','native':'tests/render, same event list; deterministic native random seeds','programs':rows},indent=2)+'\n')
