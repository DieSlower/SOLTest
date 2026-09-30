# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Bakes the AT-HYG catalog into the star-field runtime data (sub-part 4a of Part 4 / issue #5, SDD 5 §3.1 step 2):
#   bright stars (V < 8.0)  -> a compact binary list of direction, magnitude, flux and linear RGB (sprites, decision 3)
#   faint stars  (V >= 8.0) -> flux splatted onto a 6 x 2048 x 2048 HDR cubemap (decisions 6 and 7)
#
# Source data (see fetch_athyg.py, which downloads it):
#   AT-HYG v4.0, https://codeberg.org/astronexus/athyg, file data/athyg_40.csv.gz
#   (download: https://codeberg.org/astronexus/athyg/media/branch/main/data/athyg_40.csv.gz,
#    sha256 69ad04dd33d7c7bb4f5e1b4682798075811547ea9fb8d0e802e5b319c46818a6)
#   License CC BY-SA 4.0 (http://creativecommons.org/licenses/by-sa/4.0/). Attribution: "AT-HYG star catalog,
#   David Nash / astronexus, https://codeberg.org/astronexus/athyg, CC BY-SA 4.0". Every output of this script is a
#   derivative work and is distributed under the same license (SDD 5 decision 1).
#
# Columns used (header confirmed 2026-09-30, 34 columns: id,tyc,gaia,hyg,hip,hd,hr,gl,bayer,flam,con,proper,ra,dec,
# pos_src,dist,x0,y0,z0,dist_src,mag,absmag,ci,mag_src,rv,rv_src,pmra,pmdec,pm_src,vx,vy,vz,spect,spect_src):
#   ra       right ascension, J2000/ICRS, HOURS (0-24). Checked: atan2(y0, x0) matches ra*15 deg to ~3e-5 relative.
#   dec      declination, J2000/ICRS, degrees
#   dist     distance, parsecs (only used to reject row 1, the Sun, at 4.85e-6 pc)
#   mag      apparent magnitude. mag_src "T" (99.9% of rows) is the Tycho-2 VT magnitude; "GJ" is Johnson V
#   ci       color index. For mag_src "T" it is the Tycho BT-VT difference (not B-V: Aldebaran reads 1.777 against a
#            true B-V of 1.54); for "GJ" it is Johnson B-V. Blank for ~4,000 rows, including Sirius, Vega, Arcturus
#   mag_src  photometry source, as above ("OTHER" is only the Sun)
#   spect    spectral type, used as a B-V fallback when ci is blank
#   hyg      HYG catalog id (blank for most rows), the join key into HYG below
#
# Bright-star photometry correction (HYG v4.4, https://codeberg.org/astronexus/hyg, data/hyg/CURRENT/hyg_v44.csv.gz,
# same author and CC BY-SA 4.0 license; downloaded by fetch_athyg.py). Tycho-2 VT saturates on the brightest stars
# (Sirius reads V -1.09 after the Tycho transform against a true -1.44; Arcturus 0.16 vs -0.05), and several of them
# have a blank ci. HYG's "mag" is Hipparcos-derived Johnson V and its "ci" is Johnson B-V. The join (AT-HYG "hyg" =
# HYG "id") is clean: 119,614 linked rows, no duplicate ids, median position agreement 0.04".
#   - V and B-V are replaced by HYG's for linked stars with HYG V < HYG_OVERRIDE_MAG (4.0), EXCEPT blends: another
#     AT-HYG star within BLEND_RADIUS_ARCSEC (60") carries >= BLEND_FLUX_FRACTION (10%) of the HYG flux AND HYG's V
#     is closer to the combined Tycho light of the group than to the star's own. There HYG's entry is the pair's
#     combined light while AT-HYG lists the components separately (Castor 1.58 = 1.93 + 2.97, Acrux, Porrima...), so
#     overriding would double-count; pairs HYG resolves itself (Rigil Kentaurus / Toliman) are still corrected.
#     For V >= 4 the Tycho-derived V already matches HYG (median offset 0.00).
#   - B-V is also taken from HYG for any linked star whose AT-HYG ci is blank, ahead of the spectral-type fallback.
#
# Outputs (all under the gitignored Tools/StarField/data/bake/; the format contract for the 4b import is in
# star_field_meta.json, written alongside):
#   star_field_bright.bin           bright-star list, little-endian, see BRIGHT_* below
#   star_field_cube.dds             the faint cubemap as ONE DDS cube (DX10 header, R16G16B16A16_FLOAT, faces in
#                                   D3D order +X,-X,+Y,-Y,+Z,-Z, 1 mip) -- the file the 4b import should use
#   star_field_face_<n>_<F>.hdr     the same six faces as Radiance RGBE images, for inspection or per-face import
#   star_field_meta.json            parameters, formulas, counts and the frame/face conventions
#   preview/*.png                   tone-mapped previews (per face, a cube cross, an ecliptic equirectangular map)
#
# Self-check: every run first splats synthetic single stars (interior points, just inside all 24 face edges, the 8
# cube corners, random directions) onto a 6 x 64^2 cube and requires each one's flux-weighted centroid to land within
# VERIFY_TOLERANCE_TEXELS of its true direction with its flux conserved; the bake aborts before writing if any fails.
# This catches placement bugs that the whole-cube flux-conservation check cannot.
#
# Idempotent: the parsed catalogs are cached in data/athyg_parsed.npz (keyed on PARSE_VERSION and both CSVs' sizes and
# mtimes) and the bake is skipped when every output is newer than both CSVs and this script. Every output is written to
# a ".part" file and renamed into place, so an interrupted run never leaves a truncated file that looks current.
# Pass --rebuild to redo everything.
# Run: python Tools/StarField/bake_star_field.py [--rebuild] [--include-bright-in-cube]
import argparse
import csv
import json
import math
import re
import struct
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
DATA_DIR = HERE / "data"
CSV_PATH = DATA_DIR / "athyg_40.csv"
HYG_CSV_PATH = DATA_DIR / "hyg_v44.csv"
CACHE_PATH = DATA_DIR / "athyg_parsed.npz"
OUT_DIR = DATA_DIR / "bake"
PREVIEW_DIR = DATA_DIR / "preview"

# Bump whenever parse_csv / parse_hyg or the SPECTRAL_* fallback changes, so a stale parse cache is rebuilt
PARSE_VERSION = 2

# Bake parameters (SDD 5 decision 7)
MAG_CUTOFF = 8.0
FACE_SIZE = 2048

# Flux reference: flux = 10^(-0.4 (V - FLUX_REF_MAG)), so a star exactly at the cutoff has flux 1.0 in both tiers
FLUX_REF_MAG = MAG_CUTOFF

# J2000 mean obliquity of the ecliptic, IAU 2006 (Hilton et al. 2006, Celest. Mech. 94, 351): 84381.406 arcsec
OBLIQUITY_DEG = 84381.406 / 3600.0

