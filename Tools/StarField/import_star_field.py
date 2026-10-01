# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Imports the star-field bake into Unreal content (sub-part 4b of Part 4 / issue #5, SDD 5 §3.1 step 3 and decision 5),
# headless and idempotent:
#   /Game/SOL/StarField/T_SOLStarFieldCube        TextureCube from data/bake/star_field_cube.dds (faint stars, V >= 8):
#                                                 sRGB off, HDR Compressed (BC6H), mips generated (simple average; the
#                                                 bake ships mip 0 only), Skybox texture group
#   /Game/SOL/StarField/DA_SOLStarField           USOLStarFieldData from data/bake/star_field_bright.bin (bright stars,
#                                                 V < 8, brightest first) plus the metadata in star_field_meta.json
#   /Game/SOL/StarField/CelestialVault/...        one-time copies of the CelestialVault plugin assets SDD 5 decision 5
#                                                 names (see CV_ASSETS), with every reference back into the plugin removed
# The bake (Tools/StarField/bake_star_field.py) must have been run first.
#
# CelestialVault copy -- the temporary-enable dance. Unreal can only duplicate an asset whose content root is mounted,
# and /CelestialVault/ is mounted only while the plugin is enabled, but SOLTest.uproject must NEVER list CelestialVault
# (it drags in the beta DaySequence plugin). So the copy step runs with the plugin enabled for that one editor run only:
#   1. back up SOLTest.uproject byte for byte and add {"Name": "CelestialVault", "Enabled": true} to its Plugins
#   2. run this script inside UnrealEditor-Cmd; EditorAssetLibrary.duplicate_asset copies each asset into /Game/SOL/,
#      references between copies are re-pointed at the copies, and each saved copy is checked in the asset registry
#      to have no dependency left under /CelestialVault/ (the run fails loudly otherwise)
#   3. restore SOLTest.uproject's original bytes (in a finally block, so a crash or a failed import still restores it)
#   The original is also written to Saved/SOLTest.uproject.solbak before step 1, and the next run restores from it if a
#   killed driver left it behind; the editor run is killed after --timeout seconds (default 3600). Manual recovery:
#   git checkout -- SOLTest.uproject
# Running this file with a normal Python (the "driver" mode below) does all three steps for you and verifies the
# restore. Re-run the copy only when the engine version changes CelestialVault's source assets: pass -SOLRebuild, or
# delete /Game/SOL/StarField/CelestialVault/. When the copies already exist the driver does not touch the .uproject.
#
# Run (driver, recommended; editor must be closed):
#   python Tools/StarField/import_star_field.py [-SOLRebuild] [--timeout 3600] [--engine "<UE 5.8 install dir>"]
# Run (inside Unreal directly, CreateSOLContent.py style; the CelestialVault copy step then only works if the plugin
# happens to be enabled, and is otherwise skipped with an error):
#   UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript -script="<abs path>/Tools/StarField/import_star_field.py"
#       -unattended [-stdout -FullStdOutLogOutput] [-SOLRebuild]
# -SOLRebuild recreates every asset; without it existing assets are kept (re-run with it after a re-bake).
# Asset paths must match SOL::Paths in Source/SOLTest/SOLConstants.h.
import json
import struct
import sys
from pathlib import Path

try:
    import unreal
except ImportError:
    unreal = None

HERE = Path(__file__).resolve().parent
PROJECT_DIR = HERE.parent.parent
UPROJECT_PATH = PROJECT_DIR / "SOLTest.uproject"
BAKE_DIR = HERE / "data" / "bake"
DDS_PATH = BAKE_DIR / "star_field_cube.dds"
BRIGHT_PATH = BAKE_DIR / "star_field_bright.bin"
META_PATH = BAKE_DIR / "star_field_meta.json"

STAR_FIELD_DIR = "/Game/SOL/StarField"
CUBE_NAME = "T_SOLStarFieldCube"
DATA_NAME = "DA_SOLStarField"
CV_DIR = STAR_FIELD_DIR + "/CelestialVault"
CV_CONTENT_DIR = PROJECT_DIR / "Content" / "SOL" / "StarField" / "CelestialVault"
CV_PLUGIN = "CelestialVault"
CV_ROOT = "/" + CV_PLUGIN + "/"

# Dependency prefixes that mean a copy still needs the plugin: its content, its code module, or DaySequence's module
# (Source/SOLTest/Tests/StarFieldTest.cpp checks the same list on every test run, with the plugin disabled)
CV_LEAK_PREFIXES = (CV_ROOT, "/Script/CelestialVault", "/Script/DaySequence")

