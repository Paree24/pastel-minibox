#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "engine/Engine.h"
#include "MidiLearn.h"

namespace mb
{
/** Minibox plugin: one instrument at a time (switched with the header tabs), host MIDI on any channel,
    host tempo. Host parameters: Macro 1–8, Level, Pan, Mute, Master Volume, Delay Send Return and
    Reverb Return (14 in all). */
class MiniboxProcessor : public juce::AudioProcessor
{
public:
    MiniboxProcessor();
    ~MiniboxProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Minibox"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    Engine& engine() { return eng; }
    DeviceProcessor* device() { return eng.pluginDevice(); }
    /** Switch the instrument (message thread). Loads the device's default preset. */
    void switchInstrument (DeviceType);
    /** Called by the editor when a macro changes in the UI, so the host sees it (as a gesture when the
        UI moved it, which also arms MIDI learn). */
    void macroChangedByUi (int macro, float value);
    /** Step the instrument's presets (message thread): the MIDI-learned preset knob without an editor. */
    void stepPreset (int delta);
    /** Global MIDI learn of the host parameters (~/.config/PastelMidiLearn/Minibox.xml). */
    pastel::MidiLearn& midiLearn() { return *learn; }
    /** Set by an open editor so the preset knob steps exactly like its header arrows. */
    std::function<void (int)> presetStepInEditor;

    std::array<juce::AudioParameterFloat*, 8> macroParams {};
    juce::AudioParameterFloat *levelParam = nullptr, *panParam = nullptr, *masterParam = nullptr, *delayParam = nullptr, *reverbParam = nullptr;
    juce::AudioParameterBool* muteParam = nullptr;

private:
    void syncHostParameters();
    void pullMacrosFromDevice();
    std::unique_ptr<pastel::MidiLearn> learn;   // made after the parameters exist
    Engine eng { Engine::Mode::Plugin };
    std::array<float, 14> lastHost {};
    std::array<float, 8> lastMacro {};
    double sampleRate = 48000.0;
    juce::MidiBuffer chunk;
    juce::MidiBuffer kept;   // the block's MIDI minus the CCs the MIDI learn takes
};
} // namespace mb
