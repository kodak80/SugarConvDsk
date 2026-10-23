import struct
SCK=24027428.5714285
def parse(path):
    d=open(path,'rb').read(); i=0; flux=[]; idx=[]; acc=0; pos=0; fpos=[]
    while i<len(d):
        b=d[i]
        if b<=0x07:
            v=(b<<8)|d[i+1]; flux.append(acc+v); fpos.append(pos); acc=0; i+=2; pos+=2
        elif b==0x08: i+=1; pos+=1
        elif b==0x09: i+=2; pos+=2
        elif b==0x0A: i+=3; pos+=3
        elif b==0x0B: acc+=0x10000; i+=1; pos+=1
        elif b==0x0C:
            v=(d[i+1]<<8)|d[i+2]; flux.append(acc+v); fpos.append(pos); acc=0; i+=3; pos+=3
        elif b==0x0D:
            t=d[i+1]; ln=struct.unpack('<H',d[i+2:i+4])[0]
            if t==0x0D: break
            if t==2:
                sp,sc,ic=struct.unpack('<III',d[i+4:i+16]); idx.append((sp,sc))
            i+=4+ln
        else:
            flux.append(acc+b); fpos.append(pos); acc=0; i+=1; pos+=1
    return flux, idx, fpos