# CelestialVault assets copied (SDD 5 decision 5), in dependency order (a copy's references into the plugin are
# re-pointed at copies made before it). The star material is a reference for 4c's sprite material, not used directly;
# the quad mesh's material slot is cleared because its plugin material (MI_SolarSystemPlanets) is not copied.
CV_ASSETS = [
    "/CelestialVault/Textures/T_MilkyWay",
    "/CelestialVault/Textures/T_StarMask_Round",
    "/CelestialVault/Materials/MF_BillboardSizeByPixelUnits",
    "/CelestialVault/Materials/MF_ScalePlaneToMinScreenPixels",
    "/CelestialVault/Materials/MF_DirectionToLatLong",
    "/CelestialVault/Materials/M_Stars_EnergyConservative",
    "/CelestialVault/Meshes/SM_Plane_FacingX",
]

# Bright-star binary contract (bake_star_field.py BRIGHT_*): header magic, version, count, floats/record; then records
BRIGHT_MAGIC = b"SOLSTARB"
BRIGHT_VERSION = 1
BRIGHT_HEADER = struct.Struct("<8sIII")
BRIGHT_FLOATS = 8

# Printed last by the in-editor run; the driver treats its absence as failure (the commandlet's exit code is not
# reliable: every SOLTest commandlet run logs an unrelated GameFeatureData asset-manager error that makes it return 1)
DONE_MARKER = "SOLStarField: import finished OK"

DEFAULT_ENGINE_DIR = Path("C:/Program Files/Epic Games/UE_5.8")

# Driver: on-disk copy of the original .uproject, written before it is modified, so a killed driver is recovered on
# the next run; and how long the editor run may take before it is killed (BC6H encoding and shader compiles are slow)
UPROJECT_BACKUP_PATH = PROJECT_DIR / "Saved" / "SOLTest.uproject.solbak"
DEFAULT_TIMEOUT_S = 3600
RECOVERY_HINT = "if SOLTest.uproject is left listing CelestialVault, run: git checkout -- SOLTest.uproject"


# ---------------------------------------------------------------------------------------------------------------------
# In-editor import (runs inside UnrealEditor-Cmd)
# ---------------------------------------------------------------------------------------------------------------------

# Logs an informational line with the script's prefix
def log(message):
    unreal.log("SOLStarField: " + message)


# Deletes an existing asset when rebuilding; returns True if the caller should (re)create it
def prepare_destination(path, rebuild):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return True
    if not rebuild:
        log("%s exists, skipping" % path)
        return False
    if not unreal.EditorAssetLibrary.delete_asset(path):
        raise RuntimeError("SOLStarField: could not delete %s for rebuild" % path)
    return True


# Imports the baked DDS as a TextureCube with HDR-compressed, mipmapped, linear settings
def import_cube(rebuild):
    path = STAR_FIELD_DIR + "/" + CUBE_NAME
    if not prepare_destination(path, rebuild):
        return
    if not DDS_PATH.is_file():
        raise RuntimeError("SOLStarField: %s is missing; run bake_star_field.py first" % DDS_PATH)

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(DDS_PATH))
    task.set_editor_property("destination_path", STAR_FIELD_DIR)
    task.set_editor_property("destination_name", CUBE_NAME)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    cube = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(cube, unreal.TextureCube):
        raise RuntimeError("SOLStarField: %s imported as %s, not a TextureCube"
                           % (path, cube.get_class().get_name() if cube else "nothing"))

    # Set everything without per-property rebuilds; the last set notifies, which rebuilds (and BC6H-compresses) once
    quiet = unreal.PropertyAccessChangeNotifyMode.NEVER
    cube.modify()
    cube.set_editor_property("srgb", False, quiet)
    cube.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_HDR_COMPRESSED, quiet)
    cube.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE, quiet)
    cube.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_SKYBOX,
                             unreal.PropertyAccessChangeNotifyMode.ALWAYS)
    unreal.EditorAssetLibrary.save_loaded_asset(cube, False)
    height, width = struct.unpack_from("<II", DDS_PATH.read_bytes()[:20], 12)
    log("imported %s (TextureCube, %dx%d per face, BC6H, mips generated) from %s" % (path, width, height, DDS_PATH.name))