# Tycho -> Johnson transforms, ESA 1997, "The Hipparcos and Tycho Catalogues", Vol. 1, §1.3, Appendix 4, eq. 1.3.20:
#   V = VT - 0.090 (BT - VT),  B - V = 0.850 (BT - VT)  (stated valid for -0.2 < BT - VT < 2.0)
# BT-VT is clamped to TYCHO_CI_CLAMP first so noisy faint-star colors (the file has values up to 6.6) can't drag V.
TYCHO_V_COEF = 0.090
TYCHO_BV_COEF = 0.850
TYCHO_CI_CLAMP = (-0.3, 2.4)

# B-V is clamped to the stellar range of Ballesteros' fit before converting to temperature
BV_CLAMP = (-0.4, 2.0)

# Mean B-V by spectral class letter, used only when ci is blank. Main-sequence values rounded from
# Pecaut & Mamajek 2013 (ApJS 208, 9), Table 5 (O5 -0.32, B5 -0.16, A5 0.14, F5 0.44, G5 0.68, K5 1.15, M3 1.5)
SPECTRAL_BV = {"O": -0.32, "B": -0.16, "A": 0.14, "F": 0.44, "G": 0.68, "K": 1.15, "M": 1.50}
SPECTRAL_RE = re.compile(r"[OBAFGKM]")

# HYG photometry override (see the header): magnitude limit, blend search radius, blend flux fraction
HYG_OVERRIDE_MAG = 4.0
BLEND_RADIUS_ARCSEC = 60.0
BLEND_FLUX_FRACTION = 0.1

# Placement self-check: cube size and the allowed centroid error, in local texel widths
VERIFY_FACE_SIZE = 64
VERIFY_TOLERANCE_TEXELS = 0.1

# Bright-star record: 8 little-endian float32 = dir_x, dir_y, dir_z (unit, ecliptic J2000, right-handed), V, flux, R, G, B
BRIGHT_MAGIC = b"SOLSTARB"
BRIGHT_VERSION = 1
BRIGHT_HEADER = struct.Struct("<8sIII")  # magic, version, record count, floats per record
BRIGHT_FIELDS = ["dir_x", "dir_y", "dir_z", "vmag", "flux", "r", "g", "b"]

# Cube face names in D3D / Unreal TextureCube order
FACE_NAMES = ["PX", "NX", "PY", "NY", "PZ", "NZ"]

# Photometry source codes stored in the parse cache
SRC_TYCHO, SRC_GJ, SRC_OTHER = 0, 1, 2


# Prints a tagged progress line
def log(message):
    print("[bake_star_field] " + message, flush=True)


# Context manager yielding a sibling ".part" path to write; renames it over the target only if the block succeeds
class atomic_output:
    # Remembers the final and temporary paths
    def __init__(self, path):
        self.path = Path(path)
        self.temp_path = self.path.with_name(self.path.name + ".part")

    # Hands the temporary path to the writer
    def __enter__(self):
        return self.temp_path

    # Commits on success, discards the partial file on failure
    def __exit__(self, exc_type, exc, traceback):
        if exc_type is None:
            self.temp_path.replace(self.path)
        else:
            self.temp_path.unlink(missing_ok=True)
        return False


# ---------------------------------------------------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------------------------------------------------

# Parses a float field; blank or malformed text becomes NaN
def to_float(text):
    try:
        return float(text) if text else math.nan
    except ValueError:
        return math.nan


# Returns the fallback B-V for a spectral-type string, or NaN if it has no recognisable class letter
def spectral_bv(spect):
    match = SPECTRAL_RE.search(spect or "")
    return SPECTRAL_BV[match.group(0)] if match else math.nan


# Reads the needed columns of the CSV into numpy arrays, counting rows too malformed to use
def parse_csv():
    log("parsing " + CSV_PATH.name + " (one-time; cached afterwards)")
    started = time.time()
    ra, dec, dist, mag, ci, src, spect_bv, hyg_id = [], [], [], [], [], [], [], []
    malformed = 0
    with open(CSV_PATH, "r", encoding="utf-8", newline="") as stream:
        reader = csv.reader(stream)
        header = next(reader)
        col = {name: index for index, name in enumerate(header)}
        required = ["ra", "dec", "dist", "mag", "ci", "mag_src", "spect", "hyg"]
        missing = [name for name in required if name not in col]
        if missing:
            raise RuntimeError("CSV is missing expected columns: %s (header: %s)" % (missing, header))
        i_ra, i_dec, i_dist, i_mag, i_ci, i_src, i_spect, i_hyg = (col[name] for name in required)
        width = len(header)
        source_codes = {"T": SRC_TYCHO, "GJ": SRC_GJ}

        # One pass; short or long rows are counted and skipped rather than guessed at
        for row in reader:
            if len(row) != width:
                malformed += 1
                continue
            ci_text = row[i_ci]
            ra.append(to_float(row[i_ra]))
            dec.append(to_float(row[i_dec]))
            dist.append(to_float(row[i_dist]))
            mag.append(to_float(row[i_mag]))
            ci.append(to_float(ci_text))
            src.append(source_codes.get(row[i_src], SRC_OTHER))
            spect_bv.append(math.nan if ci_text else spectral_bv(row[i_spect]))
            hyg_text = row[i_hyg]
            hyg_id.append(int(hyg_text) if hyg_text.isdigit() else -1)
    arrays = {
        "ra": np.array(ra), "dec": np.array(dec), "dist": np.array(dist), "mag": np.array(mag),
        "ci": np.array(ci), "src": np.array(src, dtype=np.int8), "spect_bv": np.array(spect_bv),
        "malformed": np.array(malformed),
    }
    log("parsed %d rows (%d malformed) in %.1f s" % (len(ra), malformed, time.time() - started))

    # Attach HYG's Johnson V and B-V to every linked row (NaN where unlinked or blank)
    hyg_mag, hyg_ci = parse_hyg()
    hyg_id = np.array(hyg_id, dtype=np.int64)
    linked = (hyg_id >= 0) & (hyg_id < len(hyg_mag))
    arrays["hyg_mag"] = np.where(linked, hyg_mag[np.where(linked, hyg_id, 0)], np.nan)
    arrays["hyg_ci"] = np.where(linked, hyg_ci[np.where(linked, hyg_id, 0)], np.nan)
    log("linked %d rows to HYG (%d with an unknown HYG id)" % (linked.sum(), ((hyg_id >= 0) & ~linked).sum()))
    return arrays


# Reads HYG's mag and ci into arrays indexed by HYG id (NaN for ids that are absent or blank)
def parse_hyg():
    log("parsing " + HYG_CSV_PATH.name)
    ids, mags, cis = [], [], []
    with open(HYG_CSV_PATH, "r", encoding="utf-8", newline="") as stream:
        reader = csv.reader(stream)
        header = next(reader)
        col = {name: index for index, name in enumerate(header)}
        missing = [name for name in ("id", "mag", "ci") if name not in col]
        if missing:
            raise RuntimeError("HYG CSV is missing expected columns: %s" % missing)
        for row in reader:
            if len(row) != len(header) or not row[col["id"]].isdigit():
                continue
            ids.append(int(row[col["id"]]))
            mags.append(to_float(row[col["mag"]]))
            cis.append(to_float(row[col["ci"]]))
    ids = np.array(ids, dtype=np.int64)
    mag = np.full(ids.max() + 1, np.nan)
    ci = np.full(ids.max() + 1, np.nan)
    mag[ids] = mags
    ci[ids] = cis
    return mag, ci


