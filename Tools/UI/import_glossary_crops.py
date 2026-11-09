# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Imports the map glossary rows' screenshot crops (SDD 33 section 9, D39, part 33e-ii), headless and idempotent:
#   Docs/Images/<ImageFile>  -> <Texture> (a UTexture2D under Content/SOL/UI/Glossary/), one per
#                               SOL::Paths::GLOSSARY_CROPS row
# The ImageFile / Texture pairs are read from Source/SOLTest/SOLConstants.h (the GLOSSARY_CROPS table), so the script
# cannot drift from the C++. Each texture keeps "<MD5 of its source PNG>-<every import setting below>" in its
# SOLSourceMD5 metadata tag: a re-run skips (never re-imports or re-saves) a texture whose tag matches, so an unchanged
# run writes no .uasset, and a changed PNG or setting re-imports it (SOLTest.Glossary.CropTexturesCurrent checks the
# MD5 prefix). The PNGs are written by the -SOLWriteGlossaryCrops run (Tools\WritePlayerGuide.ps1 runs both).
# Settings: texture group UI, compression UserInterface2D (TC_EDITOR_ICON: uncompressed BGRA8, the thin one- and
# two-pixel lines stay crisp), sRGB, clamp addressing, never streamed, and a mip chain (simple average). The Help row
# draws the 176 px crop in the 88-unit symbol view, which the solid menu's engine DPI curve (SDD 46 section 4: 720 ->
# 0.666, 1080 -> 1.0, 2160 -> 2.0) puts at about 59 px at 1280x720, 88 px at 1920x1080 and 176 px at 4K. A 2x or 3x
# minification without mips samples only some of the texels and breaks the thin dashed lines up; with the chain the
# 88 px mip is an exact box-filtered half (1080p) and 720p blends the 88 and 44 px mips, while 4K reads the top mip
# 1:1. The chain costs a third more of a 121 KB texture.
# Run: UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript -script="<abs path>/Tools/UI/import_glossary_crops.py"
#      -unattended -nosplash -nullrhi (add -stdout -FullStdOutLogOutput to see the SOLGlossaryCrops log lines)
import hashlib
import os
import re
import unreal

HASH_TAG = "SOLSourceMD5"
DONE_MARKER = "DONE"
# The texture properties set after the import, in order; every value is part of the tag
TEXTURE_SETTINGS = (
    ("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON),
    ("srgb", True),
    ("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE),
    ("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI),
    ("never_stream", True),
    ("address_x", unreal.TextureAddress.TA_CLAMP),
    ("address_y", unreal.TextureAddress.TA_CLAMP),
)

EAL = unreal.EditorAssetLibrary
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, "..", ".."))
CONSTANTS_FILE = os.path.join(PROJECT_DIR, "Source", "SOLTest", "SOLConstants.h")
IMAGE_DIR = os.path.join(PROJECT_DIR, "Docs", "Images")

# One GLOSSARY_CROPS row: { TEXT("Id"), TEXT("ImageFile"), TEXT("Texture"), Left, Top }
ROW_PATTERN = re.compile(r'\{\s*TEXT\("([^"]+)"\)\s*,\s*TEXT\("([^"]+)"\)\s*,\s*TEXT\("([^"]+)"\)\s*,\s*-?\d+\s*,'
                         r'\s*-?\d+\s*\}')


# Logs one line with the script's prefix
def log(message):
    unreal.log("SOLGlossaryCrops: " + message)


# Returns the (id, image file, texture object path) rows of SOL::Paths::GLOSSARY_CROPS from SOLConstants.h
def crop_rows():
    with open(CONSTANTS_FILE, "r", encoding="utf-8") as handle:
        text = handle.read()
    table = re.search(r"\bGLOSSARY_CROPS\[\]\s*=\s*\{(.*?)\n\s*\};", text, re.DOTALL)
    if table is None:
        raise RuntimeError("SOLGlossaryCrops: GLOSSARY_CROPS not found in SOLConstants.h")
    rows = ROW_PATTERN.findall(table.group(1))
    if not rows:
        raise RuntimeError("SOLGlossaryCrops: GLOSSARY_CROPS has no rows")
    # every row opener must have parsed (a reformatted row would otherwise be skipped without a word)
    openers = len(re.findall(r"\{\s*TEXT\(", table.group(1)))
    if openers != len(rows):
        raise RuntimeError("SOLGlossaryCrops: GLOSSARY_CROPS has %d rows but %d parsed (update ROW_PATTERN)"
                           % (openers, len(rows)))
    return rows


# Returns the MD5 hex digest of a file
def file_md5(path):
    with open(path, "rb") as handle:
        return hashlib.md5(handle.read()).hexdigest()


# Returns a setting value's text for the tag (an enum's name, else its str)
def setting_text(value):
    return value.name if hasattr(value, "name") else str(value)


# Returns a source file path, failing when it is missing
def require(path):
    if not os.path.isfile(path):
        raise RuntimeError("SOLGlossaryCrops: missing source %s (run Tools\\WritePlayerGuide.bat)" % path)
    return path


# Returns the package path of an object path ("/Game/A/T_X.T_X" -> "/Game/A/T_X")
def package_of(object_path):
    return object_path.rsplit(".", 1)[0]


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
        raise RuntimeError("SOLGlossaryCrops: import of %s failed" % package_path)
    asset = EAL.load_asset(package_path)
    if asset is None:
        raise RuntimeError("SOLGlossaryCrops: %s did not load after import" % package_path)
    return asset


# Imports the crops that changed and applies the UI settings
def import_crops():
    for crop_id, image_file, texture_path in crop_rows():
        package_path = package_of(texture_path)
        source = require(os.path.join(IMAGE_DIR, image_file))
        tag = "-".join([file_md5(source)] + [setting_text(value) for _, value in TEXTURE_SETTINGS])
        name = split_path(package_path)[1]
        if is_current(package_path, tag):
            log("%s (%s) unchanged, skipped" % (name, crop_id))
            continue
        texture = import_file(source, package_path)
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError("SOLGlossaryCrops: %s is not a Texture2D after import" % package_path)
        for property_name, value in TEXTURE_SETTINGS:
            texture.set_editor_property(property_name, value)
        EAL.set_metadata_tag(texture, HASH_TAG, tag)
        EAL.save_loaded_asset(texture, only_if_is_dirty=False)
        log("%s (%s) imported from %s" % (name, crop_id, image_file))


# Imports every crop whose source or settings changed
def main():
    import_crops()
    log(DONE_MARKER)


main()
