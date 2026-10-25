#pragma once
#include <juce_core/juce_core.h>
#include <functional>

namespace mbtest
{
struct Ctx
{
    int failures = 0, checks = 0;
    juce::String current;
    void check (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok) { ++failures; std::printf ("  FAIL [%s] %s\n", current.toRawUTF8(), what.toRawUTF8()); }
    }
};

using TestFn = std::function<void (Ctx&)>;
struct Registrar { Registrar (const char* name, TestFn fn); };
#define MB_TEST(name) static void name (mbtest::Ctx&); static mbtest::Registrar reg_##name (#name, name); static void name (mbtest::Ctx& ctx)

int runAll (const juce::StringArray& args);
} // namespace mbtest
