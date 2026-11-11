import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tunnels as T
from tunnels import *
import tunnels2 as T2
from PIL import ImageFont

AMBER = (0xE0, 0xB0, 0x60)
WALL = {'hex': T.g_hex, 'gates': T.g_gates, 'rings': T.g_rings, 'corridor': T.g_corridor,
        'chevrons': T.g_chev, 'dots': T.g_dots}
# lane x extent (abs) on the floor per geometry
LANE = {'hex': (0.05, 0.95), 'gates': (0.05, 1.05), 'rings': (0.05, 0.75),
        'corridor': (0.05, 1.25), 'chevrons': (0.05, 1.05), 'dots': (0.05, 0.75)}
NUM = {k: n+1 for n, k in enumerate(WALL)}
try: F1 = ImageFont.load_default(size=13)
except TypeError: F1 = ImageFont.load_default()

def floor_y(key, x): return T2.GEO[key][1](x)

def lane_poly(key, sg, z0, z1):
    a, b = LANE[key]; xs = [sg*(a + (b-a)*j/6) for j in range(7)]
    top = [proj((x, floor_y(key, x), z1)) for x in xs]
    bot = [proj((x, floor_y(key, x), z0)) for x in xs][::-1]
    return top + bot

def overlay_strip(out, key, sg, col, k):
    ov = Image.new('RGBA', (W, H), (0,0,0,0)); d = ImageDraw.Draw(ov)
    nz = 24; zs_ = [0.5 + (ZF-0.5)*j/nz for j in range(nz+1)]
    for j in range(nz):
        al = k * min(1.0, alpha((zs_[j]+zs_[j+1])/2) ** 0.5)
        P = lane_poly(key, sg, zs_[j], zs_[j+1])
        if all(P): d.polygon(P, fill=col + (int(255*al),))
    out.paste(ov, (0,0), ov)

def overlay_syms(out, key, i):
    ov = Image.new('RGBA', (W, H), (0,0,0,0)); d = ImageDraw.Draw(ov)
    a, b = LANE[key]; xc = (a+b)/2; SP = 1.8; w, l = 0.13, 0.2
    for sg, dd in ((-1, +1), (+1, -1)):
        x0 = sg*xc; yc = floor_y(key, x0)
        for kk in range(-3, int(ZF/SP)+3):
            z = kk*SP + dd*(i/N)*5*SP
            if z < 0.6: continue
            al = alpha(z) * 0.4
            if al <= 0.02: continue
            P3 = [(x0, z+dd*l), (x0+w, z-dd*l), (x0, z-dd*l*0.3), (x0-w, z-dd*l)]
            P = [proj((x, yc, zz)) for x, zz in P3]
            if all(P): d.polygon(P, fill=TEAL + (int(255*al),))
    out.paste(ov, (0,0), ov)

def caption(out, legend):
    d = ImageDraw.Draw(out); dim = (110, 125, 130)
    d.text((W-170, H-24), "wall flow = your speed", fill=dim, font=F1)
    if legend:
        for r, (t, c) in enumerate((("OUT", CYAN), ("IN", AMBER))):
            y = 14 + r*18
            d.rectangle([16, y+2, 28, y+12], fill=sc(c, 0.55))
            d.text((34, y), t, fill=dim, font=F1)

def render(key, mode):
    name = "tunnel3_%d_%s_%s.gif" % (NUM[key], key, mode)
    sy = T2.GEO[key][4]
    if mode == 'color': co, ci, go, gi = CYAN, AMBER, CYAN, AMBER
    else: co = ci = TEAL; go = gi = CYAN
    ships = [((-0.5, sy[0], 0.0, +1, 0), co, go), ((-0.5, sy[1], 4.5, +1, 0), co, go),
             ((0.5, sy[1], 2.5, -1, 0), ci, gi), ((0.5, sy[0], 7.0, -1, 0), ci, gi)]
    frames = []
    for i in range(N):
        lay = Image.new("RGB", (W, H)); dr = ImageDraw.Draw(lay)
        WALL[key](dr, i)
        if mode == 'symbols':
            for sg_ in (-1, 1):
                pass
            seg(dr, (0, floor_y(key, 0), ZN), (0, floor_y(key, 0), ZF), TEAL, 0.22, 1, 28)
        out = glow(lay, BG)
        if mode == 'color':
            overlay_strip(out, key, -1, CYAN, 0.30); overlay_strip(out, key, +1, AMBER, 0.30)
        else:
            overlay_syms(out, key, i)
        fl = Image.new("RGB", (W, H)); sg_ = ImageDraw.Draw(fl)
        for sh, c, g in sorted(ships, key=lambda s: -ship_pos(s[0], i)[0]):
            T2.ship2(out, sg_, sh, i, c, g)
        out = ImageChops.add(out, fl.filter(ImageFilter.GaussianBlur(5)))
        out = ImageChops.add(out, fl)
        caption(out, mode == 'color')
        frames.append(out)
    tile = Image.new("RGB", (W, H))
    for n, fi in enumerate((0, N//4, N//2, 3*N//4)):
        tile.paste(frames[fi].crop((240,135,720,405)), ((n % 2)*W//2, (n//2)*H//2))
    pal = tile.quantize(colors=int(os.environ.get("PC",14)), method=Image.Quantize.MEDIANCUT)
    q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
    p = os.path.join(T.OUT, name)
    q[0].save(p, save_all=True, append_images=q[1:], duration=33, loop=0, disposal=1)
    im = Image.open(p); print(name, im.size, im.n_frames, os.path.getsize(p)//1024, "KB")
    frames[N//3].save(os.path.join(os.path.dirname(T.OUT), "prev3_%s_%s.png" % (key, mode)))
    frames[N//3+1].save(os.path.join(os.path.dirname(T.OUT), "prev3b_%s_%s.png" % (key, mode)))

if __name__ == "__main__":
    for k in (sys.argv[1:] or list(WALL)):
        for m in ('color', 'symbols'): render(k, m)
