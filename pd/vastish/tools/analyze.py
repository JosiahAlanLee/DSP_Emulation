import numpy as np, wave, glob, sys
for f in sorted(glob.glob(sys.argv[1]+"/*.wav")):
    w=wave.open(f); n=w.getnframes(); sr=w.getframerate(); ch=w.getnchannels()
    a=np.frombuffer(w.readframes(n),dtype=np.int16).reshape(-1,ch)/32768.0
    m=a.mean(1); seg=sr//4
    rms=[np.sqrt(np.mean(m[i:i+seg]**2)) for i in range(0,len(m)-seg,seg)]
    cents=[]
    for i in range(0,len(m)-seg,seg):
        x=m[i:i+seg]; sp=np.abs(np.fft.rfft(x*np.hanning(seg))); fr=np.fft.rfftfreq(seg,1/sr)
        cents.append((sp*fr).sum()/max(sp.sum(),1e-9))
    print(f.split('/')[-1], f"peak={np.abs(a).max():.2f} clip%={100*(np.abs(a)>0.999).mean():.2f} Lrms={np.sqrt((a[:,0]**2).mean()):.3f} Rrms={np.sqrt((a[:,1]**2).mean()):.3f}")
    print("  rms :", " ".join(f"{r:.2f}" for r in rms))
    print("  cent:", " ".join(f"{c/1000:.1f}" for c in cents))
