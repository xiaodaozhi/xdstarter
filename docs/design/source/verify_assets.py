"""Read-only PNG dimensions, alpha and sampled text-background contrast checks."""
from pathlib import Path
import struct
import zlib

OUT = Path(__file__).resolve().parents[1] / 'brand'

def read_png(path):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    pos, compressed = 8, bytearray()
    while pos < len(data):
        n = struct.unpack('>I', data[pos:pos+4])[0]
        tag, payload = data[pos+4:pos+8], data[pos+8:pos+8+n]
        if tag == b'IHDR':
            w,h,depth,kind,_,_,interlace = struct.unpack('>IIBBBBB',payload)
        elif tag == b'IDAT':
            compressed.extend(payload)
        pos += n+12
    assert depth == 8 and kind in (2,6) and interlace == 0
    channels = 4 if kind == 6 else 3
    stride, rows, previous = w*channels, [], bytearray(w*channels)
    raw = zlib.decompress(compressed)
    for y in range(h):
        start = y*(stride+1)
        mode, row = raw[start], bytearray(raw[start+1:start+stride+1])
        for i in range(stride):
            a = row[i-channels] if i >= channels else 0
            b = previous[i]
            c = previous[i-channels] if i >= channels else 0
            if mode == 1: value = a
            elif mode == 2: value = b
            elif mode == 3: value = (a+b)//2
            elif mode == 4:
                p = a+b-c
                d = (abs(p-a),abs(p-b),abs(p-c))
                value = (a,b,c)[d.index(min(d))]
            else:
                assert mode == 0
                value = 0
            row[i] = (row[i]+value)&255
        rows.append(row)
        previous = row
    return w,h,channels,rows

def luminance(rgb):
    v = [c/255 for c in rgb]
    v = [c/12.92 if c <= .04045 else ((c+.055)/1.055)**2.4 for c in v]
    return sum(c*k for c,k in zip(v,(.2126,.7152,.0722)))
