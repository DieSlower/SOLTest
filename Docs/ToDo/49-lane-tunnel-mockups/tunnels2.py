import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tunnels as T
from tunnels import *
from PIL import ImageFont

OUTC = (0x3F, 0xD0, 0xE0); INC = (0xE0, 0xB0, 0x60); DIVC = (215, 235, 240)
def tint(c): return tuple(int(v + (255 - v) * 0.4) for v in c)

def zs(SP, i, d, mult=2):
    off = (i / N) * mult * SP
    for k in range(-3, int(ZF / SP) + 3):
        z = k * SP + d * off
        if z > 0.5: yield k, z

def hexhalf(z, sg):
    v = hexv(z); ids = [5, 0, 1] if sg > 0 else [4, 3, 2]
    return [(0, -0.896, z)] + [v[k] for k in ids] + [(0, 0.896, z)]

def g_hex(dr, i, L, m):
    sg, d = L['sg'], L['d']
    for k, z in zs(1.2, i, d): poly(dr, hexhalf(z, sg), L['main'], m, 2, closed=False)
    ids = [5, 0, 1] if sg > 0 else [4, 3, 2]
    for k in ids: seg(dr, hexv(ZN)[k], hexv(ZF)[k], L['acc'], 0.8*m, 2, 28)

def g_gates(dr, i, L, m):
    sg, d = L['sg'], L['d']; R = 1.1
    for k, z in zs(2.4, i, d):
        poly(dr, [(0,-R,z),(sg*R,-R,z),(sg*R,R,z),(0,R,z)], L['main'], 0.3*m, 1, closed=False)
        for yy in (-R, R):
            a = (sg*R, yy, z)
            seg(dr, a, (sg*R*0.4, yy, z), L['acc'], m, 4)
            seg(dr, a, (sg*R, yy*0.4, z), L['acc'], m, 4)

def g_rings(dr, i, L, m):
    sg, d = L['sg'], L['d']
    a0 = -math.pi/2 if sg > 0 else math.pi/2
    for k, z in zs(1.4, i, d):
        poly(dr, [(math.cos(a0+math.pi*j/24), math.sin(a0+math.pi*j/24), z) for j in range(25)], L['main'], m, 2, closed=False)
    for g in range(8):
        a = 2*math.pi*(g+0.5)/8
        if (math.cos(a) > 0) != (sg > 0): continue
        for k, z in zs(0.6, i, d): dot(dr, (math.cos(a), math.sin(a), z), L['acc'], 0.55*m, 1.0)

def g_corr(dr, i, L, m):
    sg, d = L['sg'], L['d']; Y = 0.8; Wd = 1.3
    for k, z in zs(1.0, i, d):
        seg(dr, (sg*Wd, Y, z), (0, Y, z), L['main'], 0.5*m, 1, 6)
        if k % 2 == 0: seg(dr, (sg*Wd, Y, z), (sg*Wd, Y-0.35, z), L['acc'], 0.9*m, 2)
    seg(dr, (sg*Wd*0.5, Y, ZN), (sg*Wd*0.5, Y, ZF), L['main'], 0.28*m, 1, 28)
    seg(dr, (sg*Wd, Y, ZN), (sg*Wd, Y, ZF), L['acc'], m, 3, 28)

def g_chev(dr, i, L, m):
    sg, d = L['sg'], L['d']
    for k, z in zs(1.5, i, d):
        poly(dr, [(0,-1.0,z+0.6),(sg*1.2,0.8,z-0.3),(0,0.35,z)], L['main'], m, 3, closed=False)

def g_dots(dr, i, L, m):
    sg, d = L['sg'], L['d']; NR = 20
    for k, z in zs(0.7, i, d):
        for j in range(NR):
            a = 2*math.pi*(j + 0.5*(k % 2))/NR
            if (((a + math.pi/2) % (2*math.pi)) < math.pi) != (sg > 0): continue
            dot(dr, (math.cos(a), math.sin(a), z), L['main'] if (j % 3) else L['acc'], m, 1.6)

# per geometry: fn, floor-y function, arrow lane centre, divider (y, brightness) list, ship ys
GEO = {
 'hex':      (g_hex,   lambda x: 0.78, 0.5,  [(0.896, 1.0), (-0.896, 0.35)], (-0.4, 0.4)),
 'gates':    (g_gates, lambda x: 1.1,  0.55, [(1.1, 1.0), (-1.1, 0.35)],     (-0.4, 0.4)),
 'rings':    (g_rings, lambda x: 0.97*math.sqrt(max(0, 1-x*x)), 0.5, [(1.0, 1.0), (-1.0, 0.35)], (-0.4, 0.4)),
 'corridor': (g_corr,  lambda x: 0.8,  0.65, [(0.8, 1.0)],                   (0.35, 0.15)),
 'chevrons': (g_chev,  lambda x: 0.85, 0.6, [(0.35, 1.0), (-1.0, 0.35)], (-0.05, 0.3)),
 'dots':     (g_dots,  lambda x: 0.97*math.sqrt(max(0, 1-x*x)), 0.5, [(1.0, 1.0), (-1.0, 0.35)], (-0.4, 0.4)),
}

def dividers(dr, key, m):
    for y, b in GEO[key][3]:
        seg(dr, (0, y, ZN), (0, y, ZF), DIVC, b, 2, 28)

