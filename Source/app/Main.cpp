#include <juce_audio_utils/juce_audio_utils.h>
#include "MainComponent.h"

namespace mb
{
/** Feeds the audio device from the engine, with every connected MIDI input. */
struct AudioCallback : juce::AudioIODeviceCallback
{
    AudioCallback (Engine& e, juce::MidiMessageCollector& c) : engine (e), collector (c) {}
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        const int block = std::max (512, d->getCurrentBufferSizeSamples());
        engine.prepare (d->getCurrentSampleRate(), block);
        collector.reset (d->getCurrentSampleRate());
        midi.ensureSize (4096);
    }
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        midi.clear();
        collector.removeNextBlockOfMessages (midi, n);
        if (numOut <= 0) return;
        float* l = out[0];
        float* r = numOut > 1 ? out[1] : scratch (n);
        // render in engine-sized chunks, shifting MIDI into each chunk
        const int maxBlock = engine.getMaxBlock();
        for (int pos = 0; pos < n; pos += maxBlock)
        {
            const int len = std::min (maxBlock, n - pos);
            chunk.clear();
            chunk.addEvents (midi, pos, len, -pos);
            engine.process (l + pos, r + pos, len, &chunk);
        }
        for (int ch = 2; ch < numOut; ++ch) if (out[ch] != nullptr) juce::FloatVectorOperations::clear (out[ch], n);
    }
    float* scratch (int n) { if ((int) spare.size() < n) spare.resize ((size_t) n); return spare.data(); }

    Engine& engine;
    juce::MidiMessageCollector& collector;
    juce::MidiBuffer midi, chunk;
    std::vector<float> spare;
};

/** Every MIDI input, with the name of its port: mapped controller messages (and messages while a control
    is armed for MIDI learn) go to the map, everything else plays the instruments. */
struct MidiRouter : juce::MidiInputCallback
{
    MidiRouter (MidiMap& m, juce::MidiMessageCollector& c) : map (m), collector (c) {}
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& msg) override
    {
        if (! map.filter (source != nullptr ? source->getName() : juce::String(), msg)) collector.handleIncomingMidiMessage (source, msg);
    }
    MidiMap& map;
    juce::MidiMessageCollector& collector;
};

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (Engine& e, juce::AudioDeviceManager& dm)
        : DocumentWindow ("Pastel Minibox", juce::Colour (0xff141518), DocumentWindow::allButtons), deviceManager (dm)
    {
        setUsingNativeTitleBar (true);
        main = new MainComponent (e);
        main->onTitleChanged = [this] { setName (main->windowTitle()); };
        main->showAudioSettings = [this] { showAudioSettings(); };
        main->createAudioSettings = [this] () -> juce::Component*
        {
            auto* sel = new juce::AudioDeviceSelectorComponent (deviceManager, 0, 0, 2, 2, true, false, true, false);
            sel->setSize (520, 440);
            return sel;
        };
        setContentOwned (main, false);
        setResizable (true, true);
        // any shape: the layout widens with the window (16:10 → 1600 wide, 16:9 → 1778 …; MainComponent::resized)
        constrainer.setSizeLimits (960, 600, 6000, 2500);
        setConstrainer (&constrainer);
        // open in the screen's own shape
        const auto area = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
        const double aspect = juce::jlimit (1.6, 2.4, area.getWidth() / (double) std::max (1, area.getHeight()));
        const double w = 1000.0 * aspect;
        const double scale = std::min ({ 1.0, area.getWidth() * 0.94 / w, area.getHeight() * 0.92 / 1000.0 });
        centreWithSize ((int) (w * scale), (int) (1000 * scale));
        setVisible (true);
        main->grabKeyboardFocus();
        // the window is mapped asynchronously: focus again once it is on screen (keys work before any click)
        juce::Component::SafePointer<MainComponent> safe (main);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->grabKeyboardFocus(); });
        setName (main->windowTitle());
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    // the window frame takes focus when it opens or is activated: the app gets it (and its keys) at once
    void focusGained (FocusChangeType) override { if (main != nullptr) main->grabKeyboardFocus(); }
    void activeWindowStatusChanged() override { DocumentWindow::activeWindowStatusChanged(); if (isActiveWindow() && main != nullptr && ! main->hasKeyboardFocus (true)) main->grabKeyboardFocus(); }
    bool keyPressed (const juce::KeyPress& k) override { return main != nullptr && main->keyPressed (k); }
    bool keyStateChanged (bool down) override { return main != nullptr && main->keyStateChanged (down); }
    MainComponent& component() { return *main; }

    void showAudioSettings()
    {
        auto* sel = new juce::AudioDeviceSelectorComponent (deviceManager, 0, 0, 2, 2, true, false, true, false);
        sel->setSize (520, 440);
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (sel);
        o.dialogTitle = "Audio and MIDI";
        o.dialogBackgroundColour = juce::Colour (0xff20232A);
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }

private:
    MainComponent* main = nullptr;
    juce::AudioDeviceManager& deviceManager;
    juce::ComponentBoundsConstrainer constrainer;
};

class MiniboxApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Pastel Minibox"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName = "Pastel Minibox";
        opts.filenameSuffix = ".settings";
        opts.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (opts);

        engine = std::make_unique<Engine>();
        callback = std::make_unique<AudioCallback> (*engine, collector);
        std::unique_ptr<juce::XmlElement> saved (settings->getXmlValue ("audio"));
        deviceManager.initialise (0, 2, saved.get(), true);
        for (auto& in : juce::MidiInput::getAvailableDevices()) deviceManager.setMidiInputDeviceEnabled (in.identifier, true);
        window = std::make_unique<MainWindow> (*engine, deviceManager);
        router = std::make_unique<MidiRouter> (window->component().midiMap(), collector);
        window->component().enableAutosave();
        deviceManager.addMidiInputDeviceCallback ({}, router.get());
        deviceManager.addAudioCallback (callback.get());
        const auto arg = commandLine.unquoted().trim();
        if (arg.endsWithIgnoreCase (".minibox") && juce::File (arg).existsAsFile()) window->component().openFile (juce::File (arg));
    }

    void shutdown() override
    {
        if (auto xml = deviceManager.createStateXml()) settings->setValue ("audio", xml.get());
        settings->saveIfNeeded();
        deviceManager.removeAudioCallback (callback.get());
        deviceManager.removeMidiInputDeviceCallback ({}, router.get());
        router.reset();
        window.reset();
        deviceManager.closeAudioDevice();
        callback.reset();
        engine.reset();
    }

    void systemRequestedQuit() override
    {
        if (window == nullptr) { quit(); return; }
        window->component().askToSaveThen ([] { juce::JUCEApplication::getInstance()->quit(); });
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        const auto arg = commandLine.unquoted().trim();
        if (window != nullptr && arg.endsWithIgnoreCase (".minibox")) window->component().openFile (juce::File (arg));
    }

private:
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<Engine> engine;
    juce::AudioDeviceManager deviceManager;
    juce::MidiMessageCollector collector;
    std::unique_ptr<AudioCallback> callback;
    std::unique_ptr<MidiRouter> router;
    std::unique_ptr<MainWindow> window;
};
} // namespace mb

START_JUCE_APPLICATION (mb::MiniboxApplication)
