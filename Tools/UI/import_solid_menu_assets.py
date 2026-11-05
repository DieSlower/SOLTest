# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Imports the solid menu textures and the Rajdhani font (SDD 46 section 10, part 46b), headless and idempotent:
#   Docs/Art/menu-style/textures/*.png      -> SOL::Paths SOLID_TEX_BRUSHED / RIVET / VENT / HAZARD
#   Docs/Art/fonts/rajdhani/Rajdhani-*.ttf  -> /Game/SOL/UI/Solid/Fonts/FF_Rajdhani_<Weight> (UFontFace)
#                                           -> SOL::Paths SOLID_MENU_FONT (F_SOLMenu, a composite UFont with the four
#                                              weights as typefaces named Regular, Medium, SemiBold, Bold)
# Every asset path is read from Source/SOLTest/SOLConstants.h (the SOLID_* entries of SOL::Paths), so the script cannot
# drift from the C++. Each asset keeps the MD5 of its source file plus its import settings in a metadata tag: a re-run
# skips (never re-imports or re-saves) an asset that is already current, so an unchanged run writes no .uasset.
# Textures: no mips, texture group UI, compression Masks (grey masks) or UserInterface2D (the rivet, which has alpha).
# Font faces: the font file import needs Slate, so UnrealEditor-Cmd crashes on a first import. When a face is missing run
#      UnrealEditor.exe SOLTest.uproject -ExecCmds="py <abs path>/Tools/UI/import_solid_menu_assets.py,quit" -unattended
#      -nosplash -nullrhi once (closed editor, then end the process if it lingers); textures and F_SOLMenu work headless.
# Run: UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript -script="<abs path>/Tools/UI/import_solid_menu_assets.py"
#      -unattended -nosplash -nullrhi (add -stdout -FullStdOutLogOutput to see the SOLSolidAssets log lines)
import hashlib
import os
import re
import unreal

HASH_TAG = "SOLSourceMD5"
DONE_MARKER = "DONE"
FONT_WEIGHTS = ["Regular", "Medium", "SemiBold", "Bold"]
FONT_FACE_DIR = "/Game/SOL/UI/Solid/Fonts"

EAL = unreal.EditorAssetLibrary
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, "..", ".."))
CONSTANTS_FILE = os.path.join(PROJECT_DIR, "Source", "SOLTest", "SOLConstants.h")
TEXTURE_DIR = os.path.join(PROJECT_DIR, "Docs", "Art", "menu-style", "textures")
FONT_DIR = os.path.join(PROJECT_DIR, "Docs", "Art", "fonts", "rajdhani")

# SOL::Paths constant, source PNG, compression, sRGB (the settings are part of the hash tag)
TEXTURES = [
    ("SOLID_TEX_BRUSHED", "brushed-gray-512.png", unreal.TextureCompressionSettings.TC_MASKS, False),
    ("SOLID_TEX_RIVET", "rivet-32.png", unreal.TextureCompressionSettings.TC_EDITOR_ICON, True),
    ("SOLID_TEX_VENT", "vent-strip-128x16.png", unreal.TextureCompressionSettings.TC_MASKS, False),
    ("SOLID_TEX_HAZARD", "hazard-128x16.png", unreal.TextureCompressionSettings.TC_MASKS, False),
]
SETTINGS_VERSION = "v1"  # bump to force a re-import when the import settings above change


# Logs one line with the script's prefix
def log(message):
    unreal.log("SOLSolidAssets: " + message)


# Returns a SOL::Paths string constant from SOLConstants.h, so the asset paths cannot drift from the C++
def path_constant(name):
    with open(CONSTANTS_FILE, "r", encoding="utf-8") as handle:
        text = handle.read()
    match = re.search(r"\b" + name + r'\s*=\s*TEXT\("([^"]+)"\)', text)
    if match is None:
        raise RuntimeError("SOLSolidAssets: %s not found in SOLConstants.h" % name)
    return match.group(1)


# Returns the MD5 hex digest of a file
def file_md5(path):
    with open(path, "rb") as handle:
        return hashlib.md5(handle.read()).hexdigest()


# Returns a source file path, failing when it is missing
def require(path):
    if not os.path.isfile(path):
        raise RuntimeError("SOLSolidAssets: missing source %s" % path)
    return path


# Splits a package path into its directory and asset name
def split_path(package_path):
    directory, name = package_path.rsplit("/", 1)
    return directory, name