# Returns the parsed catalog, from the npz cache when PARSE_VERSION and both CSVs' size and mtime match
def load_catalog(rebuild):
    athyg, hyg = CSV_PATH.stat(), HYG_CSV_PATH.stat()
    stamp = np.array([PARSE_VERSION, athyg.st_size, int(athyg.st_mtime), hyg.st_size, int(hyg.st_mtime)],
                     dtype=np.int64)
    if not rebuild and CACHE_PATH.exists():
        # Closed on exit so Windows lets a stale cache be replaced below
        with np.load(CACHE_PATH) as cached:
            if "stamp" in cached.files and np.array_equal(cached["stamp"], stamp):
                log("using parse cache " + CACHE_PATH.name)
                return {key: cached[key] for key in cached.files if key != "stamp"}
        log("parse cache is stale; re-parsing")
    arrays = parse_csv()
    with atomic_output(CACHE_PATH) as temp_path:
        with open(temp_path, "wb") as out:
            np.savez(out, stamp=stamp, **arrays)
    return arrays


# ---------------------------------------------------------------------------------------------------------------------
# Astronomy
# ---------------------------------------------------------------------------------------------------------------------

# Converts RA (hours) / Dec (degrees) to unit vectors in the ecliptic J2000 frame, shape (n, 3)
def equatorial_to_ecliptic(ra_hours, dec_deg):
    # Equatorial unit vector: +X to the vernal equinox, +Z to the north celestial pole
    ra = np.radians(ra_hours * 15.0)
    dec = np.radians(dec_deg)
    cos_dec = np.cos(dec)
    equatorial = np.stack([cos_dec * np.cos(ra), cos_dec * np.sin(ra), np.sin(dec)], axis=1)

    # Rotate about +X by the obliquity (the equinox is shared by both frames):
    #   [x_ecl]   [1    0      0   ] [x_eq]
    #   [y_ecl] = [0  cos e  sin e ] [y_eq]
    #   [z_ecl]   [0 -sin e  cos e ] [z_eq]
    # Standard rotation, e.g. Meeus, "Astronomical Algorithms" (2nd ed.) eq. 13.1-13.2 in vector form, and the
    # Explanatory Supplement to the Astronomical Almanac (3rd ed.) §6; e = 84381.406" (IAU 2006).
    eps = math.radians(OBLIQUITY_DEG)
    rotation = np.array([[1.0, 0.0, 0.0],
                         [0.0, math.cos(eps), math.sin(eps)],
                         [0.0, -math.sin(eps), math.cos(eps)]])
    return equatorial @ rotation.T


# Returns galactic latitude (degrees) of equatorial RA/Dec, for the Milky Way sanity check
def galactic_latitude(ra_hours, dec_deg):
    # North galactic pole, J2000: RA 192.85948 deg, Dec 27.12825 deg (Hipparcos Catalogue Vol. 1 §1.5.3)
    pole_ra, pole_dec = math.radians(192.85948), math.radians(27.12825)
    ra = np.radians(ra_hours * 15.0)
    dec = np.radians(dec_deg)
    sin_b = np.sin(dec) * math.sin(pole_dec) + np.cos(dec) * math.cos(pole_dec) * np.cos(ra - pole_ra)
    return np.degrees(np.arcsin(np.clip(sin_b, -1.0, 1.0)))


# Returns Johnson V and B-V for every star, applying the Tycho transforms and the spectral-type color fallback
def johnson_photometry(catalog):
    mag, ci, src = catalog["mag"], catalog["ci"], catalog["src"]
    has_ci = np.isfinite(ci)
    tycho = (src == SRC_TYCHO) & has_ci

    # Tycho rows: mag is VT, ci is BT-VT -> V and B-V (ESA 1997 eq. 1.3.20)
    bt_vt = np.clip(np.where(tycho, ci, 0.0), *TYCHO_CI_CLAMP)
    vmag = np.where(tycho, mag - TYCHO_V_COEF * bt_vt, mag)
    bv = np.where(tycho, TYCHO_BV_COEF * bt_vt, ci)

    # No ci: HYG's Johnson B-V if linked, else the spectral class; still NaN means "no color data" (neutral white)
    hyg_ci = catalog["hyg_ci"]
    bv = np.where(has_ci, bv, np.where(np.isfinite(hyg_ci), hyg_ci, catalog["spect_bv"]))
    return vmag, bv


# Replaces V and B-V with HYG's for bright linked stars that aren't blended with a catalogued neighbour (see header);
# returns (vmag, bv, indices overridden, indices skipped as blends)
def apply_hyg_overrides(catalog, vmag, bv, directions):
    hyg_mag, hyg_ci = catalog["hyg_mag"], catalog["hyg_ci"]
    candidates = np.flatnonzero(np.isfinite(hyg_mag) & (hyg_mag < HYG_OVERRIDE_MAG))

    # Possible blend partners: any star bright enough to reach the flux fraction of the brightest candidate's limit
    partner_limit = HYG_OVERRIDE_MAG - 2.5 * math.log10(BLEND_FLUX_FRACTION)
    partners = np.flatnonzero(vmag < partner_limit)
    cos_radius = math.cos(math.radians(BLEND_RADIUS_ARCSEC / 3600.0))
    close = (directions[candidates] @ directions[partners].T) > cos_radius
    close &= candidates[:, None] != partners[None, :]

    # Only partners carrying >= BLEND_FLUX_FRACTION of the candidate's HYG flux matter
    partner_flux = 10.0 ** (-0.4 * vmag[partners])
    candidate_floor = BLEND_FLUX_FRACTION * 10.0 ** (-0.4 * hyg_mag[candidates])
    significant = close & (partner_flux[None, :] >= candidate_floor[:, None])

    # HYG sometimes lists a pair as one combined-light entry (Castor: 1.58 = 1.93 + 2.97) and sometimes resolves it
    # (Rigil Kentaurus -0.01 with Toliman separate). Call it a blend only when HYG's V is closer to the Tycho light of
    # candidate + partners combined than to the candidate's own Tycho light
    self_flux = 10.0 ** (-0.4 * vmag[candidates])
    combined_v = -2.5 * np.log10(self_flux + (significant * partner_flux[None, :]).sum(axis=1))
    closer_to_combined = np.abs(hyg_mag[candidates] - combined_v) < np.abs(hyg_mag[candidates] - vmag[candidates])
    blended = significant.any(axis=1) & closer_to_combined

    chosen = candidates[~blended]
    vmag = vmag.copy()
    bv = bv.copy()
    vmag[chosen] = hyg_mag[chosen]
    bv[chosen] = np.where(np.isfinite(hyg_ci[chosen]), hyg_ci[chosen], bv[chosen])
    return vmag, bv, chosen, candidates[blended]