def arrows(dr, i, L, key):
    fy = GEO[key][1]; xc = L['sg'] * GEO[key][2]; d = L['d']; w, l = 0.24, 0.3
    for k, z in zs(1.3, i, d, 3):
        al = alpha(z)
        if al <= 0.03: continue
        # filled arrowhead: tip, outer wings and a back notch
        P3 = [(xc, z+d*l), (xc+w, z-d*l), (xc, z-d*l*0.3), (xc-w, z-d*l)]
        yc = fy(xc); P = [proj((x, yc, zz)) for x, zz in P3]
        if all(P): dr.polygon(P, fill=sc((90, 225, 240), al))

def ship2(out, dglow, sh, i, col, gcol):
    x, y, z0, s, ph = sh
    z, fa = ship_pos(sh, i)
    L, Wg = 0.8, 0.38
    V = {'nose': (x,y,z+s*L), 'wl': (x-Wg,y,z-s*L*0.6), 'wr': (x+Wg,y,z-s*L*0.6),
         'tail': (x,y,z-s*L*0.2), 'fin': (x,y-0.3,z-s*L*0.5), 'eng': (x,y,z-s*L*0.45)}
    P = {k: proj_s(v) for k, v in V.items()}
    if any(v is None for v in P.values()) or fa < 0.05: return
    faces = [('nose','wl','tail'), ('nose','tail','wr'), ('nose','fin','tail')]
    shades = [0.30, 0.22, 0.14]
    ov = Image.new('RGBA', (W, H), (0,0,0,0)); df = ImageDraw.Draw(ov)
    for f in sorted(range(3), key=lambda f: -sum(V[k][2] for k in faces[f])):
        df.polygon([P[k] for k in faces[f]], fill=sc(col, shades[f])+(255,))
        df.line([P[k] for k in faces[f]] + [P[faces[f][0]]], fill=sc(tint(col), 0.95)+(255,), width=2)
    ov.putalpha(ov.getchannel('A').point(lambda v: int(v*fa)))
    out.paste(ov, (0,0), ov)
    e = P['eng']; er = max(3, 0.09*F/V['eng'][2])
    tr = proj_s((x, y, z - s*L*0.45 - s*1.2))
    if tr: dglow.line([e, tr], fill=sc(gcol, 0.6*fa), width=3)
    dglow.ellipse([e[0]-er, e[1]-er, e[0]+er, e[1]+er], fill=sc((220,255,255), fa))

try: FONT = ImageFont.load_default(size=18)
except TypeError: FONT = ImageFont.load_default()

def legend(out):
    d = ImageDraw.Draw(out)
    for r, (txt, c) in enumerate((("OUT", OUTC), ("IN", INC))):
        y = 18 + r*28
        d.rectangle([20, y, 40, y+18], fill=c)
        d.text((50, y), txt, fill=(225, 235, 240), font=FONT)

def render(name, key, mode, nc):
    fn, _, _, _, sy = GEO[key]
    if mode == 'color':
        LO = dict(sg=-1, d=+1, main=OUTC, acc=tint(OUTC)); LI = dict(sg=+1, d=-1, main=INC, acc=tint(INC))
        m = 1.0; sc_o, sc_i, g_o, g_i = OUTC, INC, OUTC, INC
    else:
        LO = dict(sg=-1, d=+1, main=CYAN, acc=TEAL); LI = dict(sg=+1, d=-1, main=CYAN, acc=TEAL)
        m = 0.55; sc_o = sc_i = TEAL; g_o = g_i = CYAN
    ships = [((-0.5, sy[0], 0.0, +1, 0), sc_o, g_o), ((-0.5, sy[1], 4.5, +1, 0), sc_o, g_o),
             ((0.5, sy[1], 2.5, -1, 0), sc_i, g_i), ((0.5, sy[0], 7.0, -1, 0), sc_i, g_i)]
    frames = []
    for i in range(N):
        lay = Image.new("RGB", (W, H)); dr = ImageDraw.Draw(lay)
        fn(dr, i, LO, m); fn(dr, i, LI, m); dividers(dr, key, m)
        if mode == 'arrows': arrows(dr, i, LO, key); arrows(dr, i, LI, key)
        out = glow(lay, BG)
        fl = Image.new("RGB", (W, H)); sg_ = ImageDraw.Draw(fl)
        for sh, c, g in sorted(ships, key=lambda s: -ship_pos(s[0], i)[0]):
            ship2(out, sg_, sh, i, c, g)
        out = ImageChops.add(out, fl.filter(ImageFilter.GaussianBlur(5)))
        out = ImageChops.add(out, fl)
        if mode == 'color': legend(out)
        frames.append(out)
    tile = Image.new("RGB", (W, H))
    for n, fi in enumerate((0, N//4, N//2, 3*N//4)):
        tile.paste(frames[fi].resize((W//2, H//2)), ((n % 2)*W//2, (n//2)*H//2))
    pal = tile.quantize(colors=nc, method=Image.Quantize.MEDIANCUT)
    q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
    p = os.path.join(T.OUT, name)
    q[0].save(p, save_all=True, append_images=q[1:], duration=33, loop=0, disposal=1)
    im = Image.open(p); print(name, im.size, im.n_frames, os.path.getsize(p)//1024, "KB")
    frames[N//3].save(os.path.join(os.path.dirname(T.OUT), "prev2_%s_%s.png" % (key, mode)))

if __name__ == "__main__":
    keys = sys.argv[1:] or list(GEO)
    nums = {k: n+1 for n, k in enumerate(GEO)}
    for k in keys:
        render("tunnel_%d_%s_color.gif" % (nums[k], k), k, 'color', 28)
        render("tunnel_%d_%s_arrows.gif" % (nums[k], k), k, 'arrows', 24)