# Reads the bright-star binary into a list of 8-float tuples after validating its header
def read_bright_records():
    raw = BRIGHT_PATH.read_bytes()
    magic, version, count, floats = BRIGHT_HEADER.unpack_from(raw, 0)
    if magic != BRIGHT_MAGIC or version != BRIGHT_VERSION or floats != BRIGHT_FLOATS:
        raise RuntimeError("SOLStarField: %s has header %r v%d with %d floats/record, expected %r v%d with %d"
                           % (BRIGHT_PATH.name, magic, version, floats, BRIGHT_MAGIC, BRIGHT_VERSION, BRIGHT_FLOATS))
    expected = BRIGHT_HEADER.size + count * floats * 4
    if len(raw) != expected:
        raise RuntimeError("SOLStarField: %s is %d bytes, expected %d for %d records"
                           % (BRIGHT_PATH.name, len(raw), expected, count))
    record = struct.Struct("<%df" % floats)
    return [record.unpack_from(raw, BRIGHT_HEADER.size + index * record.size) for index in range(count)]


# Creates the USOLStarFieldData asset from the bright-star binary and the bake metadata
def import_bright(rebuild):
    path = STAR_FIELD_DIR + "/" + DATA_NAME
    if not prepare_destination(path, rebuild):
        return
    for source in (BRIGHT_PATH, META_PATH):
        if not source.is_file():
            raise RuntimeError("SOLStarField: %s is missing; run bake_star_field.py first" % source)

    records = read_bright_records()
    meta = json.loads(META_PATH.read_text(encoding="utf-8"))
    face_size = int(meta["parameters"]["face_size"])
    if int(meta["counts"]["bright"]) != len(records):
        raise RuntimeError("SOLStarField: meta says %d bright stars, binary has %d"
                           % (meta["counts"]["bright"], len(records)))

    # One struct per star: ecliptic direction, V, flux, luminance-1 linear color
    stars = [unreal.SOLBrightStar(direction=unreal.Vector3f(x, y, z), visual_magnitude=vmag, flux=flux,
                                  color=unreal.LinearColor(r, g, b, 1.0))
             for x, y, z, vmag, flux, r, g, b in records]

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.SOLStarFieldData)
    data = unreal.AssetToolsHelpers.get_asset_tools().create_asset(DATA_NAME, STAR_FIELD_DIR, unreal.SOLStarFieldData,
                                                                   factory)
    data.set_editor_property("bright_stars", stars)
    data.set_editor_property("magnitude_cutoff", float(meta["parameters"]["mag_cutoff"]))
    data.set_editor_property("flux_reference_magnitude", float(meta["parameters"]["flux_ref_mag"]))
    data.set_editor_property("cube_texel_solid_angle_sr", (2.0 / face_size) ** 2)
    data.set_editor_property("cube_face_size", face_size)
    data.set_editor_property("attribution", meta["source"]["attribution"])
    unreal.EditorAssetLibrary.save_loaded_asset(data, False)
    log("created %s with %d bright stars (V %.2f .. %.2f), texel solid angle %.4g sr"
        % (path, len(stars), records[0][3], records[-1][3], (2.0 / face_size) ** 2))


# Re-points material-expression references (textures, function calls) inside a copy from plugin assets to their copies
def remap_plugin_references(copy_path, copies):
    remapped = 0
    for expression in unreal.ObjectIterator(unreal.MaterialExpression):
        if not expression.get_path_name().startswith(copy_path + "."):
            continue
        for prop in ("texture", "material_function"):
            try:
                target = expression.get_editor_property(prop)
            except Exception:
                continue
            if target is None or not target.get_path_name().startswith(CV_ROOT):
                continue
            package = target.get_path_name().split(".")[0]
            if package not in copies:
                raise RuntimeError("SOLStarField: %s references %s, which is not copied (add it to CV_ASSETS first)"
                                   % (copy_path, package))
            expression.set_editor_property(prop, unreal.EditorAssetLibrary.load_asset(copies[package]))
            remapped += 1
    return remapped


# Fails if a saved copy is unknown to the asset registry, has no dependencies recorded, or still depends on the plugin
def check_no_plugin_dependencies(copy_paths):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([CV_DIR], True)
    options = unreal.AssetRegistryDependencyOptions(True, True, True, True, True)
    for copy_path in copy_paths:
        if not registry.get_assets_by_package_name(copy_path):
            raise RuntimeError("SOLStarField: the asset registry does not know %s; cannot check it" % copy_path)
        dependencies = [str(name) for name in (registry.get_dependencies(copy_path, options) or [])]
        if not dependencies:
            raise RuntimeError("SOLStarField: the asset registry has no dependencies for %s; cannot check it"
                               % copy_path)
        leaks = [name for name in dependencies if name.startswith(CV_LEAK_PREFIXES)]
        if leaks:
            raise RuntimeError("SOLStarField: %s still depends on %s" % (copy_path, leaks))