# Ballesteros 2012 (EPL 97, 34008, eq. 14): B-V -> effective temperature in kelvin
def bv_to_temperature(bv):
    bv = np.clip(bv, *BV_CLAMP)
    return 4600.0 * (1.0 / (0.92 * bv + 1.7) + 1.0 / (0.92 * bv + 0.62))


# Wyman, Sloan & Shirley 2013, "Simple Analytic Approximations to the CIE XYZ Color Matching Functions"
# (JCGT 2(2), 1-11), multi-lobe fit of the CIE 1931 2-degree observer; wavelength in nm
def cie_xyz_cmf(wavelength):
    # Piecewise Gaussian with different widths either side of the peak
    def lobe(x, mu, s1, s2):
        t = (x - mu) / np.where(x < mu, s1, s2)
        return np.exp(-0.5 * t * t)

    x = 1.056 * lobe(wavelength, 599.8, 37.9, 31.0) + 0.362 * lobe(wavelength, 442.0, 16.0, 26.7) \
        - 0.065 * lobe(wavelength, 501.1, 20.4, 26.2)
    y = 0.821 * lobe(wavelength, 568.8, 46.9, 40.5) + 0.286 * lobe(wavelength, 530.9, 16.3, 31.1)
    z = 1.217 * lobe(wavelength, 437.0, 11.8, 36.0) + 0.681 * lobe(wavelength, 459.0, 26.0, 13.8)
    return np.stack([x, y, z], axis=1)


# Builds a temperature -> linear-sRGB lookup table by integrating Planck's law against the CIE CMFs
def build_blackbody_lut(t_min=1000.0, t_max=50000.0, samples=4096):
    temperatures = np.geomspace(t_min, t_max, samples)
    wavelength = np.arange(380.0, 781.0, 1.0)
    cmf = cie_xyz_cmf(wavelength)

    # Planck spectral radiance up to a constant: lambda^-5 / (exp(hc / (lambda k T)) - 1)
    c2 = 1.438776877e-2  # second radiation constant hc/k, m*K
    lam = wavelength * 1e-9
    planck = lam[None, :] ** -5 / np.expm1(c2 / (lam[None, :] * temperatures[:, None]))
    xyz = planck @ cmf

    # CIE XYZ -> linear sRGB / Rec.709 primaries, D65 white (IEC 61966-2-1)
    to_srgb = np.array([[3.2404542, -1.5371385, -0.4985314],
                        [-0.9692660, 1.8760108, 0.0415560],
                        [0.0556434, -0.2040259, 1.0572252]])
    rgb = normalise_luminance(np.clip(xyz @ to_srgb.T, 0.0, None))
    return temperatures, rgb


# Scales RGB rows so their Rec.709 luminance is exactly 1 (brightness then lives only in the flux scalar)
def normalise_luminance(rgb):
    luminance = rgb @ np.array([0.2126, 0.7152, 0.0722])
    return rgb / luminance[:, None]


# Converts B-V to linear sRGB (luminance 1); NaN B-V gives neutral white (1, 1, 1), which is also luminance 1
def bv_to_linear_rgb(bv, lut):
    temperatures, lut_rgb = lut
    rgb = np.ones((len(bv), 3))
    known = np.isfinite(bv)
    log_t = np.log(bv_to_temperature(bv[known]))
    log_lut = np.log(temperatures)
    for channel in range(3):
        rgb[known, channel] = np.interp(log_t, log_lut, lut_rgb[:, channel])
    return rgb


# ---------------------------------------------------------------------------------------------------------------------
# Cubemap math (D3D / Unreal TextureCube convention; the sampling vector is the Unreal-handed direction)
# ---------------------------------------------------------------------------------------------------------------------

# Maps direction vectors (n, 3) to (face index, u, v) with u, v in [-1, 1]; u right, v down within the face image
def direction_to_face_uv(d):
    x, y, z = d[:, 0], d[:, 1], d[:, 2]
    ax, ay, az = np.abs(x), np.abs(y), np.abs(z)

    # Major axis picks the face; ties resolve X over Y over Z
    face = np.where((ax >= ay) & (ax >= az), np.where(x >= 0, 0, 1),
                    np.where(ay >= az, np.where(y >= 0, 2, 3), np.where(z >= 0, 4, 5)))

    # Per-face (sc, tc, ma) from the D3D cube-map face table
    sc = np.select([face == 0, face == 1, face == 2, face == 3, face == 4], [-z, z, x, x, x], -x)
    tc = np.select([face == 2, face == 3], [z, -z], -y)
    ma = np.select([face <= 1, face <= 3], [ax, ay], az)
    return face, sc / ma, tc / ma


# Inverse of direction_to_face_uv for face coordinates (u, v may lie slightly outside [-1, 1]); not normalised
def face_uv_to_direction(face, u, v):
    one = np.ones_like(u)
    x = np.select([face == 0, face == 1, face == 5], [one, -one, -u], u)
    y = np.select([face == 2, face == 3], [one, -one], -v)
    z = np.select([face == 0, face == 1, face == 2, face == 3, face == 4], [-u, u, v, -v, one], -one)
    return np.stack([x, y, z], axis=1)


# Converts ecliptic (right-handed) directions to the Unreal-handed frame used for cube sampling: (x, -y, z)
def ecliptic_to_unreal(d):
    return d * np.array([1.0, -1.0, 1.0])


