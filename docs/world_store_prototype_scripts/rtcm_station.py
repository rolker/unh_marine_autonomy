#!/usr/bin/env python3
"""Decode RTCM 3 station messages (1005/1006 ARP ECEF + ITRF year, 1032 physical ref) from a rosbag2's
/bizzy/mavros/gps_rtk/send_rtcm topic. Prints one line per distinct station position seen."""
import sys, math
from pathlib import Path
from rosbags.highlevel import AnyReader
def bits(b, off, n, signed=False):
    v = 0
    for i in range(n): v = (v << 1) | ((b[(off+i)//8] >> (7-((off+i)%8))) & 1)
    if signed and v >> (n-1): v -= 1 << n
    return v
def ecef2lla(x,y,z):
    a=6378137.0; f=1/298.257223563; e2=f*(2-f); lon=math.atan2(y,x); p=math.hypot(x,y); lat=math.atan2(z,p*(1-e2))
    for _ in range(6):
        N=a/math.sqrt(1-e2*math.sin(lat)**2); h=p/math.cos(lat)-N; lat=math.atan2(z,p*(1-e2*N/(N+h)))
    return math.degrees(lat), math.degrees(lon), h
def parse(frame):
    # frame: 0xD3, 6-bit reserved + 10-bit length, payload, 3-byte CRC
    if not frame or frame[0] != 0xD3: return None
    L = bits(frame, 14, 10); p = frame[3:3+L]; t = bits(p, 0, 12)
    if t in (1005, 1006):
        sid=bits(p,12,12); itrf=bits(p,24,6); off=12+12+6+4   # after type, station id, ITRF year, 4 flag bits comes ECEF X; X→Y and Y→Z each skip 2 flag bits
        x=bits(p,off,38,True)/1e4; off+=38+2; y=bits(p,off,38,True)/1e4; off+=38+2; z=bits(p,off,38,True)/1e4
        return t, sid, itrf, (x,y,z)
    return None
seen = {}
for bag in sys.argv[1:]:
    with AnyReader([Path(bag)]) as r:
        conns=[c for c in r.connections if c.topic.endswith('gps_rtk/send_rtcm')]
        n=0; data=bytearray()
        for c, ts, raw in r.messages(connections=conns):
            m=r.deserialize(raw, c.msgtype); n+=1; data+=bytes(m.data)   # frames are split across messages: buffer the whole stream
        i=0
        while i+3 <= len(data):
            if data[i]!=0xD3: i+=1; continue
            L=bits(data[i:i+3],14,10)
            if i+3+L+3 > len(data): break
            fr=bytes(data[i:i+3+L+3]); i+=3+L+3
            try: r_=parse(fr)
            except IndexError: continue
            if r_:
                key=(Path(bag).name, r_[0], r_[1], round(r_[3][0],2), round(r_[3][1],2), round(r_[3][2],2))
                seen.setdefault(key, [r_[2], 0])[1]+=1
        print(f"# {Path(bag).name}: {n} RTCM messages, {len(data)} bytes", file=sys.stderr)
for (bag,t,sid,x,y,z),(itrf,cnt) in sorted(seen.items()):
    lat,lon,h=ecef2lla(x,y,z)
    print(f"{bag}  msg{t} station {sid}  itrf_year_field={itrf}  ECEF=({x:.3f},{y:.3f},{z:.3f})  lat={lat:.7f} lon={lon:.7f} h={h:.3f}  n={cnt}")
