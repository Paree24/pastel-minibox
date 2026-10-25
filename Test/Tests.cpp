#include "Tests.h"
#include <vector>

namespace mbtest
{
namespace
{
std::vector<std::pair<const char*, TestFn>>& registry() { static std::vector<std::pair<const char*, TestFn>> r; return r; }
}

Registrar::Registrar (const char* name, TestFn fn) { registry().push_back ({ name, std::move (fn) }); }

int runAll (const juce::StringArray& args)
{
    Ctx ctx;
    int run = 0;
    for (auto& [name, fn] : registry())
    {
        // tools (render, balance, measure: some rewrite factory content) only run when named
        const bool tool = juce::String (name).startsWith ("tool_");
        if (args.isEmpty() ? tool : (! args.contains (name) && ! (args.contains ("all") && ! tool))) continue;
        ctx.current = name;
        const int before = ctx.failures;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        fn (ctx);
        std::printf ("%s %-34s %6.0f ms\n", ctx.failures == before ? "ok  " : "FAIL", name,
                     juce::Time::getMillisecondCounterHiRes() - t0);
        ++run;
    }
    std::printf ("\n%d tests, %d checks, %d failures\n", run, ctx.checks, ctx.failures);
    return ctx.failures == 0 ? 0 : 1;
}
} // namespace mbtest
