# SOLTest
# Copyright © 2026 Acid Rain Studios LLC
#
# Creates the sound classes the settings volumes control (SDD 8, part 8d), headless and idempotent:
#   /Game/SOL/Audio/SC_SOLMaster   master class, parent of the two below
#   /Game/SOL/Audio/SC_SOLSfx      effects: every combat wave /Game/SOL/Combat/Audio/S_SOL* is assigned to it
#   /Game/SOL/Audio/SC_SOLMusic    music (no music content yet; the class is ready for it)
# Paths must match SOL::Paths::SOUND_CLASS_* in Source/SOLTest/SOLConstants.h.
# Run: UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript -script="<abs path>/Tools/CreateSOLAudioClasses.py"
#      -unattended (add -stdout -FullStdOutLogOutput to see the SOLAudio log lines)
import unreal

AUDIO_DIR = "/Game/SOL/Audio"
MASTER_NAME = "SC_SOLMaster"
SFX_NAME = "SC_SOLSfx"
MUSIC_NAME = "SC_SOLMusic"
COMBAT_AUDIO_DIR = "/Game/SOL/Combat/Audio"
DONE_MARKER = "DONE"

EAL = unreal.EditorAssetLibrary


# Logs one line with the script's prefix
def log(message):
    unreal.log("SOLAudio: " + message)


# Loads a sound class, creating it when it does not exist yet
def get_or_create_class(name):
    path = "%s/%s" % (AUDIO_DIR, name)
    if EAL.does_asset_exist(path):
        return EAL.load_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    sound_class = tools.create_asset(name, AUDIO_DIR, unreal.SoundClass, unreal.SoundClassFactory())
    if sound_class is None:
        raise RuntimeError("SOLAudio: failed to create %s" % path)
    log("created %s" % path)
    return sound_class


# Makes the children of the master class (the property change also sets each child's parent)
def link_children(master, children):
    current = list(master.get_editor_property("child_classes"))
    for child in children:
        if child not in current:
            current.append(child)
    master.set_editor_property("child_classes", current)
    for child in children:
        child.set_editor_property("parent_class", master)


# Assigns the effects class to every combat sound wave
def assign_combat_waves(sfx):
    changed = []
    for path in EAL.list_assets(COMBAT_AUDIO_DIR, recursive=False):
        asset = EAL.load_asset(path)
        if isinstance(asset, unreal.SoundWave):
            if asset.get_editor_property("sound_class_object") != sfx:
                asset.set_editor_property("sound_class_object", sfx)
                changed.append(asset)
            log("%s -> %s" % (asset.get_name(), SFX_NAME))
    return changed


# Creates and links the classes, assigns the waves and saves what changed
def main():
    master = get_or_create_class(MASTER_NAME)
    sfx = get_or_create_class(SFX_NAME)
    music = get_or_create_class(MUSIC_NAME)
    link_children(master, [sfx, music])
    waves = assign_combat_waves(sfx)
    for asset in [master, sfx, music] + waves:
        EAL.save_loaded_asset(asset, only_if_is_dirty=False)
    log("parent of %s is %s" % (SFX_NAME, sfx.get_editor_property("parent_class").get_name()))
    log(DONE_MARKER)


main()
