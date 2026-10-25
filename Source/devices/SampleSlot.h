#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <atomic>

namespace mb
{
/** Decoded audio. Mono or stereo, kept at the file's own sample rate. */
struct SampleData
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 48000.0;
    juce::String ref;    // "factory:<name>" or an absolute path
    juce::String name;
    std::vector<float> peaks;   // 512 min/max pairs for the waveform display
    int length() const { return buffer.getNumSamples(); }
    void computePeaks();
};
using SamplePtr = std::shared_ptr<const SampleData>;

/** Where factory content lives: next to the binary (bundle Resources) or in the source tree. */
juce::File factoryDir();
juce::File resolveSampleRef (const juce::String& ref);
juce::String refForFile (const juce::File&);
SamplePtr loadSample (const juce::String& ref);      // synchronous decode (IO worker or tests)

/** A sample pointer the audio thread can read while the message thread swaps it. A spin lock
    guards only the pointer copy; replaced buffers are retired and freed on the message thread. */
class SampleSlot
{
public:
    SamplePtr get() const { juce::SpinLock::ScopedLockType l (lock); return current; }
    void set (SamplePtr p);
    /** Audio thread: raw pointer valid until the next set() + collect(). */
    const SampleData* audio() const { juce::SpinLock::ScopedTryLockType l (lock); return l.isLocked() ? current.get() : cachedRaw; }
    void refreshCache() { juce::SpinLock::ScopedLockType l (lock); cachedRaw = current.get(); }

private:
    mutable juce::SpinLock lock;
    SamplePtr current;
    const SampleData* cachedRaw = nullptr;
};

/** Holds replaced sample buffers until the message thread frees them (never on the audio thread). */
void retireSample (SamplePtr);
void collectRetiredSamples();

/** While set, sample loads requested by devices decode synchronously (offline export, tests). */
std::atomic<bool>& forceSyncSampleLoads();

/** Background decoding: load `ref` on a worker, then hand the result to `done` on the message thread. */
void loadSampleAsync (const juce::String& ref, std::function<void (SamplePtr)> done);
} // namespace mb
