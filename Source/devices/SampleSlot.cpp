#include "SampleSlot.h"
#include <juce_events/juce_events.h>

namespace mb
{
void SampleData::computePeaks()
{
    const int n = length();
    peaks.assign (1024, 0.0f);
    if (n == 0) return;
    for (int b = 0; b < 512; ++b)
    {
        const int s0 = (int) ((int64_t) b * n / 512), s1 = std::max (s0 + 1, (int) ((int64_t) (b + 1) * n / 512));
        float lo = 0.0f, hi = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* d = buffer.getReadPointer (ch);
            for (int i = s0; i < std::min (s1, n); ++i) { lo = std::min (lo, d[i]); hi = std::max (hi, d[i]); }
        }
        peaks[(size_t) b * 2] = lo; peaks[(size_t) b * 2 + 1] = hi;
    }
}

juce::File factoryDir()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    const juce::File candidates[] = {
        exe.getParentDirectory().getChildFile ("Factory"),
        exe.getParentDirectory().getParentDirectory().getChildFile ("Resources/Factory"),
        exe.getParentDirectory().getParentDirectory().getParentDirectory().getChildFile ("Resources/Factory"),
#ifdef MINIBOX_SOURCE_DIR
        juce::File (MINIBOX_SOURCE_DIR).getChildFile ("Factory"),
#endif
    };
    for (auto& c : candidates)
        if (c.getChildFile ("samples").isDirectory()) return c;
    return candidates[0];
}

juce::File resolveSampleRef (const juce::String& ref)
{
    if (ref.startsWith ("factory:"))
        return factoryDir().getChildFile ("samples").getChildFile (ref.fromFirstOccurrenceOf ("factory:", false, false) + ".wav");
    return juce::File (ref);
}

juce::String refForFile (const juce::File& f)
{
    const auto samples = factoryDir().getChildFile ("samples");
    if (f.isAChildOf (samples)) return "factory:" + f.getFileNameWithoutExtension();
    return f.getFullPathName();
}

SamplePtr loadSample (const juce::String& ref)
{
    const auto file = resolveSampleRef (ref);
    if (! file.existsAsFile()) return {};
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (! reader) return {};
    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) reader->sampleRate * 60);
    auto d = std::make_shared<SampleData>();
    d->buffer.setSize ((int) std::min (2u, reader->numChannels), len);
    reader->read (&d->buffer, 0, len, 0, true, true);
    d->sampleRate = reader->sampleRate;
    d->ref = ref;
    d->name = file.getFileNameWithoutExtension();
    d->computePeaks();
    return d;
}

void SampleSlot::set (SamplePtr p)
{
    SamplePtr old;
    {
        juce::SpinLock::ScopedLockType l (lock);
        old = std::move (current);
        current = std::move (p);
        cachedRaw = current.get();
    }
    retireSample (std::move (old));
}

namespace
{
juce::CriticalSection& retireLock() { static juce::CriticalSection c; return c; }
std::vector<std::pair<SamplePtr, juce::uint32>>& retired() { static std::vector<std::pair<SamplePtr, juce::uint32>> r; return r; }
juce::ThreadPool& ioPool() { static juce::ThreadPool p (2); return p; }
} // namespace

std::atomic<bool>& forceSyncSampleLoads() { static std::atomic<bool> f { false }; return f; }

void retireSample (SamplePtr p)
{
    if (! p) return;
    const juce::ScopedLock l (retireLock());
    retired().push_back ({ std::move (p), juce::Time::getMillisecondCounter() });
}

void collectRetiredSamples()
{
    std::vector<SamplePtr> drop;
    {
        const juce::ScopedLock l (retireLock());
        const auto now = juce::Time::getMillisecondCounter();
        auto& r = retired();
        // keep buffers for half a second so no audio block can still be reading them
        for (auto it = r.begin(); it != r.end();)
            if (now - it->second > 500) { drop.push_back (std::move (it->first)); it = r.erase (it); }
            else ++it;
    }
    // `drop` frees here, on the calling (message) thread.
}

void loadSampleAsync (const juce::String& ref, std::function<void (SamplePtr)> done)
{
    ioPool().addJob ([ref, done]
    {
        auto p = loadSample (ref);
        juce::MessageManager::callAsync ([p, done] { done (p); });
    });
}
} // namespace mb
