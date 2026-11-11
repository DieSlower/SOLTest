import math, os, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops

W, H = 960, 540
N = int(os.environ.get("NF", 60))
CX, CY, F = W/2, H/2 - 10, 430.0
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tunnel_examples")
os.makedirs(OUT, exist_ok=True)
TEAL = (0x9F, 0xD6, 0xD0); CYAN = (0x3F, 0xD0, 0xE0)
ZN, ZF = 1.0, 13.0
PS = 1.7

def sc(c, k): return tuple(max(0, min(255, int(v * k))) for v in c)

def make_bg():
    img = Image.new("RGB", (W, H)); px = img.load()
    for y in range(H):
        for x in range(W):
            k = max(0, 1 - math.hypot(x-CX, y-CY)/620)
            px[x, y] = (int(4+6*k), int(6+12*k), int(14+22*k))
    r = random.Random(11); dr = ImageDraw.Draw(img)
    for _ in range(60):
        x, y = r.randrange(W), r.randrange(H); b = r.randrange(50, 120)
        dr.point((x, y), fill=(b, b, min(255, b+30)))
    return img
BG = make_bg()

def proj(p):
    x, y, z = p
    if z < 0.2: return None
    return (CX + F*x*PS/z, CY + F*y*PS/z)

def proj_s(p):
    x, y, z = p
    if z < 0.2: return None
    return (CX + F*x/z, CY + F*y/z)

def alpha(z):
    a = min(1.0, max(0.0, (z-0.6)/1.2))
    return a * max(0.0, 1 - z/ZF) ** 1.1

def seg(dr, p0, p1, col, k=1.0, w=2, pieces=1):
    # 3D segment, split so alpha follows depth
    for j in range(pieces):
        a = [p0[m] + (p1[m]-p0[m])*j/pieces for m in range(3)]
        b = [p0[m] + (p1[m]-p0[m])*(j+1)/pieces for m in range(3)]
        al = alpha((a[2]+b[2])/2) * k
        if al <= 0.02: continue
        pa, pb = proj(a), proj(b)
        if pa and pb: dr.line([pa, pb], fill=sc(col, al), width=w)

def poly(dr, pts, col, k=1.0, w=2, closed=True, pieces=1):
    n = len(pts)
    for j in range(n if closed else n-1):
        seg(dr, pts[j], pts[(j+1) % n], col, k, w, pieces)

def dot(dr, p, col, k=1.0, r=1.0):
    al = alpha(p[2]) * k
    pp = proj(p)
    if al <= 0.02 or not pp: return
    rr = max(1.0, r * F * PS / p[2] / 100)
    dr.ellipse([pp[0]-rr, pp[1]-rr, pp[0]+rr, pp[1]+rr], fill=sc(col, al))

# ---------------- ships
def ship_pos(sh, i):
    x, y, z0, s, ph = sh
    zmin, zmax = 2.0, 11.0; rng = zmax - zmin
    z = zmin + (z0 + s * (i/N) * rng) % rng
    edge = min(z - zmin, zmax - z)
    return z, min(1.0, edge/1.5)

