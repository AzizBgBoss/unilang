#!/usr/bin/env python3
"""Extract the assets of the PICO-8 'Super Mario Bros.' cart (smbp8) into a C header.

Pipeline (ported from the cart's own code):
  1. read the cart PNG: the ROM is hidden in the 2 low bits of each ARGB channel
  2. decompress the cart code (PXA format) and pull out the level-building tables
  3. run the cart's two image decoders (move-to-front + run-length coder) to
     unpack the sprite sheet and the 109x14 "chunk" map from the music/sfx area
  4. run the cart's level builder to assemble World 1-1 as a 14 x 240 tile grid

Usage: python extract_cart.py smbp8-13_p8.png -o smb_data.h
Needs: pillow
"""
import argparse
from PIL import Image

def read_rom(path):
    im = Image.open(path).convert('RGBA')
    rom = bytearray()
    for r, g, b, a in im.getdata():
        rom.append(((a & 3) << 6) | ((r & 3) << 4) | ((g & 3) << 2) | (b & 3))
    return bytes(rom)

def pxa_decompress(data):
    assert data[:4] == b'\x00pxa', 'unsupported code format'
    dlen = (data[4] << 8) | data[5]
    pos = 64
    def bits(n):
        nonlocal pos
        v = 0
        for i in range(n):
            v |= ((data[pos >> 3] >> (pos & 7)) & 1) << i
            pos += 1
        return v
    mtf = list(range(256)); out = bytearray()
    while len(out) < dlen:
        if bits(1):
            u = 0
            while bits(1): u += 1
            n = 4 + u
            idx = bits(n) + (1 << n) - 16
            c = mtf.pop(idx); mtf.insert(0, c); out.append(c)
        else:
            if not bits(1): ob = 15
            elif not bits(1): ob = 10
            else: ob = 5
            off = bits(ob) + 1
            if ob == 10 and off == 1:
                while True:
                    c = bits(8)
                    if c == 0: break
                    out.append(c)
            else:
                ln = 3
                while True:
                    p = bits(3); ln += p
                    if p != 7: break
                for _ in range(ln): out.append(out[-off])
    return bytes(out)

def parse_lua_str(b, start):
    q = b[start]; i = start + 1; out = bytearray()
    esc = {ord('n'): 10, ord('r'): 13, ord('t'): 9, ord('\\'): 92, ord('"'): 34, ord("'"): 39,
           ord('a'): 7, ord('b'): 8, ord('f'): 12, ord('v'): 11}
    while b[i] != q:
        c = b[i]
        if c == 0x5c:
            i += 1; c = b[i]
            if 48 <= c <= 57:
                j = i; v = 0; k = 0
                while k < 3 and 48 <= b[j] <= 57: v = v * 10 + b[j] - 48; j += 1; k += 1
                out.append(v); i = j; continue
            if c == ord('x'):
                out.append(int(b[i + 1:i + 3], 16)); i += 3; continue
            out.append(esc[c]); i += 1; continue
        out.append(c); i += 1
    return bytes(out)

