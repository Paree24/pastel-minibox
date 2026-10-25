#pragma once
#include <juce_core/juce_core.h>
#include "devices/DeviceProcessor.h"

namespace mb
{
/** A browsable preset. Factory presets are data files shipped in Factory/presets/<device>/ named
    "NNN Name [Family].mbx-<device>"; a user overwrite of one (OVER) lives in the user folder's Factory/
    subfolder under the same file name and shadows the shipped file until it is deleted (DEL restores). */
struct PresetInfo
{
    juce::String name, family;
    bool factory = true;
    bool shadowed = false;   // factory slot replaced by the user's own version
    int index = -1;          // factory number (NNN), -1 for user presets
    juce::File file;
};

namespace presets
{
/** Factory presets sorted by number (cached; rescan() re-reads the folders). */
const std::vector<PresetInfo>& factory (DeviceType);
std::vector<PresetInfo> user (DeviceType);
/** Factory then user presets. */
std::vector<PresetInfo> list (DeviceType);
std::vector<juce::String> families (DeviceType);
void rescan();

/** Reset the device to defaults, then apply the preset (never overlay-only). */
bool apply (DeviceProcessor&, const PresetInfo&, bool syncSamples = false);
/** The first factory preset: the sound of a new slot or plugin tab. */
void loadDefault (DeviceProcessor&, bool syncSamples = false);
/** The factory entry the device's current preset came from, if any. */
const PresetInfo* findFactory (const DeviceProcessor&);

juce::File factoryDir (DeviceType);
juce::File userDir (DeviceType);                   // Documents/Minibox Presets/<Device>
void setUserRootForTest (const juce::File&);       // empty = Documents/Minibox Presets
juce::File shadowDir (DeviceType);                 // userDir/Factory
juce::String fileExtension (DeviceType);           // ".mbx-poly"
juce::String sanitiseName (juce::String);
bool userPresetExists (DeviceType, const juce::String& name);
bool save (const DeviceProcessor&, const juce::String& name);
bool remove (const PresetInfo&);                   // user preset: delete; shadowed factory: restore the shipped one
bool overwriteFactory (const DeviceProcessor&, const PresetInfo&);

/** Device preset JSON (also used for user preset files and inside projects). */
juce::var toJson (const DeviceProcessor&, const juce::String& name, const juce::String& family);
bool fromJson (DeviceProcessor&, const juce::var&);
} // namespace presets
} // namespace mb
