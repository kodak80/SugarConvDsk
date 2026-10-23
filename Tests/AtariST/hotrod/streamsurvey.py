"""Compare decoded data-stream bytes of several IPF/CTR images track by track.
Usage: streamsurvey.py REF other1 [other2 ...] [--from A --to B]
For each track: bytes after the stream sync (read without resync), number of differing bytes vs REF
(first/last positions), and bytes that vary between two locks (weak) in each image."""
import sys, ctypes as C
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); from ipfcheck import *
S='0100010010001001'
import os; LIM=int(os.environ.get('LIM','100000'))
lib=C.CDLL(os.environ.get('CAPSIMG', 'libcapsimage.so')); lib.CAPSInit()
def opener(path):
    cid=lib.CAPSAddImage(); lib.CAPSLockImage(cid,path.encode()); fl=F_DENVAR|F_UPDATEFD|F_TYPE
    lib.CAPSLoadImage(cid,fl)
    def get(cyl):
        ti=TI2(); ti.type=2
        if lib.CAPSLockTrack(C.byref(ti),cid,cyl,0,fl)!=0 or ti.tracklen==0: return None
        x=''.join(f'{b:08b}' for b in bytes(ti.trackbuf[:ti.tracklen])); lib.CAPSUnlockTrack(cid,cyl,0); return x
    return get
def stream(bits):
    i=bits.find(S*int(os.environ.get('NS','3')))
    if i<0: return None
    while bits[i:i+16]==S: i+=16
    X=bits[i:]+bits[:i]; n=len(X)//16
    return bytes(int(X[k*16+1:k*16+16:2],2) for k in range(n))
args=[a for a in sys.argv[1:]]
lo,hi=2,76
if '--from' in args: k=args.index('--from'); lo=int(args[k+1]); del args[k:k+2]
if '--to' in args: k=args.index('--to'); hi=int(args[k+1]); del args[k:k+2]
imgs=[(p,opener(p)) for p in args]
for cyl in range(lo,hi+1):
    out=[f'{cyl:02}']
    ref=None
    for j,(p,get) in enumerate(imgs):
        b1=get(cyl); b2=get(cyl)
        if b1 is None: out.append('none'); continue
        s1=stream(b1); s2=stream(b2)
        if s1 is None: out.append('nosync'); continue
        weak=[k for k in range(min(len(s1),len(s2))) if s1[k]!=s2[k]]
        if j==0: ref=s1; out.append(f'ref len={len(s1)} weak={len(weak)}{weak[:3]}')
        else:
            if ref is None: out.append("noref"); continue
            n=min(len(ref),len(s1),LIM)
            d=[k for k in range(n) if ref[k]!=s1[k]]
            out.append(f'diff={len(d)}{d[:4]}{"..%d"%d[-1] if len(d)>4 else ""} weak={len(weak)}{weak[:3]}')
    print(' | '.join(out))