class Cart:
    def __init__(self, rom):
        self.mem = bytearray(65536)
        self.mem[0:0x4300] = rom[0:0x4300]
        self.mem[0x8000:0x8000 + 0x2300] = self.mem[0x2000:0x2000 + 0x2300]
        self.el = [0]                       # 1-based nibble list of level data
        for n in range(8192, 12502):
            self.el.append(self.mem[n] >> 4); self.el.append(self.mem[n] & 15)
        code = pxa_decompress(rom[0x4300:0x8000])
        self.dg = parse_lua_str(code, code.index(b"dg='") + 3)
        dxs = parse_lua_str(code, code.index(b'dx=split"') + 8)
        self.dx = [None] + [int(v) for v in dxs.split(b',')]
        self.scenery = parse_lua_str(code, code.index(b'tk=ord(') + 7)
        self.dh(37077, self.mget, self.mset)     # map
        self.dh(37881, self.sget, self.sset)     # sprite sheet
        self.mem[0x3000:0x3100] = self.mem[0x4200:0x4300]   # sprite flags

    def mget(self, x, y):
        return self.mem[0x2000 + y * 128 + x] if y < 32 else self.mem[0x1000 + (y - 32) * 128 + x]
    def mset(self, x, y, v):
        if y < 32: self.mem[0x2000 + y * 128 + x] = v
        else: self.mem[0x1000 + (y - 32) * 128 + x] = v
    def sget(self, x, y):
        b = self.mem[y * 64 + x // 2]
        return b >> 4 if x & 1 else b & 15
    def sset(self, x, y, v):
        a = y * 64 + x // 2
        self.mem[a] = (self.mem[a] & 0x0f) | (v << 4) if x & 1 else (self.mem[a] & 0xf0) | v
    def fget(self, t): return self.mem[0x3000 + t]

    def dh(self, start, getf, setf):
        mem = self.mem; bitpos = start * 8
        def rd(e):
            nonlocal bitpos
            b = bitpos >> 3
            raw = int.from_bytes(bytes(mem[b:b + 4]), 'little')
            v = (raw >> (bitpos & 7)) & ((1 << e) - 1)
            bitpos += e
            return v
        def num(base):
            n = 0; e = base
            while True:
                n += 1; d = rd(n); e += d
                if d < (1 << n) - 1: return e
        def mtf(lst, t):
            lst.pop(lst.index(t)); lst.insert(0, t)
        h = num(0); l = num(0); c = num(1)
        sym = [rd(c) for _ in range(num(1))]
        ctx = {}; d = 0; t = None
        for y in range(l + 1):
            for x in range(h + 1):
                d -= 1
                if d < 1:
                    d = num(1); t = True if t is None else not t
                a = getf(x, y - 1) if y > 0 else 0
                lst = ctx.setdefault(a, list(sym))
                n = lst[0] if t else lst[num(2) - 1]
                mtf(lst, n); mtf(sym, n)
                setf(x, y, n)

GROUPS = [[1,1,2,1,3,3],[7,1,2,1,3,3],[1,7,2,1,3,3],[7,7,2,1,3,3],[7,7,1,1,2,2],[7,1,1,1,5,6],
          [1,7,1,1,5,6],[7,7,1,1,5,6],None,[7,1,1,2,4,4],[1,7,1,2,4,4],[7,7,1,2,4,4],None,
          [7,1,1,1,5,6],[1,7,1,1,5,6],[7,7,1,1,5,6]]

def dc(cart, d, t, nq, ej):
    dg, el, dx = cart.dg, cart.el, cart.dx
    if t > 0:
        for n in range(14): nq[n] = {}
    i0, i1 = dg[0], dg[1]
    pos = [dx[d]]
    def n(k):
        v = 0
        for tt in range(1, k + 1):
            v += el[pos[0]] << (4 * (k - tt)); pos[0] += 1
        return v
    w = n(1); tb = n(1); ef = n(2) * 16 + 16
    dp = 0
    while pos[0] < dx[d + 1] - 1:
        ne = n(2)
        if ne >= 128:
            e4 = ne % 16; ns = n(1)
            ne = [21, 45, 83, 9, 24, 25, 28, 15][ne // 16 - 8]
        else:
            e4 = n(1); ns = n(1)
        if ns > 12:
            dp += ns - 12; ns = n(1)
        e4 += dp * 16
        o4 = 4 * ne + 3
        r, l, b3, b4 = dg[o4], dg[o4 + 1], dg[o4 + 2], dg[o4 + 3]
        e = b3 // 16 + 1; dd = b3 % 16 + 1; ii = b4 // 16; f = ii // 13
        params = []
        for tt in range(6):
            dw = GROUPS[ii][tt]
            vals = [0, 1, 256, i1, e, dd, n(dw // 7)]
            params.append(vals[dw - 1])
        eq, ez, db, dj, dv, dm = params
        I = max(e + eq, eq * 16 * f) - 1
        F = max(dd + ez, ez * i0 * f) - 1
        o = 0
        if 65 <= ne <= 74: o = ne - 64          # block content id (the cart stores it as a fraction)
        if 75 <= ne <= 122:
            ej.append(dict(entnum=ne - 74, x=e4 * 8, y=ns * 8, xv=eq, yv=ez))
        for nn in range(0, t + 1):
            c = nn * 48
            for tx in range(0, I + 1):
                for ny in range(0, F + 1):
                    h = (tx - e) % dv; a = (ny - dd) % dm
                    sx = r + ((db * e + h + dj * ((tx // I) * (dv - h) + e)) if tx >= e else tx)
                    sy = l + ((db * dd + a + dj * ((ny // F) * (dm - a) + dd)) if ny >= dd else ny)
                    tile = cart.mget(sx, sy)
                    if cart.fget(tile) < 128 and ns + ny < 14:
                        nq[ns + ny][e4 + tx + c] = (tile, o)
    return w, tb, ef

def build_level(cart, y, tk):
    nq = [{} for _ in range(14)]; ej = []
    dc(cart, tk, 8, nq, ej)
    w, tb, ef = dc(cart, y, 0, nq, ej)
    return nq, ej, w, tb, ef

PICO = [(0,0,0),(29,43,83),(126,37,83),(0,135,81),(171,82,54),(95,87,79),(194,195,199),(255,241,232),
        (255,0,77),(255,163,0),(255,236,39),(0,228,54),(41,173,255),(131,118,156),(255,119,168),(255,204,170)]
SECRET = {134:(162,136,121),137:(255,108,36),138:(168,231,46),139:(0,181,67),140:(6,90,181)}
def rgb(c): return PICO[c] if c < 16 else SECRET[c]
def rgb332(c):
    r, g, b = rgb(c); return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6)
# world 1 display palette (the cart's e_()): final colour -> displayed colour
DISP = {1:3, 2:140, 3:139, 4:4, 5:5, 6:6, 7:7, 8:8, 9:9, 10:137, 11:138, 12:12, 13:134}

def emit(name, vals, per_line=16, typ='uint8_t'):
    s = 'const %s %s[%d] = {\n' % (typ, name, len(vals))
    for i in range(0, len(vals), per_line):
        s += '    ' + ', '.join(str(v) for v in vals[i:i + per_line]) + ',\n'
    return s.rstrip(',\n') + '\n};\n\n'

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cart'); ap.add_argument('-o', default='smb_data.h'); ap.add_argument('--level', type=int, default=1)
    a = ap.parse_args()
    cart = Cart(read_rom(a.cart))
    nq, ej, w, tb, ef = build_level(cart, a.level, cart.scenery[a.level - 1])
    W = ef
    level = [0] * (14 * W); cont = []
    for r in range(14):
        for x, (t, o) in nq[r].items():
            if x < W:
                level[r * W + x] = t
                if o: cont.append((x, r, o))
    h = '// Generated by extract_cart.py from the PICO-8 cart. Do not edit.\n'
    h += '#define LVL_W %d\n#define LVL_H 14\n#define NCONT %d\n#define NENT %d\n\n' % (W, len(cont), len(ej))
    h += emit('smb_gfx', list(cart.mem[0:8192]))                    # 128x128, 4bpp, low nibble = even x
    h += emit('smb_flags', list(cart.mem[0x3000:0x3100]))           # sprite flags
    h += emit('smb_level', level, 20)                               # row-major tile ids
    h += emit('cont_x', [c[0] for c in cont]) + emit('cont_y', [c[1] for c in cont]) + emit('cont_v', [c[2] for c in cont])
    h += emit('ent_type', [e['entnum'] for e in ej]) + emit('ent_x', [e['x'] for e in ej], typ='uint16_t') + emit('ent_y', [e['y'] for e in ej])
    h += emit('disp332', [rgb332(DISP.get(c, c)) for c in range(16)])  # final colour -> RGB332
    open(a.o, 'w').write(h)
    print('wrote %s: level %d is %dx14, %d block contents, %d entities' % (a.o, a.level, W, len(cont), len(ej)))

if __name__ == '__main__':
    main()
