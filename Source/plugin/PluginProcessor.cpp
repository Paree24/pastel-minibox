#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "app/presets/PresetBank.h"
#include "model/PluginState.h"

namespace mb
{
MiniboxProcessor::MiniboxProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    for (int m = 0; m < 8; ++m)
    {
        macroParams[(size_t) m] = new juce::AudioParameterFloat (juce::ParameterID ("macro" + juce::String (m + 1), 1), "Macro " + juce::String (m + 1), 0.0f, 1.0f, 0.5f);
        addParameter (macroParams[(size_t) m]);
    }
    levelParam = new juce::AudioParameterFloat (juce::ParameterID ("level", 1), "Level", 0.0f, 1.0f, 0.8f);
    panParam = new juce::AudioParameterFloat (juce::ParameterID ("pan", 1), "Pan", -1.0f, 1.0f, 0.0f);
    muteParam = new juce::AudioParameterBool (juce::ParameterID ("mute", 1), "Mute", false);
    masterParam = new juce::AudioParameterFloat (juce::ParameterID ("masterVolume", 1), "Master Volume", 0.0f, 1.2f, 0.8f);
    delayParam = new juce::AudioParameterFloat (juce::ParameterID ("delayReturn", 1), "Delay Send Return", 0.0f, 1.0f, 0.8f);
    reverbParam = new juce::AudioParameterFloat (juce::ParameterID ("reverbReturn", 1), "Reverb Return", 0.0f, 1.0f, 0.8f);
    for (auto* p : std::initializer_list<juce::AudioProcessorParameter*> { levelParam, panParam, muteParam, masterParam, delayParam, reverbParam }) addParameter (p);
    eng.project().master.glue = 0.0f;   // the plugin is one instrument: no bus compression by default
    switchInstrument (DeviceType::Poly);
    learn = std::make_unique<pastel::MidiLearn> (*this, "Minibox");
    learn->onPresetStep = [this] (int delta) { if (presetStepInEditor) presetStepInEditor (delta); else stepPreset (delta); };
}

MiniboxProcessor::~MiniboxProcessor() = default;

bool MiniboxProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MiniboxProcessor::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    eng.prepare (sr, std::max (512, block));
    setLatencySamples (eng.master().latency());
}

void MiniboxProcessor::switchInstrument (DeviceType t)
{
    auto d = createDevice (t);
    presets::loadDefault (*d);
    auto old = eng.setDevice (0, 0, std::move (d));
    old.reset();
    pullMacrosFromDevice();
}

void MiniboxProcessor::pullMacrosFromDevice()
{
    if (auto* dev = eng.pluginDevice())
        for (int m = 0; m < 8; ++m)
        {
            lastMacro[(size_t) m] = dev->getMacro (m);
            macroParams[(size_t) m]->setValueNotifyingHost (lastMacro[(size_t) m]);
            lastHost[(size_t) m] = macroParams[(size_t) m]->get();
        }
}

void MiniboxProcessor::stepPreset (int delta)
{
    auto* d = eng.pluginDevice();
    if (d == nullptr) return;
    auto list = presets::list (d->type());
    if (list.empty()) return;
    int cur = -1;
    for (int i = 0; i < (int) list.size(); ++i) if (list[(size_t) i].name == d->presetName) { cur = i; break; }
    const int n = (int) list.size();
    const int next = cur < 0 ? (delta > 0 ? 0 : n - 1) : ((cur + delta) % n + n) % n;
    presets::apply (*d, list[(size_t) next]);
    pullMacrosFromDevice();
}

void MiniboxProcessor::macroChangedByUi (int m, float v)
{
    auto* p = macroParams[(size_t) m];
    // the host (or a learned CC) already wrote the parameter when it is equal: only UI moves are gestures
    const bool fromUi = std::abs (p->get() - v) > 1.0e-5f;
    lastMacro[(size_t) m] = v;
    if (fromUi) p->beginChangeGesture();
    p->setValueNotifyingHost (v);
    if (fromUi) p->endChangeGesture();
    lastHost[(size_t) m] = p->get();
}