def ship_draw(out, dglow, sh, i):
    x, y, z0, s, ph = sh
    z, fa = ship_pos(sh, i)
    L, Wg = 0.8, 0.5
    V = {
     'nose': (x, y, z + s*L), 'wl': (x-Wg, y, z - s*L*0.6), 'wr': (x+Wg, y, z - s*L*0.6),
     'tail': (x, y, z - s*L*0.2), 'fin': (x, y-0.3, z - s*L*0.5),
     'eng': (x, y, z - s*L*0.45)}
    P = {k: proj_s(v) for k, v in V.items()}
    if any(v is None for v in P.values()): return
    faces = [('nose','wl','tail'), ('nose','tail','wr'), ('nose','fin','tail')]
    shades = [0.30, 0.22, 0.14]
    if fa < 0.05: return
    ov = Image.new('RGBA', (W, H), (0,0,0,0)); dfill = ImageDraw.Draw(ov)
    order = sorted(range(3), key=lambda f: -sum(V[k][2] for k in faces[f]))
    for f in order:
        dfill.polygon([P[k] for k in faces[f]], fill=sc(TEAL, shades[f])+(255,))
        dfill.line([P[k] for k in faces[f]] + [P[faces[f][0]]], fill=sc(TEAL, 0.95)+(255,), width=2)
    ov.putalpha(ov.getchannel('A').point(lambda v: int(v*fa)))
    out.paste(ov, (0,0), ov)
    e = P['eng']; er = max(3, 0.09*F/V['eng'][2])
    tr = proj_s((x, y, z - s*L*0.45 - s*1.2))
    if tr: dglow.line([e, tr], fill=sc(CYAN, 0.55*fa), width=3)
    dglow.ellipse([e[0]-er, e[1]-er, e[0]+er, e[1]+er], fill=sc((200, 255, 255), fa))

# ---------------- geometries
def hexv(z, R=1.0): return [(R*math.cos(math.pi/3*k+0.26), R*math.sin(math.pi/3*k+0.26), z) for k in range(6)]

def g_hex(dr, i):
    SP = 1.2; off = (i/N)*2*SP
    for k in range(-1, int(ZF/SP)+2):
        z = k*SP - off + SP
        if z > 0.5: poly(dr, hexv(z), CYAN, 1.0, 2)
    for k in range(6):
        a, b = hexv(ZN)[k], hexv(ZF)[k]
        seg(dr, a, b, TEAL, 0.8, 2, 28)

def g_gates(dr, i):
    SP = 2.4; off = (i/N)*2*SP
    for k in range(-1, int(ZF/SP)+2):
        z = k*SP - off + SP
        if z <= 0.5: continue
        R = 1.1
        c = [(-R,-R,z),(R,-R,z),(R,R,z),(-R,R,z)]
        poly(dr, c, CYAN, 0.28, 1)
        for j in range(4):
            a, b, d = c[j], c[(j+1)%4], c[(j-1)%4]
            u = [(b[m]-a[m])*0.3 for m in range(3)]; v = [(d[m]-a[m])*0.3 for m in range(3)]
            seg(dr, a, [a[m]+u[m] for m in range(3)], TEAL, 1.0, 4)
            seg(dr, a, [a[m]+v[m] for m in range(3)], TEAL, 1.0, 4)

def g_rings(dr, i):
    SP = 1.4; off = (i/N)*2*SP
    for k in range(-1, int(ZF/SP)+2):
        z = k*SP - off + SP
        if z > 0.5:
            poly(dr, [(math.cos(2*math.pi*j/48), math.sin(2*math.pi*j/48), z) for j in range(48)], CYAN, 1.0, 2)
    DS = 0.6; off2 = (i/N)*DS*2
    for g in range(8):
        a = 2*math.pi*(g+0.5)/8
        for k in range(int(ZF/DS)+2):
            z = k*DS - off2 + DS
            if z > 0.5: dot(dr, (math.cos(a), math.sin(a), z), TEAL, 0.55, 1.0)

def g_corridor(dr, i):
    Y = 0.8; Wd = 1.3
    SP = 1.0; off = (i/N)*2*SP
    for k in range(-1, int(ZF/SP)+2):
        z = k*SP - off + SP
        if z > 0.5:
            seg(dr, (-Wd, Y, z), (Wd, Y, z), CYAN, 0.5, 1, 6)
            for sx in (-Wd, Wd):
                if k % 2 == 0: seg(dr, (sx, Y, z), (sx, Y-0.35, z), TEAL, 0.9, 2)
    for x in (-Wd*0.5, 0, Wd*0.5):
        seg(dr, (x, Y, ZN), (x, Y, ZF), CYAN, 0.28, 1, 28)
    for sx in (-Wd, Wd):
        seg(dr, (sx, Y, ZN), (sx, Y, ZF), TEAL, 1.0, 3, 28)