# Returns whether an asset exists with this tag value (nothing to do)
def is_current(package_path, tag):
    if not EAL.does_asset_exist(package_path):
        return False
    asset = EAL.load_asset(package_path)
    return asset is not None and EAL.get_metadata_tag(asset, HASH_TAG) == tag


# Imports one file as an asset, failing when the import produced nothing, and returns the loaded asset (not yet saved)
def import_file(source, package_path):
    directory, name = split_path(package_path)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source)
    task.set_editor_property("destination_path", directory)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    if len(task.get_editor_property("imported_object_paths")) == 0:
        raise RuntimeError("SOLSolidAssets: import of %s failed" % package_path)
    asset = EAL.load_asset(package_path)
    if asset is None:
        raise RuntimeError("SOLSolidAssets: %s did not load after import" % package_path)
    return asset


# Imports the textures that changed and applies the UI settings (no mips, UI group, compression, sRGB)
def import_textures():
    for constant, filename, compression, srgb in TEXTURES:
        package_path = path_constant(constant)
        source = require(os.path.join(TEXTURE_DIR, filename))
        tag = "%s-%s-%s-%s" % (file_md5(source), SETTINGS_VERSION, compression.name, srgb)
        name = split_path(package_path)[1]
        if is_current(package_path, tag):
            log("%s unchanged, skipped" % name)
            continue
        texture = import_file(source, package_path)
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError("SOLSolidAssets: %s is not a Texture2D after import" % package_path)
        texture.set_editor_property("compression_settings", compression)
        texture.set_editor_property("srgb", srgb)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        texture.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
        texture.set_editor_property("address_y", unreal.TextureAddress.TA_WRAP)
        EAL.set_metadata_tag(texture, HASH_TAG, tag)
        EAL.save_loaded_asset(texture, only_if_is_dirty=False)
        log("%s imported (%s)" % (name, compression.name))


# Imports the font face assets that changed and returns (package path, md5) by weight
def import_font_faces():
    faces = {}
    for weight in FONT_WEIGHTS:
        source = require(os.path.join(FONT_DIR, "Rajdhani-%s.ttf" % weight))
        package_path = "%s/FF_Rajdhani_%s" % (FONT_FACE_DIR, weight)
        md5 = file_md5(source)
        faces[weight] = (package_path, md5)
        if is_current(package_path, md5):
            log("FF_Rajdhani_%s unchanged, skipped" % weight)
            continue
        face = import_file(source, package_path)
        if not isinstance(face, unreal.FontFace):
            raise RuntimeError("SOLSolidAssets: %s is not a FontFace after import" % package_path)
        EAL.set_metadata_tag(face, HASH_TAG, md5)
        EAL.save_loaded_asset(face, only_if_is_dirty=False)
        log("FF_Rajdhani_%s imported" % weight)
    return faces


# Builds the composite font F_SOLMenu with one typeface per weight when the faces changed or the font is missing
def build_composite_font(faces):
    font_path = path_constant("SOLID_MENU_FONT")
    combined = "-".join(faces[weight][1] for weight in FONT_WEIGHTS) + "-" + SETTINGS_VERSION
    if is_current(font_path, combined):
        log("F_SOLMenu unchanged, skipped")
        return
    directory, name = split_path(font_path)
    font = EAL.load_asset(font_path) if EAL.does_asset_exist(font_path) else None
    if font is None:
        font = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, directory, unreal.Font,
                                                                         unreal.FontFactory())
    if font is None:
        raise RuntimeError("SOLSolidAssets: could not create %s" % font_path)

    # FFontData, FTypeface and FTypefaceEntry are not exposed to Python, so the composite font is filled through the
    # exposed FCompositeFont's text import: one typeface entry per weight, each pointing at its font face asset
    entries = []
    for weight in FONT_WEIGHTS:
        face_path = faces[weight][0]
        asset_name = split_path(face_path)[1]
        entries.append("(Name=\"%s\",Font=(FontFaceAsset=FontFace'%s.%s'))" % (weight, face_path, asset_name))
    composite = font.get_editor_property("composite_font")
    composite.import_text("(DefaultTypeface=(Fonts=(%s)))" % ",".join(entries))
    font.set_editor_property("composite_font", composite)
    font.set_editor_property("font_cache_type", unreal.FontCacheType.RUNTIME)
    EAL.set_metadata_tag(font, HASH_TAG, combined)
    EAL.save_loaded_asset(font, only_if_is_dirty=False)
    log("F_SOLMenu built with %d typefaces" % len(entries))


# Imports the textures and the font, each only when its source changed
def main():
    import_textures()
    build_composite_font(import_font_faces())
    log(DONE_MARKER)


main()
