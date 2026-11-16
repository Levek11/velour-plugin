import numpy as np, subprocess, scipy.signal as ss, time
import os; D=os.path.dirname(os.path.abspath(__file__)); H=D+"/harness"; W=D+"/"
sr=48000; rng=np.random.default_rng(1)
def run(x, depth=6, sharp=4, sel=4, atk=5, rel=60, lo=800, hi=16000, mix=1, delta=0, link=1):
    x.astype(np.float32).T.copy().tofile(W+"in.f32")
    t=time.time()
    out=subprocess.run([H,W+"in.f32",W+"out.f32",str(sr),*map(str,[depth,sharp,sel,atk,rel,lo,hi,mix,delta,link])],capture_output=True,text=True)
    el=time.time()-t
    lat=int(out.stdout.split("=")[1])
    y=np.fromfile(W+"out.f32",dtype=np.float32).reshape(-1,2).T
    return y, lat, el
def pink(n):
    w=rng.standard_normal((2,n)); f=np.fft.rfftfreq(n,1/sr); F=np.fft.rfft(w); F[:,1:]/=np.sqrt(f[1:]); F[:,0]=0
    p=np.fft.irfft(F,n); return p/np.max(np.abs(p))*0.3
def psd(x): f,P=ss.welch(x[0],sr,nperseg=8192); return f,10*np.log10(P+1e-30)
N=sr*6
# 1 transparency
x=rng.standard_normal((2,N))*0.1
y,lat,el=run(x,depth=0)
e=y[:,lat:]-x[:,:N-lat]; seg=slice(sr,N-lat-sr)
print(f"latency {lat} samples ({lat/sr*1000:.1f} ms); depth0 null: {20*np.log10(np.std(e[:,seg])/np.std(x[:,seg])):.1f} dB; realtime factor {6/el:.0f}x")
# 2 pink noise + resonance
p=pink(N)
b,a=ss.iirpeak(3150,12,sr)  # narrow resonance
res=p+ss.lfilter(b,a,p)*6.0
res=res/np.max(np.abs(res))*0.5
y,lat,_=run(res)
ya=y[:,lat:]; xa=res[:,:N-lat]
f,Px=psd(xa[:,sr:]); _,Py=psd(ya[:,sr:])
d=Py-Px
def band(f0,bw=0.1):
    m=(f>f0*(1-bw))&(f<f0*(1+bw)); return d[m].mean()
print("pink+3.15k resonance: input peak above neighbours: %.1f dB"%(Px[np.argmin(abs(f-3150))]-np.mean(Px[(f>2000)&(f<2500)])+ (np.mean(Px[(f>2000)&(f<2500)])-np.mean(Px[(f>2000)&(f<2500)]))))
for f0 in [200,1000,2000,3150,5000,10000]:
    print(f"  change at {f0:>5} Hz: {band(f0,0.03 if f0==3150 else 0.1):+.1f} dB")
# 3 plain pink noise -> should be near untouched
y,lat,_=run(p)
f,Px=psd(p[:,sr:N-lat]); _,Py=psd(y[:,lat+sr:])
d=Py-Px; m=(f>1000)&(f<15000)
print(f"plain pink noise: mean change 1k-15k {d[m].mean():+.2f} dB, worst {d[m].min():+.2f} dB")
# 4 delta + wet == dry
yw,lat,_=run(res); yd,_,_=run(res,delta=1)
e=(yw+yd)[:,lat:]-res[:,:N-lat]
print("wet+delta vs dry null: %.1f dB"%(20*np.log10(np.std(e[:,sr:-sr])/np.std(res[:,sr:-sr]))))
# 5 mix 0
y0,lat,_=run(res,mix=0)
e=y0[:,lat:]-res[:,:N-lat]; print("mix 0 null: %.1f dB"%(20*np.log10(np.std(e[:,sr:-sr])/np.std(res[:,sr:-sr]))))
# 6 depth sweep at the resonance
for dep in [3,6,10,15]:
    y,lat,_=run(res,depth=dep); f,Py=psd(y[:,lat+sr:]); f,Px=psd(res[:,sr:N-lat]); d=Py-Px
    m=(f>3050)&(f<3250); print(f"  depth {dep:>2}: resonance cut {d[m].mean():+.1f} dB, 1-2k change {d[(f>1000)&(f<2000)].mean():+.2f} dB")