def g_chev(dr, i):
    SP = 1.5; off = (i/N)*2*SP
    for k in range(-1, int(ZF/SP)+2):
        z = k*SP - off + SP
        if z <= 0.5: continue
        pts = [(0, -1.0, z+0.6), (1.2, 0.8, z-0.3), (0, 0.35, z), (-1.2, 0.8, z-0.3)]
        poly(dr, pts, CYAN, 1.0, 3)

def g_dots(dr, i):
    DS = 0.7; off = (i/N)*DS*2; NR = 20
    for k in range(int(ZF/DS)+3):
        z = k*DS - off + DS
        if z <= 0.5: continue
        for j in range(NR):
            a = 2*math.pi*(j + 0.5*(k % 2))/NR
            dot(dr, (math.cos(a), math.sin(a), z), CYAN if (j % 3) else TEAL, 1.0, 1.6)

SHIPS = {
 'hex':   [(0.8,-0.5,0,+1,0),(-0.8,0.5,5,+1,0),(0.7,0.6,3,-1,0),(-0.7,-0.5,7,-1,0)],
 'gates': [(0.9,-0.5,0,+1,0),(-0.9,0.5,5,+1,0),(0.8,0.6,3,-1,0),(-0.8,-0.6,7,-1,0)],
 'rings': [(0.9,-0.6,0,+1,0),(-0.9,0.5,5,+1,0),(0.8,0.6,3,-1,0),(-0.8,-0.6,7,-1,0)],
 'corr':  [(1.1,0.9,0,+1,0),(-1.2,0.8,5,+1,0),(1.2,0.5,3,-1,0),(-0.7,0.3,7,-1,0)],
 'chev':  [(0.7,0.4,0,+1,0),(-0.7,0.5,5,+1,0),(0.6,0.0,3,-1,0),(-0.5,0.6,7,-1,0)],
 'dots':  [(0.8,-0.6,0,+1,0),(-0.9,0.5,5,+1,0),(0.8,0.6,3,-1,0),(-0.8,-0.6,7,-1,0)],
}

def glow(layer, base):
    g1 = layer.filter(ImageFilter.GaussianBlur(2.5)); g2 = layer.filter(ImageFilter.GaussianBlur(9))
    out = ImageChops.add(base, g2); out = ImageChops.add(out, g1); return ImageChops.add(out, layer)

def render(name, fn, key, nc=20):
    frames = []
    for i in range(N):
        lay = Image.new("RGB", (W, H)); dr = ImageDraw.Draw(lay)
        fn(dr, i)
        out = glow(lay, BG)
        fl = Image.new("RGB", (W, H)); sg = ImageDraw.Draw(fl)
        ships = sorted(SHIPS[key], key=lambda s: -ship_pos(s, i)[0])
        for sh in ships:
            ship_draw(out, sg, sh, i)
        out = ImageChops.add(out, fl.filter(ImageFilter.GaussianBlur(5)))
        out = ImageChops.add(out, fl)
        frames.append(out)
    pal = frames[N//2].quantize(colors=nc, method=Image.Quantize.MEDIANCUT)
    q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
    p = os.path.join(OUT, name)
    q[0].save(p, save_all=True, append_images=q[1:], duration=33, loop=0, disposal=1)
    im = Image.open(p); print(name, im.size, im.n_frames, os.path.getsize(p)//1024, "KB")
    frames[N//3].save(os.path.join(os.path.dirname(OUT), "prev_" + key + ".png"))

if __name__ == "__main__":
    jobs = [("tunnel_1_hex.gif", g_hex, 'hex'), ("tunnel_2_gates.gif", g_gates, 'gates'),
            ("tunnel_3_rings.gif", g_rings, 'rings'), ("tunnel_4_corridor.gif", g_corridor, 'corr'),
            ("tunnel_5_chevrons.gif", g_chev, 'chev'), ("tunnel_6_dots.gif", g_dots, 'dots')]
    for n, f, k in jobs: render(n, f, k)
