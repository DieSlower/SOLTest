# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Downloads the star catalogs for the star-field bake (sub-part 4a of Part 4 / issue #5, SDD 5 decision 1, §3.1 step 1).
#
# Sources (confirmed 2026-09-30). Both are by David Nash / astronexus and stored in Git LFS: the ".../raw/..." URL
# returns only the ~134-byte LFS pointer, so the real bytes come from ".../media/...". Each pointer's sha256 oid is
# used here to verify the download.
#
#   AT-HYG v4.0 (primary catalog, 2,558,654 rows, 34 columns)
#     Repository  https://codeberg.org/astronexus/athyg  (the GitHub original is no longer maintained)
#     File        data/athyg_40.csv.gz
#     Download    https://codeberg.org/astronexus/athyg/media/branch/main/data/athyg_40.csv.gz
#
#   HYG v4.4 (Hipparcos-based Johnson V and B-V, 119,614 rows; used only to correct the brightest stars, whose Tycho-2
#   photometry saturates -- AT-HYG's "hyg" column is this file's "id")
#     Repository  https://codeberg.org/astronexus/hyg
#     File        data/hyg/CURRENT/hyg_v44.csv.gz
#     Download    https://codeberg.org/astronexus/hyg/media/branch/main/data/hyg/CURRENT/hyg_v44.csv.gz
#
#   License     Both are licensed under Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0,
#               http://creativecommons.org/licenses/by-sa/4.0/). Attribution: "AT-HYG and HYG star catalogs,
#               David Nash / astronexus, https://codeberg.org/astronexus, CC BY-SA 4.0". Everything baked from them
#               (the star-field cubemap and bright-star data) is a derivative work and carries the same license.
#
# Output (all under the gitignored Tools/StarField/data/):
#   athyg_40.csv.gz, hyg_v44.csv.gz   the verified downloads
#   athyg_40.csv, hyg_v44.csv         decompressed copies read by bake_star_field.py
#
# Idempotent: an existing file whose sha256 matches its LFS pointer is kept. Pass --rebuild to re-download and
# re-decompress unconditionally.
# Run: python Tools/StarField/fetch_athyg.py [--rebuild]
import argparse
import gzip
import hashlib
import shutil
import sys
import time
import urllib.request
from pathlib import Path

DATA_DIR = Path(__file__).resolve().parent / "data"

# (label, repository URL, path of the .csv.gz inside the repository)
DATASETS = [
    ("AT-HYG v4.0", "https://codeberg.org/astronexus/athyg", "data/athyg_40.csv.gz"),
    ("HYG v4.4", "https://codeberg.org/astronexus/hyg", "data/hyg/CURRENT/hyg_v44.csv.gz"),
]


# Prints a tagged progress line
def log(message):
    print("[fetch_athyg] " + message, flush=True)


# Fetches a Git LFS pointer and returns (sha256 hex, size in bytes), or (None, None) if it can't be read
def fetch_lfs_pointer(pointer_url):
    try:
        with urllib.request.urlopen(pointer_url, timeout=60) as response:
            text = response.read(4096).decode("utf-8", "replace")
    except OSError as error:
        log("WARNING: could not read the LFS pointer (%s); the download will not be hash-verified" % error)
        return None, None
    fields = dict(line.split(" ", 1) for line in text.splitlines() if " " in line)
    oid = fields.get("oid", "")
    if not oid.startswith("sha256:"):
        log("WARNING: LFS pointer has no sha256 oid; the download will not be hash-verified")
        return None, None
    return oid[len("sha256:"):].strip(), int(fields.get("size", "0"))


# Returns the sha256 hex digest of a file, streamed in 8 MiB blocks
def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(8 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


# Streams a URL to a temp file with periodic progress, then moves it into place
def download(url, gz_path, expected_size):
    temp_path = gz_path.with_suffix(".gz.part")
    log("downloading " + url)
    started = time.time()
    with urllib.request.urlopen(url, timeout=120) as response, open(temp_path, "wb") as out:
        total = int(response.headers.get("Content-Length") or expected_size or 0)
        done = 0
        next_report = 0
        for block in iter(lambda: response.read(1 << 20), b""):
            out.write(block)
            done += len(block)
            if done >= next_report:
                percent = (100.0 * done / total) if total else 0.0
                log("  %.1f / %.1f MB (%.0f%%)" % (done / 1e6, total / 1e6, percent))
                next_report += 20 << 20
    temp_path.replace(gz_path)
    log("downloaded %.1f MB in %.1f s" % (gz_path.stat().st_size / 1e6, time.time() - started))


# Decompresses a .csv.gz to the given .csv via a temp file
def decompress(gz_path, csv_path):
    log("decompressing to " + csv_path.name)
    started = time.time()
    temp_path = csv_path.with_suffix(".csv.part")
    with gzip.open(gz_path, "rb") as source, open(temp_path, "wb") as out:
        shutil.copyfileobj(source, out, 16 << 20)
    temp_path.replace(csv_path)
    log("decompressed %.1f MB in %.1f s" % (csv_path.stat().st_size / 1e6, time.time() - started))


# Ensures one dataset's verified .csv.gz and decompressed .csv exist; returns True on success
def fetch_dataset(label, repo_url, file_path, rebuild):
    gz_path = DATA_DIR / Path(file_path).name
    csv_path = gz_path.with_suffix("")
    log("%s: %s" % (label, repo_url))
    expected_sha, expected_size = fetch_lfs_pointer(repo_url + "/raw/branch/main/" + file_path)

    # Keep an existing download only if it still matches the published hash
    need_download = rebuild or not gz_path.exists()
    if not need_download and expected_sha:
        if sha256_of(gz_path) == expected_sha:
            log("present and verified: " + gz_path.name + " (use --rebuild to re-download)")
        else:
            log("existing " + gz_path.name + " does not match the published sha256; re-downloading")
            need_download = True
    elif not need_download:
        log("present (unverified): " + gz_path.name)

    # Download and verify against the LFS pointer's oid
    if need_download:
        download(repo_url + "/media/branch/main/" + file_path, gz_path, expected_size)
        if expected_sha:
            actual = sha256_of(gz_path)
            if actual != expected_sha:
                log("ERROR: sha256 mismatch for %s (expected %s, got %s)" % (gz_path.name, expected_sha, actual))
                return False
            log("sha256 verified: " + actual)

    # Decompress when missing, stale, or forced
    if rebuild or need_download or not csv_path.exists() or csv_path.stat().st_mtime < gz_path.stat().st_mtime:
        decompress(gz_path, csv_path)
    else:
        log("present: " + csv_path.name)
    return True


# Fetches every dataset; returns the process exit code
def main():
    parser = argparse.ArgumentParser(description="Download and decompress the AT-HYG v4.0 and HYG v4.4 catalogs.")
    parser.add_argument("--rebuild", action="store_true", help="re-download and re-decompress even if present")
    args = parser.parse_args()
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    ok = all([fetch_dataset(label, repo, path, args.rebuild) for label, repo, path in DATASETS])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
