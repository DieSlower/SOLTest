import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tunnels as T
from tunnels import *
import tunnels2 as T2
import tunnels3 as T3
import tunnels4 as T4
import tunnels5 as T5

GREEN = (0x5C, 0x8F, 0x6C); RED = (0x9A, 0x56, 0x56)
T4.GREEN = GREEN; T4.RED = RED; T5.GREEN = GREEN; T5.RED = RED
KEY = 'rings'
_ref = Image.open(os.path.join(T.OUT, "tunnel_3_rings.gif"))
REFPAL = Image.new("P", (1,1)); REFPAL.putpalette(_ref.getpalette()[:768])
REFCOLS = [tuple(_ref.getpalette()[3*k:3*k+3]) for k in range(20)]
def refq(img): return img.quantize(palette=REFPAL, dither=Image.Dither.NONE).convert("RGB")

def wall(dr, i, colored):
    # rings exactly as in the first rendering (CYAN, full strength), guide dots TEAL 0.55 r1.0 or muted lane tint
    SP = 1.4; off = (i/N)*2*SP
    for k in range(-1, int(ZF/SP)+2):
        z = k*SP - off + SP
        if z > 0.5:
            poly(dr, [(math.cos(2*math.pi*j/48), math.sin(2*math.pi*j/48), z) for j in range(48)], CYAN, 1.0, 2)
    DS = 0.6; off2 = (i/N)*DS*2
    for g in range(8):
        a = 2*math.pi*(g+0.5)/8
        if colored: continue
        col, kk, r = TEAL, 0.55, 1.0
        for k in range(int(ZF/DS)+2):
            z = k*DS - off2 + DS
            if z > 0.5: dot(dr, (math.cos(a), math.sin(a), z), col, kk, r)

def make_pal(colored):
    # fixed fine ramps so ring cyan and the muted lane tints survive
    cols = list(REFCOLS) + [(60,66,90),(110,125,130),(255,255,255)]
    bases = [TEAL] + ([GREEN, RED] if colored else [])
    lv = [0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9,1.0]
    for b in bases: cols += [sc(b, k) for k in lv]
    # glow-saturated cyan/teal toward white
    for b in (TEAL,):
        cols += [tuple(int(c + (255-c)*t) for c in b) for t in (0.25, 0.5, 0.75)]
    if colored:
        for b in (GREEN, RED):
            cols += [tuple(int(c + (255-c)*t) for c in b) for t in (0.3, 0.6)]
    flat = [v for c in cols for v in c]; flat += [0]*(768-len(flat))
    p = Image.new("P", (1,1)); p.putpalette(flat); return p

def render(name, colored):
    sy = T2.GEO[KEY][4]
    co, ci = (GREEN, RED) if colored else (TEAL, TEAL)
    ships = [((0.5, sy[0], 0.0, +1, 0), co), ((0.5, sy[1], 4.5, +1, 0), co),
             ((-0.5, sy[1], 2.5, -1, 0), ci), ((-0.5, sy[0], 7.0, -1, 0), ci)]
    frames = []
    for i in range(N):
        lay = Image.new("RGB", (W, H)); dr = ImageDraw.Draw(lay)
        wall(dr, i, colored)
        if not colored:
            seg(dr, (0, T3.floor_y(KEY, 0), ZN), (0, T3.floor_y(KEY, 0), ZF), TEAL, 0.22, 1, 28)
        out = refq(glow(lay, BG))
        if colored:
            dl = Image.new("RGB", (W, H)); dd = ImageDraw.Draw(dl)
            for g in range(8):
                a = 2*math.pi*(g+0.5)/8; col = GREEN if math.cos(a) > 0 else RED
                for k in range(int(ZF/0.6)+2):
                    z = k*0.6 - (i/N)*1.2 + 0.6
                    if z > 0.5: dot(dd, (math.cos(a), math.sin(a), z), col, 0.9, 1.7)
            out = ImageChops.add(out, ImageChops.add(dl, dl.filter(ImageFilter.GaussianBlur(2.5)).point(lambda v: v//2)))
        if not colored: T4.overlay_syms(out, i)
        fl = Image.new("RGB", (W, H)); sg_ = ImageDraw.Draw(fl)
        for sh, c in sorted(ships, key=lambda s: -ship_pos(s[0], i)[0]):
            T5.ship(out, sg_, sh, i, c)
        out = ImageChops.add(out, fl.filter(ImageFilter.GaussianBlur(5)))
        out = ImageChops.add(out, fl)
        T4.caption(out, colored)
        frames.append(out)
    pal = make_pal(colored)
    q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
    p = os.path.join(T.OUT, name)
    q[0].save(p, save_all=True, append_images=q[1:], duration=33, loop=0, disposal=1)
    im = Image.open(p); print(name, im.size, im.n_frames, os.path.getsize(p)//1024, "KB")
    sp = os.path.dirname(T.OUT)
    for n, fi in enumerate((N//3, N//3+3)):
        im.seek(fi); im.convert("RGB").save(os.path.join(sp, "prev6_%s_%d.png" % (("dots" if colored else "sym"), n)))

if __name__ == "__main__":
    render("tunnel6_rings_symbols.gif", False)
    render("tunnel6_rings_dots_muted.gif", True)
    im = Image.open(os.path.join(T.OUT, "tunnel_3_rings.gif")); im.seek(N//3); im.convert("RGB").save(os.path.join(os.path.dirname(T.OUT), "prev6_ref.png"))
