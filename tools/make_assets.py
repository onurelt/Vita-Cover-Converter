#!/usr/bin/env python3
"""Build deterministic, palette-indexed Vita LiveArea artwork without external libraries.
Style: charcoal, cyan music-to-cover transfer glyph. No copied logos or splash art.
"""
from pathlib import Path
import math, struct, zlib

OUT = Path("build-assets/sce_sys")
P = [(13, 22, 33), (18, 30, 42), (25, 45, 58), (32, 60, 74),
     (35, 99, 121), (30, 139, 158), (41, 196, 211), (58, 227, 232),
     (221, 250, 253), (115, 145, 158)]
P += [(round(13 + i*.5), round(22 + i*1.6), round(33 + i*1.9)) for i in range(48)]
P += [(19, 43, 58)] * (256 - len(P))

def png(path, w, h, pixels):
    def chunk(t, data):
        return struct.pack(">I",len(data))+t+data+struct.pack(">I",zlib.crc32(t+data)&0xffffffff)
    raw = b"".join(b"\0"+bytes(pixels[y*w:(y+1)*w]) for y in range(h))
    plte = b"".join(bytes(rgb) for rgb in P)
    payload = b"\x89PNG\r\n\x1a\n"
    payload += chunk(b"IHDR",struct.pack(">IIBBBBB",w,h,8,3,0,0,0))
    payload += chunk(b"PLTE",plte)
    payload += chunk(b"IDAT",zlib.compress(raw,9))
    payload += chunk(b"IEND",b"")
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(payload)

class Canvas:
    def __init__(self,w,h):
        self.w,self.h=w,h
        self.pix=bytearray(w*h)
    def rect(self,x,y,w,h,c):
        for yy in range(max(0,y),min(self.h,y+h)):
            a=max(0,x);b=min(self.w,x+w)
            if b>a:self.pix[yy*self.w+a:yy*self.w+b]=bytes([c])*(b-a)
    def circle(self,x,y,r,c):
        for yy in range(max(0,y-r),min(self.h,y+r+1)):
            d=int(math.sqrt(max(0,r*r-(yy-y)**2)))
            self.rect(x-d,yy,2*d+1,1,c)
    def roundrect(self,x,y,w,h,r,c):
        self.rect(x+r,y,w-2*r,h,c)
        self.rect(x,y+r,w,h-2*r,c)
        for dx,dy in ((x+r,y+r),(x+w-r-1,y+r),(x+r,y+h-r-1),(x+w-r-1,y+h-r-1)):
            self.circle(dx,dy,r,c)
    def line(self,x0,y0,x1,y1,thick,c):
        n=max(1,int(math.hypot(x1-x0,y1-y0))*2)
        for k in range(n+1):
            a=k/n
            self.circle(int(x0+(x1-x0)*a),int(y0+(y1-y0)*a),thick,c)

def symbol(c,cx,cy,scale):
    def v(n):return int(round(n*scale))
    x,y=cx-v(50),cy-v(33)
    c.roundrect(x-v(6),y-v(9),v(60),v(61),v(8),3)
    c.roundrect(x,y,v(65),v(65),v(9),5)
    c.roundrect(x+v(5),y+v(5),v(55),v(55),v(8),1)
    # crisp cyan music-note
    c.line(x+v(28),y+v(19),x+v(28),y+v(42),v(3),7)
    c.line(x+v(28),y+v(19),x+v(44),y+v(15),v(3),7)
    c.line(x+v(44),y+v(15),x+v(44),y+v(38),v(3),7)
    c.circle(x+v(23),y+v(42),v(6),7)
    c.circle(x+v(39),y+v(38),v(6),7)
    # conversion arrow
    c.line(x+v(67),y+v(34),x+v(89),y+v(34),v(3),7)
    c.line(x+v(89),y+v(34),x+v(83),y+v(27),v(3),7)
    c.line(x+v(89),y+v(34),x+v(83),y+v(41),v(3),7)
    # thumbnail / cover
    px=x+v(94);py=y+v(14)
    c.roundrect(px,py,v(37),v(42),v(7),7)
    c.roundrect(px+v(4),py+v(4),v(29),v(34),v(5),1)
    c.circle(px+v(26),py+v(12),v(4),7)
    c.line(px+v(6),py+v(33),px+v(14),py+v(23),v(2),7)
    c.line(px+v(14),py+v(23),px+v(19),py+v(29),v(2),7)
    c.line(px+v(19),py+v(29),px+v(25),py+v(21),v(2),7)
    c.line(px+v(25),py+v(21),px+v(31),py+v(32),v(2),7)

def backdrop(w,h):
    c=Canvas(w,h)
    for y in range(h):
        for x in range(w):
            glow=math.exp(-5*((x/w-.30)**2+(y/h-.48)**2))
            c.pix[y*w+x]=10+int(18*glow+12*(1-y/h))
    return c

c=backdrop(128,128)
c.circle(64,64,52,3)
c.circle(64,64,50,1)
symbol(c,54,65,.79)
png(OUT/"icon0.png",128,128,c.pix)

for w,h,name in [(960,544,"pic0.png"),(840,500,"livearea/contents/bg0.png")]:
    c=backdrop(w,h)
    for y in range(28,h,28):
        for x in range(28,w,28):
            c.circle(x,y,1,3)
    for x in range(w):
        envelope=math.sin(math.pi*x/w)**2
        amplitude=int((.16+.84*abs(math.sin(x*.048)))*envelope*h*.13)
        if x%7==0:
            c.rect(x,h//2-amplitude,2,2*amplitude,4)
    cx=w//2 if name=="pic0.png" else int(w*.28)
    cy=int(h*.48)
    c.roundrect(cx-121,cy-93,242,186,28,4)
    c.roundrect(cx-119,cy-91,238,182,26,1)
    symbol(c,cx-18,cy,1.55)
    png(OUT/name,w,h,c.pix)

c=backdrop(280,158)
c.roundrect(16,14,248,130,19,4)
c.roundrect(18,16,244,126,17,1)
symbol(c,126,72,1.25)
png(OUT/"livearea/contents/startup.png",280,158,c.pix)

(OUT/"livearea/contents/template.xml").write_text(
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<livearea style="a1" format-ver="01.00" content-rev="4">\n'
    '  <livearea-background><image>bg0.png</image></livearea-background>\n'
    '  <gate><startup-image>startup.png</startup-image></gate>\n'
    '</livearea>\n',encoding="utf-8")
for path in OUT.rglob("*"):
    if path.is_file():print(path,path.stat().st_size)