# Splats flux * rgb for each direction onto a (6, N, N, 3) cube with a bilinear (tent, 1-texel radius) kernel,
# wrapping kernel taps that fall off a face edge onto the neighbouring face
def splat_to_cube(directions, flux, rgb, size):
    face, u, v = direction_to_face_uv(directions)

    # Continuous texel coordinates (texel centers at integers) and the bilinear footprint's top-left tap
    s = (u + 1.0) * 0.5 * size - 0.5
    t = (v + 1.0) * 0.5 * size - 0.5
    s0 = np.floor(s).astype(np.int64)
    t0 = np.floor(t).astype(np.int64)
    fs = s - s0
    ft = t - t0

    accum = np.zeros((3, 6 * size * size))

    # Scatter-accumulates weighted flux into texels (face, s, t) for many stars at once
    def deposit(tap_face, tap_s, tap_t, energy, colors):
        index = (tap_face * size + tap_t) * size + tap_s
        for channel in range(3):
            accum[channel] += np.bincount(index, weights=energy * colors[:, channel], minlength=6 * size * size)

    # Bilinear tap weights; a tap past BOTH edges of a face lies beyond a cube corner where only 3 texels meet, so it
    # has no texel of its own -- drop it and renormalise the other three (keeps corner stars' centroids symmetric)
    offsets = ((0, 0), (1, 0), (0, 1), (1, 1))
    weights = np.stack([(1 - fs) * (1 - ft), fs * (1 - ft), (1 - fs) * ft, fs * ft])
    corner_star = np.zeros(len(face), dtype=bool)
    for tap, (ds, dt) in enumerate(offsets):
        beyond_corner = (((s0 + ds) < 0) | ((s0 + ds) >= size)) & (((t0 + dt) < 0) | ((t0 + dt) >= size))
        weights[tap, beyond_corner] = 0.0
        corner_star |= beyond_corner
    weights /= weights.sum(axis=0, keepdims=True)

    for (ds, dt), weight in zip(offsets, weights):
        tap_s = s0 + ds
        tap_t = t0 + dt
        energy = flux * weight
        outside = (tap_s < 0) | (tap_s >= size) | (tap_t < 0) | (tap_t >= size)
        inside = ~outside
        deposit(face[inside], tap_s[inside], tap_t[inside], energy[inside], rgb[inside])
        if not outside.any():
            continue

        # Taps past one face edge: rebuild the tap texel center's true 3D direction, re-project it onto the
        # neighbouring face and spread it bilinearly there, so the along-edge position stays geometrically exact
        # (the texel straight across a fold sits at along-edge coordinate v (1 + 1/size), not v)
        edge_tap = outside & ~corner_star
        tu = (tap_s[edge_tap] + 0.5) / size * 2.0 - 1.0
        tv = (tap_t[edge_tap] + 0.5) / size * 2.0 - 1.0
        wrapped_face, wu, wv = direction_to_face_uv(face_uv_to_direction(face[edge_tap], tu, tv))
        ws = (wu + 1.0) * 0.5 * size - 0.5
        wt = (wv + 1.0) * 0.5 * size - 0.5
        ws0 = np.floor(ws).astype(np.int64)
        wt0 = np.floor(wt).astype(np.int64)
        wfs = ws - ws0
        wft = wt - wt0
        for es, et, sub_weight in ((0, 0, (1 - wfs) * (1 - wft)), (1, 0, wfs * (1 - wft)),
                                   (0, 1, (1 - wfs) * wft), (1, 1, wfs * wft)):
            deposit(wrapped_face, np.clip(ws0 + es, 0, size - 1), np.clip(wt0 + et, 0, size - 1),
                    energy[edge_tap] * sub_weight, rgb[edge_tap])

        # Stars within half a texel of a cube corner (their beyond-corner tap was dropped above) use plain
        # seamless-cube texel adjacency instead: the tap goes to the texel straight across the edge, found by
        # re-projecting a point just past the fold at the tap's along-edge coordinate. This keeps the three corner
        # texels symmetric, which the bilinear re-projection above does not (it favours the tie-break face)
        corner_tap = outside & corner_star
        if corner_tap.any():
            across = 1.0 + 1e-6
            tu = np.clip((tap_s[corner_tap] + 0.5) / size * 2.0 - 1.0, -across, across)
            tv = np.clip((tap_t[corner_tap] + 0.5) / size * 2.0 - 1.0, -across, across)
            wrapped_face, wu, wv = direction_to_face_uv(face_uv_to_direction(face[corner_tap], tu, tv))
            deposit(wrapped_face, np.clip(np.floor((wu + 1.0) * 0.5 * size), 0, size - 1).astype(np.int64),
                    np.clip(np.floor((wv + 1.0) * 0.5 * size), 0, size - 1).astype(np.int64),
                    energy[corner_tap], rgb[corner_tap])

    cube = accum.T.astype(np.float32).reshape(6, size, size, 3)
    del accum

    # Flux per texel -> radiance: divide by each texel's solid angle relative to a face-center texel,
    # dOmega ~ (1 + u^2 + v^2)^-3/2, so corner texels (5.2x smaller) aren't dimmer than center ones
    centers = (np.arange(size) + 0.5) / size * 2.0 - 1.0
    solid_angle_ratio = (1.0 + centers[None, :] ** 2 + centers[:, None] ** 2) ** 1.5
    cube *= solid_angle_ratio.astype(np.float32)[None, :, :, None]
    return cube


# Samples a cube at Unreal-handed directions with nearest-texel lookup (for previews and self-checks only)
def sample_cube(cube, directions):
    size = cube.shape[1]
    face, u, v = direction_to_face_uv(directions)
    s = np.clip(((u + 1.0) * 0.5 * size).astype(np.int64), 0, size - 1)
    t = np.clip(((v + 1.0) * 0.5 * size).astype(np.int64), 0, size - 1)
    return cube[face, t, s]


# Returns the synthetic test directions for verify_splat as (label, unit vector) pairs, Unreal-handed
def verification_directions(size):
    rng = np.random.default_rng(20260930)
    cases = []
    inset = 1.0 - 0.3 / size  # 0.3 texel inside the edge, so kernel taps land on the neighbouring face

    # Interior points (random, off texel centers)
    for index in range(16):
        face = np.array([index % 6])
        u, v = rng.uniform(-0.9, 0.9, 2)
        cases.append(("interior f%d" % face[0], face_uv_to_direction(face, np.array([u]), np.array([v]))[0]))

    # Just inside each of the 24 face edges (4 per face), at a random position along the edge
    for face_index in range(6):
        face = np.array([face_index])
        for edge, (u, v) in enumerate([(inset, None), (-inset, None), (None, inset), (None, -inset)]):
            along = rng.uniform(-0.8, 0.8)
            u = along if u is None else u
            v = along if v is None else v
            cases.append(("edge f%d e%d" % (face_index, edge),
                          face_uv_to_direction(face, np.array([u]), np.array([v]))[0]))

    # The 8 cube corners, exactly, then points 0.2 and 0.45 texel inside a corner of each face (method changeover zone)
    for corner in range(8):
        cases.append(("corner %d" % corner, np.array([1.0 if corner & bit else -1.0 for bit in (1, 2, 4)])))
    for face_index in range(6):
        for texels in (0.2, 0.45):
            near = 1.0 - texels * 2.0 / size
            signs = rng.choice([-1.0, 1.0], 2)
            cases.append(("near-corner f%d %.2f" % (face_index, texels),
                          face_uv_to_direction(np.array([face_index]), np.array([signs[0] * near]),
                                               np.array([signs[1] * near]))[0]))

    # Random directions over the whole sphere
    for index in range(24):
        cases.append(("random %d" % index, rng.normal(size=3)))
    return [(label, d / np.linalg.norm(d)) for label, d in cases]