# Copies the CelestialVault assets into /Game/SOL/ (needs the plugin mounted for this run; see the header)
def copy_celestial_vault(rebuild):
    eal = unreal.EditorAssetLibrary
    copies = {source: CV_DIR + "/" + source.rsplit("/", 1)[1] for source in CV_ASSETS}
    pending = [source for source in CV_ASSETS if rebuild or not eal.does_asset_exist(copies[source])]
    if not pending:
        log("all %d CelestialVault copies exist under %s, skipping" % (len(CV_ASSETS), CV_DIR))
        return
    # A commandlet's asset registry has not scanned plugin content yet; duplicate_asset looks sources up there
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([CV_ROOT.rstrip("/")], True)
    if not eal.does_asset_exist(pending[0]):
        raise RuntimeError("SOLStarField: %s is not mounted, so %d CelestialVault asset(s) cannot be copied. Run this "
                           "script with a normal Python (python Tools/StarField/import_star_field.py), which enables "
                           "the plugin for one editor run and restores SOLTest.uproject afterwards"
                           % (CV_ROOT, len(pending)))

    for source in pending:
        destination = copies[source]
        if eal.does_asset_exist(destination) and not eal.delete_asset(destination):
            raise RuntimeError("SOLStarField: could not delete %s for rebuild" % destination)
        copy = eal.duplicate_asset(source, destination)
        if copy is None:
            raise RuntimeError("SOLStarField: duplicating %s -> %s failed" % (source, destination))

        # Break the copy's links back into the plugin
        remapped = remap_plugin_references(destination, copies)
        if isinstance(copy, unreal.StaticMesh):
            for index in range(len(copy.static_materials)):
                copy.set_material(index, None)
        if isinstance(copy, unreal.Material):
            unreal.MaterialEditingLibrary.recompile_material(copy)
        elif isinstance(copy, unreal.MaterialFunction):
            unreal.MaterialEditingLibrary.update_material_function(copy)
        eal.save_loaded_asset(copy, False)
        log("copied %s -> %s (%s, %d reference(s) re-pointed)"
            % (source, destination, copy.get_class().get_name(), remapped))

    check_no_plugin_dependencies([copies[source] for source in CV_ASSETS])
    log("verified no copy under %s depends on %s" % (CV_DIR, ", ".join(CV_LEAK_PREFIXES)))


# Runs every import step; any failure raises, so DONE_MARKER is only printed after a full success
def run_in_editor():
    rebuild = "-solrebuild" in unreal.SystemLibrary.get_command_line().lower()
    log("start (rebuild=%s)" % rebuild)
    import_cube(rebuild)
    import_bright(rebuild)
    copy_celestial_vault(rebuild)
    unreal.log(DONE_MARKER)


# ---------------------------------------------------------------------------------------------------------------------
# Driver (runs under a normal Python): temporarily enables CelestialVault, runs the in-editor import, restores
# ---------------------------------------------------------------------------------------------------------------------

# Returns True if any Unreal Editor process is running (it would hold the project's assets and module DLL)
def editor_running():
    import subprocess
    listing = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout.lower()
    return "unrealeditor" in listing


# Returns True if every CelestialVault copy's .uasset is already on disk
def celestial_vault_copies_exist():
    return all((CV_CONTENT_DIR / (source.rsplit("/", 1)[1] + ".uasset")).is_file() for source in CV_ASSETS)


# Returns the .uproject text with CelestialVault appended to its Plugins list
def uproject_with_plugin(original_bytes):
    project = json.loads(original_bytes.decode("utf-8-sig"))
    plugins = project.setdefault("Plugins", [])
    if any(plugin.get("Name") == CV_PLUGIN for plugin in plugins):
        raise RuntimeError("SOLTest.uproject already lists %s; remove it first (it must never stay enabled)"
                           % CV_PLUGIN)
    plugins.append({"Name": CV_PLUGIN, "Enabled": True})
    return (json.dumps(project, indent="\t") + "\n").encode("utf-8")


