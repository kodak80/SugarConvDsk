"""Check IPF IMGE / block consistency the way strict readers (Aufit) do : data and gap bits of the
blocks add up to the IMGE counts, track bytes = each block rounded up to bytes, and the stream
elements of each block fill exactly its bits (weak data counts data bits, 2 cells each).
Usage: ipfconsist.py FILE.ipf [...]   (exit status 1 if a file has inconsistent tracks)"""
import os, struct, sys
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ipfdump.py')).read().split('def main')[0])
def elements_bits(payload, off, inbits):
    bits=0
    while True:
        h=payload[off]; off+=1
        if h==0: break
        szw=h>>5; typ=h&31; sz=int.from_bytes(payload[off:off+szw],'big'); off+=szw
        if inbits:
            bits+= sz*2 if typ in (2,3,5) else sz   # data bits -> cells ; sync = cells
            if typ!=5: off+=(sz+7)//8
        else:
            bits+= sz*16 if typ in (2,3,5) else sz*8
            if typ!=5: off+=sz
    return bits
def check(path, verbose=False):
    d=open(path,'rb').read(); imges={}; probs=[]
    info=None
    for t,p,rec,payload,key in records(d):
        if t=='INFO': info=struct.unpack('>21I',rec[12:96])
        if t=='IMGE':
            v=struct.unpack('>17I',rec[12:80]); imges[v[13]]=v
        if t=='DATA' and payload:
            v=imges[key]; cyl,head=v[0],v[1]
            tb,sbyte,sbit,dbits,gbits,trbits,nblk=v[4],v[5],v[6],v[7],v[8],v[9],v[10]
            blks=[struct.unpack('>8I',payload[i*32:i*32+32]) for i in range(nblk)]
            sd=sum(b[0] for b in blks); sg=sum(b[1] for b in blks)
            issues=[]
            if sd!=dbits: issues.append(f'databits {dbits} != sum blocks {sd}')
            if sg!=gbits: issues.append(f'gapbits {gbits} != sum gaps {sg}')
            if dbits+gbits!=trbits: issues.append(f'trackbits {trbits} != data+gap {dbits+gbits}')
            sumceil=sum((b[0]+b[1]+7)//8 for b in blks)
            if tb != sumceil: issues.append(f'trackbytes {tb} != sum of block bytes {sumceil}')
            if sbyte != sbit//8: issues.append(f'startbyte {sbyte} != startbit/8')
            for i,b in enumerate(blks):
                eb=elements_bits(payload,b[7],b[5]&4)
                if eb!=b[0]: issues.append(f'block {i}: elements {eb} cells != blockbits {b[0]} (flags {b[5]})')
            if issues: probs.append((cyl,head,issues))
    return info,probs
if __name__=='__main__':
    bad=0
    for f in sys.argv[1:]:
        if f.startswith('-'): continue
        info,probs=check(f)
        print(f, 'encoder',info[1] if info else '?','tracks with issues:',len(probs))
        for c,h,iss in probs[:6]: print(f'  {c:02}.{h}:', '; '.join(iss[:4]))
        bad+=bool(probs)
    sys.exit(1 if bad else 0)