void MiniboxProcessor::syncHostParameters()
{
    auto* dev = eng.pluginDevice();
    for (int m = 0; m < 8 && dev != nullptr; ++m)
    {
        const float hv = macroParams[(size_t) m]->get();
        if (std::abs (hv - lastHost[(size_t) m]) > 1.0e-5f) { dev->setMacro (m, hv); lastMacro[(size_t) m] = hv; }   // host automation
        lastHost[(size_t) m] = hv;
    }
    auto& mixer = eng.pluginMixer();
    auto& master = eng.project().master;
    mixer.level = levelParam->get();
    mixer.pan = panParam->get();
    mixer.mute = muteParam->get();
    master.volume = masterParam->get();
    master.delayReturn = delayParam->get();
    master.reverbReturn = reverbParam->get();
}

void MiniboxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    learn->process (midi);   // learned CCs move the host parameters before they reach the engine
    syncHostParameters();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            const double bpm = pos->getBpm().orFallback (120.0);
            eng.project().bpm = bpm;
            // "playing" without a position would pin every synced step to beat 0
            const auto ppq = pos->getPpqPosition();
            eng.setHostTransport (bpm, ppq.orFallback (0.0), pos->getIsPlaying() && ppq.hasValue());
        }
    const int n = buffer.getNumSamples();
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : l;
    const int maxBlock = eng.getMaxBlock();
    for (int pos = 0; pos < n; pos += maxBlock)
    {
        const int len = std::min (maxBlock, n - pos);
        chunk.clear();
        chunk.addEvents (midi, pos, len, -pos);
        eng.process (l + pos, r + pos, len, &chunk);
    }
    midi.clear();
}

juce::AudioProcessorEditor* MiniboxProcessor::createEditor() { return new MiniboxEditor (*this); }

void MiniboxProcessor::getStateInformation (juce::MemoryBlock& out)
{
    std::vector<std::pair<juce::String, float>> host;
    for (auto* p : getParameters())
        if (auto* pw = dynamic_cast<juce::AudioProcessorParameterWithID*> (p)) host.push_back ({ pw->paramID, p->getValue() });
    const Engine::ScopedEdit edit (eng);
    out = pluginstate::toBytes (pluginstate::make (eng.pluginDevice(), host, eng.pluginMixer(), eng.project().master));
}

void MiniboxProcessor::setStateInformation (const void* data, int size)
{
    auto state = juce::ValueTree::readFromData (data, (size_t) size);
    if (! state.isValid()) return;
    // Sessions saved by the older four-slot rack keep slot 1 and drop the rest.
    auto dt = state.getChildWithName ("Device");
    if (! dt.isValid())
        if (auto slots = state.getChildWithName ("Slots"); slots.isValid() && slots.getNumChildren() > 0) dt = slots.getChild (0).getChildWithName ("Device");
    if (dt.isValid())
    {
        const auto type = deviceTypeFromId (dt.getProperty ("type").toString());
        if (type != DeviceType::Count)
        {
            auto d = createDevice (type);
            d->load (dt);
            auto old = eng.setDevice (0, 0, std::move (d));
            old.reset();
        }
    }
    {
        const Engine::ScopedEdit edit (eng);
        pluginstate::applyMix (state, eng.pluginMixer(), eng.project().master);
    }
    for (auto* p : getParameters())
        if (auto* pw = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (state.hasProperty ("p_" + pw->paramID)) p->setValueNotifyingHost ((float) state.getProperty ("p_" + pw->paramID));
    if (auto* d = eng.pluginDevice())
        for (int m = 0; m < 8; ++m) { lastMacro[(size_t) m] = d->getMacro (m); lastHost[(size_t) m] = macroParams[(size_t) m]->get(); }
}
} // namespace mb

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mb::MiniboxProcessor(); }