# Runs UnrealEditor-Cmd on this script, echoing the SOLStarField and error lines; returns True if DONE_MARKER appeared.
# The run is killed if it takes longer than timeout_s seconds
def run_editor_import(engine_dir, rebuild, timeout_s):
    import subprocess
    import threading
    editor = engine_dir / "Engine" / "Binaries" / "Win64" / "UnrealEditor-Cmd.exe"
    command = [str(editor), str(UPROJECT_PATH), "-run=pythonscript", "-script=%s" % Path(__file__).resolve(),
               "-unattended", "-nosplash", "-stdout", "-FullStdOutLogOutput"]
    if rebuild:
        command.append("-SOLRebuild")
    print("[import_star_field] running: %s" % " ".join(command), flush=True)
    finished = False
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                          errors="replace") as process:
        killed = threading.Event()

        # Kills a hung editor run so the .uproject is restored in bounded time
        def kill():
            killed.set()
            process.kill()

        watchdog = threading.Timer(timeout_s, kill)
        watchdog.start()
        try:
            for line in process.stdout:
                if "SOLStarField" in line or "Error:" in line or "Traceback" in line:
                    print(line.rstrip(), flush=True)
                finished |= DONE_MARKER in line
        finally:
            watchdog.cancel()
    if killed.is_set():
        print("[import_star_field] the editor run was killed after %d s (--timeout)" % timeout_s, flush=True)
    return finished and not killed.is_set()


# Restores SOLTest.uproject from the on-disk backup a killed earlier run left behind, then deletes the backup
def recover_from_backup():
    if not UPROJECT_BACKUP_PATH.is_file():
        return
    UPROJECT_PATH.write_bytes(UPROJECT_BACKUP_PATH.read_bytes())
    UPROJECT_BACKUP_PATH.unlink()
    print("[import_star_field] found %s from an interrupted run; restored SOLTest.uproject from it"
          % UPROJECT_BACKUP_PATH, flush=True)


# Writes the original .uproject back, verifies it and deletes the backup; never raises, prints recovery steps instead
def restore_uproject(original):
    try:
        UPROJECT_PATH.write_bytes(original)
        restored = UPROJECT_PATH.read_bytes() == original
    except OSError as error:
        print("[import_star_field] restoring SOLTest.uproject failed (%s); %s" % (error, RECOVERY_HINT), flush=True)
        return False
    print("[import_star_field] restored %s (%s)" % (UPROJECT_PATH.name, "byte-identical" if restored else "MISMATCH"),
          flush=True)
    if not restored:
        print("[import_star_field] %s (the backup is kept at %s)" % (RECOVERY_HINT, UPROJECT_BACKUP_PATH), flush=True)
        return False
    UPROJECT_BACKUP_PATH.unlink(missing_ok=True)
    return True


# Driver entry point: enables CelestialVault only when copies are needed, and always restores the .uproject
def run_driver(argv):
    import argparse
    parser = argparse.ArgumentParser(description="Import the star-field bake into SOLTest content (SDD 5, 4b).")
    parser.add_argument("-SOLRebuild", "--rebuild", dest="rebuild", action="store_true",
                        help="recreate every asset, including the CelestialVault copies")
    parser.add_argument("--engine", type=Path, default=DEFAULT_ENGINE_DIR, help="Unreal Engine 5.8 install directory")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT_S, help="seconds before the editor run is killed")
    args = parser.parse_args(argv)

    if editor_running():
        sys.exit("[import_star_field] an Unreal Editor process is running; close it first")
    recover_from_backup()
    need_plugin = args.rebuild or not celestial_vault_copies_exist()

    # Back up to disk before modifying, so even a killed driver leaves a way back (recovered on the next run)
    original = UPROJECT_PATH.read_bytes()
    if need_plugin:
        modified = uproject_with_plugin(original)
        UPROJECT_BACKUP_PATH.parent.mkdir(parents=True, exist_ok=True)
        UPROJECT_BACKUP_PATH.write_bytes(original)
        print("[import_star_field] backed up SOLTest.uproject to %s; %s" % (UPROJECT_BACKUP_PATH, RECOVERY_HINT),
              flush=True)
    restored = True
    ok = False
    try:
        if need_plugin:
            UPROJECT_PATH.write_bytes(modified)
            print("[import_star_field] temporarily enabled %s in %s" % (CV_PLUGIN, UPROJECT_PATH.name), flush=True)
        ok = run_editor_import(args.engine, args.rebuild, args.timeout)
    finally:
        if need_plugin:
            restored = restore_uproject(original)
    if not restored:
        sys.exit("[import_star_field] SOLTest.uproject was not restored; %s" % RECOVERY_HINT)
    if not ok:
        sys.exit("[import_star_field] the editor run did not finish; see the errors above")
    print("[import_star_field] done", flush=True)


if unreal is not None:
    run_in_editor()
elif __name__ == "__main__":
    run_driver(sys.argv[1:])
