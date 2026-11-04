# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Imports the three hologram menu sound cues (SDD 42 section 7.1, part 42h), headless and idempotent:
#   Docs/Art/ui-sounds/S_SOLHoloOpen.wav    -> /Game/SOL/UI/Audio/S_SOLHoloOpen
#   Docs/Art/ui-sounds/S_SOLHoloHover.wav   -> /Game/SOL/UI/Audio/S_SOLHoloHover
#   Docs/Art/ui-sounds/S_SOLHoloSelect.wav  -> /Game/SOL/UI/Audio/S_SOLHoloSelect
# Each wave's sound class is set to SC_SOLSfx so the SFX volume slider drives it. Each wave keeps the MD5 of its source
# WAV in a metadata tag: a re-run skips (never re-imports or re-saves) a wave whose source is unchanged and whose class
# is already set, so an unchanged run writes no .uasset.
# Asset paths must match SOL::HoloMenu::CUES in Source/SOLTest/SOLConstants.h.
# Run: UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript -script="<abs path>/Tools/UI/import_holo_menu_sounds.py"
#      -unattended -nosplash -nullrhi (add -stdout -FullStdOutLogOutput to see the SOLHoloSounds log lines)
import hashlib
import os
import unreal

CUE_NAMES = ["S_SOLHoloOpen", "S_SOLHoloHover", "S_SOLHoloSelect"]
DEST_DIR = "/Game/SOL/UI/Audio"
SFX_CLASS_PATH = "/Game/SOL/Audio/SC_SOLSfx"
HASH_TAG = "SOLSourceMD5"
DONE_MARKER = "DONE"

EAL = unreal.EditorAssetLibrary
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SOURCE_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "..", "Docs", "Art", "ui-sounds"))


# Logs one line with the script's prefix
def log(message):
    unreal.log("SOLHoloSounds: " + message)


# Returns a cue's source WAV path, failing when it is missing
def source_of(name):
    source = os.path.join(SOURCE_DIR, name + ".wav")
    if not os.path.isfile(source):
        raise RuntimeError("SOLHoloSounds: missing source %s" % source)
    return source


# Returns the MD5 hex digest of a file
def file_md5(path):
    with open(path, "rb") as handle:
        return hashlib.md5(handle.read()).hexdigest()


# Returns whether a cue's wave exists with this source hash and the effects class (nothing to do)
def is_current(path, md5, sfx):
    if not EAL.does_asset_exist(path):
        return False
    wave = EAL.load_asset(path)
    return (isinstance(wave, unreal.SoundWave) and EAL.get_metadata_tag(wave, HASH_TAG) == md5
            and wave.get_editor_property("sound_class_object") == sfx)


# Builds the import task of one cue WAV (saved by the script after the class and hash are set)
def make_task(name, source):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", source)
    task.set_editor_property("destination_path", DEST_DIR)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("save", False)
    return task


# Imports the changed cues, assigns the effects class and the source hash to each and saves each once
def main():
    sfx = EAL.load_asset(SFX_CLASS_PATH)
    if sfx is None:
        raise RuntimeError("SOLHoloSounds: %s missing, run Tools/CreateSOLAudioClasses.py first" % SFX_CLASS_PATH)

    # Only the cues whose source or class changed are imported
    pending = []
    for name in CUE_NAMES:
        source = source_of(name)
        path = "%s/%s" % (DEST_DIR, name)
        md5 = file_md5(source)
        if is_current(path, md5, sfx):
            log("%s unchanged, skipped" % name)
            continue
        pending.append((name, path, md5, make_task(name, source)))
    if pending:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([entry[3] for entry in pending])

    # A failed import over an existing asset leaves the old wave loadable: the task's own result is checked first
    for name, path, md5, task in pending:
        if len(task.get_editor_property("imported_object_paths")) == 0:
            raise RuntimeError("SOLHoloSounds: import of %s failed" % path)
        wave = EAL.load_asset(path)
        if not isinstance(wave, unreal.SoundWave):
            raise RuntimeError("SOLHoloSounds: %s is not a sound wave after import" % path)
        wave.set_editor_property("sound_class_object", sfx)
        EAL.set_metadata_tag(wave, HASH_TAG, md5)
        EAL.save_loaded_asset(wave, only_if_is_dirty=False)
        log("%s -> %s (%.2f s, %d ch)" % (name, sfx.get_name(), wave.get_editor_property("duration"),
                                          wave.get_editor_property("num_channels")))
    log(DONE_MARKER)


main()
