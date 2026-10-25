#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "Tests.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    return mbtest::runAll (args);
}