# Splats single synthetic stars onto a small cube and checks each one's flux-weighted centroid lands within
# VERIFY_TOLERANCE_TEXELS of its true direction and its flux is conserved; returns (passed, summary, failure lines)
def verify_splat(size=VERIFY_FACE_SIZE):
    # Unit direction and relative solid angle of every texel center, for the centroid and flux sums
    centers = (np.arange(size) + 0.5) / size * 2.0 - 1.0
    u_grid, v_grid = np.meshgrid(centers, centers)
    texel_dirs = np.stack([face_uv_to_direction(np.full(size * size, f), u_grid.ravel(), v_grid.ravel())
                           for f in range(6)])
    radial = np.linalg.norm(texel_dirs, axis=2, keepdims=True)
    texel_dirs = texel_dirs / radial
    inv_ratio = (1.0 + u_grid ** 2 + v_grid ** 2).ravel() ** -1.5

    worst = 0.0
    failures = []
    cases = verification_directions(size)
    for label, direction in cases:
        cube = splat_to_cube(direction[None, :], np.array([1.0]), np.ones((1, 3)), size)

        # Undo the solid-angle weighting to get back per-texel flux, then take the flux-weighted mean direction
        texel_flux = cube[..., 1].reshape(6, -1) * inv_ratio[None, :]
        total = texel_flux.sum()
        centroid = (texel_flux[..., None] * texel_dirs).sum(axis=(0, 1))
        centroid /= np.linalg.norm(centroid)
        error_rad = math.acos(min(1.0, float(centroid @ direction)))

        # Express the error in local texel widths: at face coords (u, v) a texel spans (2/size) sqrt(1+v^2)/(1+u^2+v^2)
        # rad along u and (2/size) sqrt(1+u^2)/(1+u^2+v^2) along v; use the narrower of the two (strictest)
        face, u, v = direction_to_face_uv(direction[None, :])
        uu, vv = u[0] ** 2, v[0] ** 2
        texel_rad = (2.0 / size) * math.sqrt(1.0 + min(uu, vv)) / (1.0 + uu + vv)
        error_texels = error_rad / texel_rad
        worst = max(worst, error_texels)
        if error_texels > VERIFY_TOLERANCE_TEXELS or abs(total - 1.0) > 1e-4:
            failures.append("%s: centroid off by %.3f texels, flux %.6f" % (label, error_texels, total))
    report = "%d synthetic stars at %d^2, worst centroid error %.4f texels (limit %.2f)" % (
        len(cases), size, worst, VERIFY_TOLERANCE_TEXELS)
    return not failures, report, failures


# ---------------------------------------------------------------------------------------------------------------------
# Writers
# ---------------------------------------------------------------------------------------------------------------------

# Writes the bright-star binary, brightest first
def write_bright(path, directions, vmag, flux, rgb):
    order = np.argsort(vmag)
    records = np.column_stack([directions[order], vmag[order], flux[order], rgb[order]]).astype("<f4")
    with atomic_output(path) as temp_path, open(temp_path, "wb") as out:
        out.write(BRIGHT_HEADER.pack(BRIGHT_MAGIC, BRIGHT_VERSION, len(records), len(BRIGHT_FIELDS)))
        out.write(records.tobytes())
    return records


# Writes one face as an uncompressed ("flat") Radiance RGBE .hdr file
def write_radiance_hdr(path, image):
    height, width, _ = image.shape
    peak = image.max(axis=2)
    mantissa, exponent = np.frexp(peak)
    scale = np.where(peak > 1e-32, mantissa * 256.0 / np.where(peak > 0, peak, 1.0), 0.0)
    rgbe = np.zeros((height, width, 4), dtype=np.uint8)
    rgbe[..., :3] = np.clip(image * scale[..., None], 0, 255).astype(np.uint8)
    rgbe[..., 3] = np.where(peak > 1e-32, exponent + 128, 0).astype(np.uint8)
    with atomic_output(path) as temp_path, open(temp_path, "wb") as out:
        out.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %d +X %d\n" % (height, width))
        out.write(rgbe.tobytes())


# Writes the whole cube as a DDS cubemap: DX10 header, R16G16B16A16_FLOAT, faces +X,-X,+Y,-Y,+Z,-Z, no mips
def write_dds_cube(path, cube):
    size = cube.shape[1]
    dds_flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x8  # CAPS | HEIGHT | WIDTH | PIXELFORMAT | PITCH
    caps2 = 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x4000 | 0x8000  # CUBEMAP + all six faces
    pixel_format = struct.pack("<II4sIIIII", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)  # FOURCC -> DX10 extension
    header = struct.pack("<4sIIIIIII44s", b"DDS ", 124, dds_flags, size, size, size * 8, 0, 1, b"\0" * 44)
    header += pixel_format + struct.pack("<IIIII", 0x1000 | 0x8, caps2, 0, 0, 0)  # caps = TEXTURE | COMPLEX
    dx10 = struct.pack("<IIIII", 10, 3, 0x4, 1, 0)  # R16G16B16A16_FLOAT, TEXTURE2D, TEXTURECUBE, arraySize 1
    rgba = np.concatenate([cube, np.ones(cube.shape[:3] + (1,))], axis=3).astype("<f2")
    with atomic_output(path) as temp_path, open(temp_path, "wb") as out:
        out.write(header + dx10)
        out.write(rgba.tobytes())


# Reads back a DDS written by write_dds_cube and returns its RGB float cube (round-trip self-check)
def read_dds_cube(path, size):
    raw = np.fromfile(path, dtype="<f2", offset=128 + 20)
    return raw.reshape(6, size, size, 4)[..., :3].astype(np.float64)


# Tone-maps HDR RGB to 8-bit with a log curve on luminance (hue kept), white at the given luminance
def tone_map(image, white):
    luminance = image @ np.array([0.2126, 0.7152, 0.0722])
    knee = white / 30.0
    mapped = np.log1p(luminance / knee) / math.log1p(white / knee)
    ratio = np.where(luminance > 0, mapped / np.maximum(luminance, 1e-30), 0.0)
    srgb = np.clip(image * ratio[..., None], 0.0, 1.0) ** (1.0 / 2.2)
    return (srgb * 255.0 + 0.5).astype(np.uint8)


# Saves an 8-bit RGB array as a PNG via a temp file
def save_png(pixels, path):
    with atomic_output(path) as temp_path:
        Image.fromarray(pixels).save(temp_path, format="PNG")


