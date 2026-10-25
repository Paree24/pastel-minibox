#include "Tests.h"
#include "engine/Engine.h"
#include "app/presets/PresetBank.h"
#include "devices/SampleSlot.h"
#include <atomic>
#include <cstdlib>
#include <new>

// Debug allocator hook: counts heap allocations made while `countAllocs` is set on this thread.
static thread_local bool countAllocs = false;
static std::atomic<int> allocCount { 0 };

void* operator new (std::size_t n)
{
    if (countAllocs) allocCount.fetch_add (1);
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

using namespace mb;

MB_TEST (engine_no_audio_thread_allocations)
{
    Engine e;
    e.prepare (48000.0, 256);
    forceSyncSampleLoads() = true;
    const DeviceType types[8] = { DeviceType::Drums, DeviceType::Acid, DeviceType::Poly, DeviceType::FM,
                                  DeviceType::DrumSampler, DeviceType::Mono, DeviceType::Poly, DeviceType::Sampler };
    for (int i = 0; i < 8; ++i)
    {
        auto d = createDevice (types[i]);
        presets::loadDefault (*d, true);
        e.setDevice (i / 4, i % 4, std::move (d));
    }
    forceSyncSampleLoads() = false;
    for (int pat = 0; pat < 2; ++pat)
        for (int t = 0; t < 4; ++t)
        {
            auto& tr = e.project().patterns[(size_t) pat].slots[(size_t) t].track;
            for (int s = 0; s < 16; s += 2)
            {
                tr.steps[(size_t) s].addNote (48 + s); tr.steps[(size_t) s].addNote (55 + s); tr.steps[(size_t) s].ratchet = (uint8_t) (1 + s % 3);
                for (int l = 0; l < 8; ++l) tr.lanes[(size_t) l][(size_t) s].on = true;
            }
            if (auto* lane = tr.lockFor (0, true)) lane->values[4] = 0.5f;
        }
    e.project().metronome = true;
    e.play();
    std::vector<float> l (256), r (256);
    juce::MidiBuffer midi;
    midi.ensureSize (1024);
    for (int i = 0; i < 20; ++i) e.process (l.data(), r.data(), 256, nullptr);   // warm-up
    allocCount = 0;
    countAllocs = true;
    for (int i = 0; i < 400; ++i)
    {
        if (i == 150) e.queuePattern (1);   // pattern switch with tails
        if (i == 300) e.queuePattern (0);
        e.process (l.data(), r.data(), 256, &midi);
    }
    countAllocs = false;
    ctx.check (allocCount.load() == 0, "allocations on the audio thread: " + juce::String (allocCount.load()));
}
