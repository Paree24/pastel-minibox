#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "Tests.h"
#include "plugin/MidiLearn.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    {
        // plugin MIDI learn maps go to a scratch folder, never the user's ~/.config
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("minibox-test-pastel-midilearn");
        dir.deleteRecursively();
        pastel::MidiLearn::setDirectoryForTest (dir);
    }
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    return mbtest::runAll (args);
}