# Writes per-face PNGs, a horizontal-cross PNG, and an ecliptic equirectangular PNG sampled through the cube
def write_previews(cube, suffix):
    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
    nonzero = cube[cube.max(axis=3) > 0] @ np.array([0.2126, 0.7152, 0.0722])
    white = float(np.percentile(nonzero, 99.9)) if len(nonzero) else 1.0
    size = cube.shape[1]
    small = size // 4

    # Per-face previews at full size, and a 2x2-box-downsampled cross (faint single stars average into the glow)
    faces = []
    for index, name in enumerate(FACE_NAMES):
        save_png(tone_map(cube[index], white), PREVIEW_DIR / ("face_%d_%s%s.png" % (index, name, suffix)))
        down = cube[index].reshape(small, 4, small, 4, 3).mean(axis=(1, 3))
        faces.append(tone_map(down, white / 4.0))
    cross = np.zeros((small * 3, small * 4, 3), dtype=np.uint8)
    for index, (row, column) in enumerate([(1, 2), (1, 0), (0, 1), (2, 1), (1, 1), (1, 3)]):
        cross[row * small:(row + 1) * small, column * small:(column + 1) * small] = faces[index]
    save_png(cross, PREVIEW_DIR / ("cube_cross%s.png" % suffix))

    # Equirectangular map in ecliptic longitude (0 at centre, increasing left like a sky chart) / latitude
    width, height = 2048, 1024
    lon = np.radians(180.0 - (np.arange(width) + 0.5) / width * 360.0)
    lat = np.radians(90.0 - (np.arange(height) + 0.5) / height * 180.0)
    lon_grid, lat_grid = np.meshgrid(lon, lat)
    ecliptic = np.stack([np.cos(lat_grid) * np.cos(lon_grid), np.cos(lat_grid) * np.sin(lon_grid),
                         np.sin(lat_grid)], axis=-1).reshape(-1, 3)
    sampled = sample_cube(cube, ecliptic_to_unreal(ecliptic)).reshape(height, width, 3)
    # Box-blur 5x5 so the preview shows star density rather than aliasing on individual stars; longitude wraps,
    # latitude repeats its edge rows (wrapping it would blend the north-pole rows into the south-pole rows)
    padded = np.pad(np.pad(sampled, ((2, 2), (0, 0), (0, 0)), mode="edge"), ((0, 0), (2, 2), (0, 0)), mode="wrap")
    blurred = sum(padded[dy:dy + height, dx:dx + width] for dy in range(5) for dx in range(5)) / 25.0
    save_png(tone_map(blurred, white / 4.0), PREVIEW_DIR / ("equirect_ecliptic%s.png" % suffix))


# ---------------------------------------------------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------------------------------------------------

# Prints a magnitude histogram with one row per whole magnitude
def print_histogram(vmag):
    log("magnitude histogram (Johnson V, after Tycho transform and HYG overrides):")
    edges = np.arange(-2.0, 16.0, 1.0)
    counts, _ = np.histogram(np.clip(vmag, -2.0, 14.999), bins=edges)
    peak = counts.max()
    for low, count in zip(edges[:-1], counts):
        label = ("%5.1f..%-5.1f" % (low, low + 1)) if low < 14 else " >=14.0    "
        print("    %s %9d  %s" % (label, count, "#" * int(round(60 * count / peak))), flush=True)


# Returns True when every output exists and is newer than both CSVs and this script
def outputs_current(paths):
    newest_input = max(CSV_PATH.stat().st_mtime, HYG_CSV_PATH.stat().st_mtime, Path(__file__).stat().st_mtime)
    return all(p.exists() and p.stat().st_mtime > newest_input for p in paths)


