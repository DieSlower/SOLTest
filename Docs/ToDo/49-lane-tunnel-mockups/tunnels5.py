import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tunnels as T
from tunnels import *
import tunnels2 as T2
import tunnels3 as T3
import tunnels4 as T4

GREEN, RED = T4.GREEN, T4.RED
KEY = 'rings'

def wall(dr, i, colored):
    # rings (teal-white) plus the 8 longitudinal guide lines of dots
    SP = 1.4; off = (i/N)*2*SP
    for k in range(-1, int(ZF/SP)+2):
        z = k*SP - off + SP
        if z > 0.5:
            poly(dr, [(math.cos(2*math.pi*j/48), math.sin(2*math.pi*j/48), z) for j in range(48)], TEAL, 1.0, 2)
    DS = 0.6; off2 = (i/N)*DS*2
    for g in range(8):
        a = 2*math.pi*(g+0.5)/8
        right = math.cos(a) > 0
        col, kk, r = ((GREEN if right else RED), 1.0, 1.9) if colored else (CYAN, 0.7, 1.2)
        for k in range(int(ZF/DS)+2):
            z = k*DS - off2 + DS
            if z > 0.5: dot(dr, (math.cos(a), math.sin(a), z), col, kk, r)

def ship(out, dglow, sh, i, col):
    # strongly tinted hull, lane-coloured outline and engine glow
    x, y, z0, s, ph = sh
    z, fa = ship_pos(sh, i)
    L, Wg = 0.8, 0.38
    V = {'nose': (x,y,z+s*L), 'wl': (x-Wg,y,z-s*L*0.6), 'wr': (x+Wg,y,z-s*L*0.6),
         'tail': (x,y,z-s*L*0.2), 'fin': (x,y-0.3,z-s*L*0.5), 'eng': (x,y,z-s*L*0.45)}
    P = {k: proj_s(v) for k, v in V.items()}
    if any(v is None for v in P.values()) or fa < 0.05: return
    faces = [('nose','wl','tail'), ('nose','tail','wr'), ('nose','fin','tail')]
    shades = [0.75, 0.55, 0.38]
    ov = Image.new('RGBA', (W, H), (0,0,0,0)); df = ImageDraw.Draw(ov)
    for f in sorted(range(3), key=lambda f: -sum(V[k][2] for k in faces[f])):
        df.polygon([P[k] for k in faces[f]], fill=sc(col, shades[f])+(255,))
        df.line([P[k] for k in faces[f]] + [P[faces[f][0]]], fill=sc(col, 1.0)+(255,), width=2)
    ov.putalpha(ov.getchannel('A').point(lambda v: int(v*fa)))
    out.paste(ov, (0,0), ov)
    e = P['eng']; er = max(3, 0.09*F/V['eng'][2])
    tr = proj_s((x, y, z - s*L*0.45 - s*1.2))
    if tr: dglow.line([e, tr], fill=sc(col, 0.9*fa), width=3)
    dglow.ellipse([e[0]-er, e[1]-er, e[0]+er, e[1]+er], fill=sc(tuple((c+255)//2 for c in col), fa))

def make_pal(colored):
    # fixed ramps per hue so lane colours survive quantisation
    cols = [(4,6,14),(8,14,28),(12,22,40),(60,66,90),(110,125,130),(255,255,255)]
    bases = [TEAL, CYAN] if not colored else [TEAL, GREEN, RED]
    lv = [0.2, 0.4, 0.6, 0.8, 1.0]
    for b in bases: cols += [sc(b, k) for k in lv]
    if colored: cols += [sc((200,255,220),0.9), sc((255,200,200),0.9)]
    flat = [v for c in cols for v in c]
    flat += [0]*(768-len(flat))
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
        out = glow(lay, BG)
        if not colored: T4.overlay_syms(out, i)
        fl = Image.new("RGB", (W, H)); sg_ = ImageDraw.Draw(fl)
        for sh, c in sorted(ships, key=lambda s: -ship_pos(s[0], i)[0]):
            ship(out, sg_, sh, i, c)
        out = ImageChops.add(out, fl.filter(ImageFilter.GaussianBlur(5)))
        out = ImageChops.add(out, fl)
        T4.caption(out, colored)
        frames.append(out)
    tile = Image.new("RGB", (W, H))
    for n, fi in enumerate((0, N//4, N//2, 3*N//4)):
        tile.paste(frames[fi].crop((240,135,720,405)), ((n % 2)*W//2, (n//2)*H//2))
    pal = make_pal(colored)
    q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
    p = os.path.join(T.OUT, name)
    q[0].save(p, save_all=True, append_images=q[1:], duration=33, loop=0, disposal=1)
    im = Image.open(p); print(name, im.size, im.n_frames, os.path.getsize(p)//1024, "KB")
    sp = os.path.dirname(T.OUT)
    for n, fi in enumerate((N//3, N//3+3)):
        im.seek(fi); im.convert("RGB").save(os.path.join(sp, "prev5_%s_%d.png" % (("dots" if colored else "sym"), n)))

if __name__ == "__main__":
    render("tunnel5_rings_symbols_guides.gif", False)
    render("tunnel5_rings_dots_green_red.gif", True)
