import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tunnels as T
from tunnels import *
import tunnels2 as T2
import tunnels3 as T3
from PIL import ImageFont

GREEN = (0x40, 0xD0, 0x70); RED = (0xE0, 0x48, 0x48)
KEY = 'rings'

def overlay_syms(out, i):
    # chevrons: outbound (moving away) on the right lane, inbound on the left
    ov = Image.new('RGBA', (W, H), (0,0,0,0)); d = ImageDraw.Draw(ov)
    a, b = T3.LANE[KEY]; xc = (a+b)/2; SP = 1.8; w, l = 0.13, 0.2
    for sg, dd in ((+1, +1), (-1, -1)):
        x0 = sg*xc; yc = T3.floor_y(KEY, x0)
        for kk in range(-3, int(ZF/SP)+3):
            z = kk*SP + dd*(i/N)*5*SP
            if z < 0.6: continue
            al = alpha(z) * 0.4
            if al <= 0.02: continue
            P3 = [(x0, z+dd*l), (x0+w, z-dd*l), (x0, z-dd*l*0.3), (x0-w, z-dd*l)]
            P = [proj((x, yc, zz)) for x, zz in P3]
            if all(P): d.polygon(P, fill=TEAL + (int(255*al),))
    out.paste(ov, (0,0), ov)

def rings_wall(dr, i, dots):
    SP = 1.4; off = (i/N)*2*SP
    for k in range(-1, int(ZF/SP)+2):
        z = k*SP - off + SP
        if z <= 0.5: continue
        poly(dr, [(math.cos(2*math.pi*j/48), math.sin(2*math.pi*j/48), z) for j in range(48)], CYAN, 1.0, 2)
        if dots:
            for sg, col in ((+1, GREEN), (-1, RED)):
                for a in range(-75, 80, 25):
                    r = math.radians(a)
                    dot(dr, (sg*math.cos(r), math.sin(r), z), col, 1.0, 2.2)

def caption(out, legend):
    d = ImageDraw.Draw(out); dim = (110, 125, 130)
    d.text((W-170, H-24), "wall flow = your speed", fill=dim, font=T3.F1)
    if legend:
        for r, (t, c) in enumerate((("OUT", GREEN), ("IN", RED))):
            y = 14 + r*18
            d.rectangle([16, y+2, 28, y+12], fill=sc(c, 0.5))
            d.text((34, y), t, fill=(95, 108, 112), font=T3.F1)

def render(name, dots):
    sy = T2.GEO[KEY][4]
    co, go = (GREEN, GREEN) if dots else (TEAL, CYAN)
    ci, gi = (RED, RED) if dots else (TEAL, CYAN)
    ships = [((0.5, sy[0], 0.0, +1, 0), co, go), ((0.5, sy[1], 4.5, +1, 0), co, go),
             ((-0.5, sy[1], 2.5, -1, 0), ci, gi), ((-0.5, sy[0], 7.0, -1, 0), ci, gi)]
    frames = []
    for i in range(N):
        lay = Image.new("RGB", (W, H)); dr = ImageDraw.Draw(lay)
        rings_wall(dr, i, dots)
        if not dots:
            seg(dr, (0, T3.floor_y(KEY, 0), ZN), (0, T3.floor_y(KEY, 0), ZF), TEAL, 0.22, 1, 28)
        out = glow(lay, BG)
        if not dots: overlay_syms(out, i)
        fl = Image.new("RGB", (W, H)); sg_ = ImageDraw.Draw(fl)
        for sh, c, g in sorted(ships, key=lambda s: -ship_pos(s[0], i)[0]):
            T2.ship2(out, sg_, sh, i, c, g)
        out = ImageChops.add(out, fl.filter(ImageFilter.GaussianBlur(5)))
        out = ImageChops.add(out, fl)
        caption(out, dots)
        frames.append(out)
    tile = Image.new("RGB", (W, H))
    for n, fi in enumerate((0, N//4, N//2, 3*N//4)):
        tile.paste(frames[fi].crop((240,135,720,405)), ((n % 2)*W//2, (n//2)*H//2))
    pal = tile.quantize(colors=int(os.environ.get("PC", 16 if dots else 24)), method=Image.Quantize.MEDIANCUT)
    q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
    p = os.path.join(T.OUT, name)
    q[0].save(p, save_all=True, append_images=q[1:], duration=33, loop=0, disposal=1)
    im = Image.open(p); print(name, im.size, im.n_frames, os.path.getsize(p)//1024, "KB")
    sp = os.path.dirname(T.OUT)
    for n, fi in enumerate((N//3, N//3+3)):
        frames[fi].save(os.path.join(sp, "prev4_%s_%d.png" % (("dots" if dots else "sym"), n)))

if __name__ == "__main__":
    render("tunnel4_rings_symbols_right.gif", False)
    render("tunnel4_rings_dots_green_red.gif", True)