# Runs the bake; returns the process exit code
def main():
    parser = argparse.ArgumentParser(description="Bake AT-HYG into bright-star data and a faint-star HDR cubemap.")
    parser.add_argument("--rebuild", action="store_true", help="ignore the parse cache and re-bake everything")
    parser.add_argument("--include-bright-in-cube", action="store_true",
                        help="debug: also splat the bright stars into the cube (outputs get a _withbright suffix), "
                             "to check cube orientation against the sprite list")
    args = parser.parse_args()
    suffix = "_withbright" if args.include_bright_in_cube else ""
    for path in (CSV_PATH, HYG_CSV_PATH):
        if not path.exists():
            log("ERROR: %s not found; run fetch_athyg.py first" % path)
            return 1
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    bright_path = OUT_DIR / "star_field_bright.bin"
    dds_path = OUT_DIR / ("star_field_cube%s.dds" % suffix)
    meta_path = OUT_DIR / ("star_field_meta%s.json" % suffix)
    hdr_paths = [OUT_DIR / ("star_field_face_%d_%s%s.hdr" % (i, n, suffix)) for i, n in enumerate(FACE_NAMES)]
    if not args.rebuild and outputs_current([bright_path, dds_path, meta_path] + hdr_paths):
        log("outputs are current (use --rebuild to force)")
        return 0
    started = time.time()

    # Placement self-check first: a splat bug aborts before any output is touched
    passed, summary, failures = verify_splat()
    log("placement self-check: %s -> %s" % (summary, "PASS" if passed else "FAIL"))
    if not passed:
        for line in failures:
            log("  " + line)
        return 1

    catalog = load_catalog(args.rebuild)
    total_rows = len(catalog["ra"]) + int(catalog["malformed"])

    # Reject rows that can't be placed or measured, and the Sun (row 1, dist 4.85e-6 pc)
    ra, dec, mag = catalog["ra"], catalog["dec"], catalog["mag"]
    bad_position = ~(np.isfinite(ra) & np.isfinite(dec) & (ra >= 0) & (ra <= 24) & (np.abs(dec) <= 90))
    bad_magnitude = ~np.isfinite(mag) & ~bad_position
    is_sun = (catalog["dist"] < 1e-3) & ~bad_position & ~bad_magnitude
    keep = ~(bad_position | bad_magnitude | is_sun)
    log("rows %d: kept %d, dropped %d malformed, %d bad position, %d bad magnitude, %d Sun"
        % (total_rows, keep.sum(), int(catalog["malformed"]), bad_position.sum(), bad_magnitude.sum(), is_sun.sum()))
    for key in ("ra", "dec", "mag", "ci", "src", "spect_bv", "hyg_mag", "hyg_ci"):
        catalog[key] = catalog[key][keep]

    # Photometry: Tycho -> Johnson, then HYG's Johnson V / B-V for unblended stars brighter than HYG_OVERRIDE_MAG
    directions = equatorial_to_ecliptic(catalog["ra"], catalog["dec"])
    tycho_vmag, bv = johnson_photometry(catalog)
    vmag, bv, overridden, blended = apply_hyg_overrides(catalog, tycho_vmag, bv, directions)
    shift = vmag[overridden] - tycho_vmag[overridden]
    log("HYG override (V < %.1f): %d stars corrected (median shift %+.3f, largest %+.3f mag), %d kept on "
        "Tycho as resolved blends" % (HYG_OVERRIDE_MAG, len(overridden), np.median(shift), shift.min(), len(blended)))
    for index in overridden[np.argsort(vmag[overridden])][:8]:
        log("  V %6.3f (Tycho-derived %6.3f)  B-V %6.3f" % (vmag[index], tycho_vmag[index], bv[index]))

    # Color
    has_ci = np.isfinite(catalog["ci"])
    from_hyg_ci = ~has_ci & np.isfinite(catalog["hyg_ci"])
    no_color = ~np.isfinite(bv)
    from_spectral = ~has_ci & ~from_hyg_ci & ~no_color
    log("color: %d from ci, %d from HYG B-V (blank ci), %d from spectral type, %d neutral white (no color data)"
        % (has_ci.sum(), from_hyg_ci.sum(), from_spectral.sum(), no_color.sum()))
    lut = build_blackbody_lut()
    rgb = bv_to_linear_rgb(bv, lut)
    flux = 10.0 ** (-0.4 * (vmag - FLUX_REF_MAG))
    print_histogram(vmag)

    # Split at the cutoff (decision 7)
    bright = vmag < MAG_CUTOFF
    faint = ~bright
    log("bright (V < %.1f): %d   faint (V >= %.1f): %d" % (MAG_CUTOFF, bright.sum(), MAG_CUTOFF, faint.sum()))
    log("integrated flux (mag-8 units): bright %.4g, faint %.4g (faint = %.1f%% of all starlight)"
        % (flux[bright].sum(), flux[faint].sum(), 100.0 * flux[faint].sum() / flux.sum()))

    # Milky Way sanity check: faint stars should crowd the galactic plane (|b| < 10 deg is 17.4% of the sky)
    b = galactic_latitude(catalog["ra"][faint], catalog["dec"][faint])
    log("faint stars with |galactic b| < 10 deg: %.1f%% (uniform sky would be 17.4%%)" % (100.0 * np.mean(np.abs(b) < 10)))

    # Bright-star list
    records = write_bright(bright_path, directions[bright], vmag[bright], flux[bright], rgb[bright])
    log("wrote %s (%d stars, %.2f MB)" % (bright_path.name, len(records), bright_path.stat().st_size / 1e6))

    # Faint cube (optionally with bright stars for orientation checks)
    splat_mask = np.ones_like(bright) if args.include_bright_in_cube else faint
    splat_started = time.time()
    cube = splat_to_cube(ecliptic_to_unreal(directions[splat_mask]), flux[splat_mask], rgb[splat_mask], FACE_SIZE)
    log("splatted %d stars onto 6 x %d^2 in %.1f s" % (splat_mask.sum(), FACE_SIZE, time.time() - splat_started))

    # Flux-conservation check: radiance x relative solid angle must sum back to the splatted luminance. This only
    # proves no flux was dropped or duplicated -- placement is covered by the self-check at the start
    centers = (np.arange(FACE_SIZE) + 0.5) / FACE_SIZE * 2.0 - 1.0
    inv_ratio = (1.0 + centers[None, :] ** 2 + centers[:, None] ** 2) ** -1.5
    cube_luminance = ((cube @ np.array([0.2126, 0.7152, 0.0722])) * inv_ratio[None]).sum()
    log("flux conservation (no dropped flux; not a placement check): cube %.6g vs stars %.6g"
        % (cube_luminance, flux[splat_mask].sum()))
    lit = (cube.max(axis=3) > 0).mean()
    log("cube: %.1f%% of texels lit, max texel %.4g, max half-float 65504" % (100.0 * lit, cube.max()))

    # Cube outputs
    write_dds_cube(dds_path, cube)
    round_trip = np.abs(read_dds_cube(dds_path, FACE_SIZE) - cube).max()
    log("wrote %s (%.1f MB), round-trip max abs error %.3g" % (dds_path.name, dds_path.stat().st_size / 1e6, round_trip))
    for index, path in enumerate(hdr_paths):
        write_radiance_hdr(path, cube[index])
    log("wrote 6 Radiance .hdr faces")

    # Metadata for the 4b import
    meta = {
        "source": {"catalog": "AT-HYG v4.0", "repository": "https://codeberg.org/astronexus/athyg",
                   "download": "https://codeberg.org/astronexus/athyg/media/branch/main/data/athyg_40.csv.gz",
                   "photometry_correction": "HYG v4.4, https://codeberg.org/astronexus/hyg/media/branch/main/"
                                            "data/hyg/CURRENT/hyg_v44.csv.gz",
                   "license": "CC BY-SA 4.0", "license_url": "http://creativecommons.org/licenses/by-sa/4.0/",
                   "attribution": "AT-HYG and HYG star catalogs, David Nash / astronexus, "
                                  "https://codeberg.org/astronexus, CC BY-SA 4.0"},
        "parameters": {"mag_cutoff": MAG_CUTOFF, "face_size": FACE_SIZE, "flux_ref_mag": FLUX_REF_MAG,
                       "obliquity_deg": OBLIQUITY_DEG, "hyg_override_mag": HYG_OVERRIDE_MAG,
                       "blend_radius_arcsec": BLEND_RADIUS_ARCSEC, "blend_flux_fraction": BLEND_FLUX_FRACTION,
                       "include_bright_in_cube": args.include_bright_in_cube},
        "self_check": summary,
        "counts": {"rows": total_rows, "kept": int(keep.sum()), "bright": int(bright.sum()), "faint": int(faint.sum()),
                   "dropped_malformed": int(catalog["malformed"]), "dropped_bad_position": int(bad_position.sum()),
                   "dropped_bad_magnitude": int(bad_magnitude.sum()), "dropped_sun": int(is_sun.sum()),
                   "hyg_overridden": int(len(overridden)), "hyg_skipped_as_blend": int(len(blended)),
                   "color_from_ci": int(has_ci.sum()), "color_from_hyg_ci": int(from_hyg_ci.sum()),
                   "color_from_spectral_type": int(from_spectral.sum()), "color_neutral_white": int(no_color.sum())},
        "bright_file": {"path": bright_path.name, "header": "<8sIII: magic 'SOLSTARB', version, count, floats/record",
                        "record": "<8f little-endian float32", "fields": BRIGHT_FIELDS,
                        "order": "ascending vmag (brightest first)",
                        "direction_frame": "unit vector, J2000 ecliptic, right-handed (+X vernal equinox, +Z north "
                                           "ecliptic pole) -- the project's universe frame; convert with "
                                           "SOLRender::EclipticToUnreal (X, -Y, Z)",
                        "flux": "10^(-0.4 (V - 8.0)): 1.0 at the cutoff, %.0f for the brightest star (V %.2f)"
                                % (records[0, 4], records[0, 3]),
                        "rgb": "linear sRGB (Rec.709, D65), normalised to luminance 1"},
        "cube": {"dds": dds_path.name, "hdr_faces": [p.name for p in hdr_paths],
                 "format": "DDS DX10 R16G16B16A16_FLOAT cubemap, 1 mip, alpha = 1",
                 "face_order": FACE_NAMES,
                 "convention": "D3D / Unreal TextureCube face table; sample with the UNREAL-handed world direction "
                               "(ecliptic (x, y, z) -> (x, -y, z)); in each face image u runs left->right, v top->bottom",
                 "texel_value": "linear-sRGB radiance: summed star flux (mag-8 units) per face-center-texel solid "
                                "angle ((2/2048)^2 sr ~ 9.54e-7 sr); a mag-8 star is 1.0 spread over its splat",
                 "splat": "bilinear (tent, 1-texel radius, 2x2 taps); off-face taps re-projected onto the adjacent "
                          "face and spread bilinearly there; within half a texel of a cube corner the beyond-corner tap "
                          "is dropped and the rest use texel adjacency; then divided by relative texel solid angle"},
    }
    with atomic_output(meta_path) as temp_path:
        temp_path.write_text(json.dumps(meta, indent=2), encoding="utf-8")
    log("wrote " + meta_path.name)

    write_previews(cube, suffix)
    log("wrote previews to " + str(PREVIEW_DIR))
    log("done in %.1f s" % (time.time() - started))
    return 0


if __name__ == "__main__":
    sys.exit(main())
