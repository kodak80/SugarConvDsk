"""Lock one IPF track with capsimg and print sync positions / weak count. Usage: cells.py ipf cyl head [index]"""
import sys, ctypes as C
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); from ipfcheck import *
def cells(path,cyl,head,index=True):
    lib=C.CDLL(os.environ.get('CAPSIMG', 'libcapsimage.so')); lib.CAPSInit()
    cid=lib.CAPSAddImage(); lib.CAPSLockImage(cid,path.encode())
    fl=F_DENVAR|F_UPDATEFD|F_TYPE|(F_INDEX if index else 0)
    lib.CAPSLoadImage(cid,fl); ti=TI2(); ti.type=2
    lib.CAPSLockTrack(C.byref(ti),cid,cyl,head,fl)
    n=ti.tracklen; buf=bytes(ti.trackbuf[:n])
    bits=''.join(f'{b:08b}' for b in buf)
    tb=[ti.timebuf[k] for k in range(ti.timelen)] if ti.timelen else []
    return bits,tb,ti.weakcnt,ti.startbit,ti.overlap
def rd(bits,p,cnt):
    n=len(bits); return bytes(int(''.join(bits[(p+i*16+j*2+1)%n] for j in range(8)),2) for i in range(cnt))
if __name__=='__main__':
    p,c,h=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]); idx=len(sys.argv)<5 or sys.argv[4]!='0'
    bits,tb,w,sb,ov=cells(p,c,h,idx)
    print(p,'cells',len(bits),'weak',w,'startbit',sb,'overlap',ov)
    s='0100010010001001'; i=0
    while True:
        i=bits.find(s,i)
        if i<0: break
        print(' sync@',i, rd(bits,i+16,12).hex()); i+=16*30
