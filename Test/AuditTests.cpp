#include "Tests.h"
#include "engine/Engine.h"
#include "model/Serializer.h"
#include "app/presets/PresetBank.h"
#include "dsp/Pedals.h"
#include "devices/drumsampler/DrumSamplerDevice.h"

// Tests for the fixes from the code review of 2026-10-28 (docs/review-2026-10-28.md): each would have failed before.

using namespace mb;

namespace
{
/** Records the value its one parameter plays with at every note-on (a step lock or the stored value). */
struct LockProbe : DeviceProcessor
{
    LockProbe() : DeviceProcessor (DeviceType::Mono) { layout.cont ("x", "X", 0, 1, 0); finishLayout(); }
    void prepare (double sr, int) override { sampleRate = sr; }
    void reset() override {}
    void handleEvent (const NoteEvent& e) override
    {
        if (e.type == NoteEvent::NoteOn) seen.push_back (pv (0));
        if (e.type == NoteEvent::NoteOff) offs.push_back (e.note);
    }
    void render (float*, float*, int) override {}
    bool anyVoiceActive() const override { return true; }
    bool isSilent() const override { return false; }
    std::vector<float> seen;
    std::vector<int> offs;
};

/** The period (samples) of a device's note: the strongest autocorrelation lag after the first dip. */
double periodOf (DeviceProcessor& dev, int note, int blocks = 30)
{
    dev.reset();
    NoteEvent on; on.type = NoteEvent::NoteOn; on.note = note; on.velocity = 0.9f;
    dev.handleEvent (on);
    std::vector<float> l (512), r (512), x;
    for (int b = 0; b < blocks; ++b)
    {
        std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
        dev.render (l.data(), r.data(), 512);
        if (b >= blocks - 10) x.insert (x.end(), l.begin(), l.end());
    }
    NoteEvent off; off.type = NoteEvent::AllNotesOff; dev.handleEvent (off);
    auto ac = [&] (int lag) { double s = 0; for (size_t i = 0; i + (size_t) lag < x.size(); ++i) s += x[i] * x[i + (size_t) lag]; return s; };
    int lag = 30; while (lag < 1500 && ac (lag) > 0.0) ++lag;
    int best = lag; double bv = -1e30;
    for (int k = lag; k < 1500; ++k) { const double v = ac (k); if (v > bv) { bv = v; best = k; } }
    return best;
}
} // namespace

/** 2.1 / 2.2: a block longer than prepared (8192) renders whole, in pieces; FM and the Sampler no longer overflow
    a 4096-sample stack buffer. */
MB_TEST (audit_large_blocks)
{
    Engine e;
    e.prepare (48000.0, 512);
    auto fm = createDevice (DeviceType::FM);
    presets::loadDefault (*fm, true);
    e.setDevice (0, 0, std::move (fm));
    e.focusTrack = 0;
    std::vector<float> l (8192), r (8192);
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    e.process (l.data(), r.data(), 8192, &m);
    float early = 0.0f, late = 0.0f;
    for (int i = 0; i < 4096; ++i) early = std::max (early, std::abs (l[(size_t) i]));
    for (int i = 4096; i < 8192; ++i) late = std::max (late, std::abs (l[(size_t) i]));
    ctx.check (early > 1.0e-3f && late > 1.0e-3f, "an 8192-sample block at a 512 preparation: sound in both halves");
    // devices prepared for 8192 render it in one go
    Engine big;
    big.prepare (48000.0, 8192);
    for (auto t : { DeviceType::FM, DeviceType::Sampler })
    {
        auto d = createDevice (t);
        presets::loadDefault (*d, true);
        d->prepare (48000.0, 8192);
        NoteEvent on; on.type = NoteEvent::NoteOn; on.note = 60; on.velocity = 0.9f;
        d->handleEvent (on);
        std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
        d->render (l.data(), r.data(), 8192);
        bool finite = true;
        for (float v : l) finite = finite && std::isfinite (v);
        ctx.check (finite, deviceTypeName (t) + ": an 8192-sample render");
    }
}

/** 3.1: drive on a stereo signal keeps the channels apart (one shared Drive mixed L into R). */
MB_TEST (audit_stereo_drive)
{
    dsp::StereoDrive d;
    d.prepare (48000.0f);
    d.setTone (0.5f);
    float rMax = 0.0f;
    for (int i = 0; i < 4800; ++i)
    {
        float l = 0.8f * std::sin (0.05f * (float) i), r = 0.0f;
        d.process (l, r, 1, 0.8f, 1.0f);   // Tube, with its DC blocker
        rMax = std::max (rMax, std::abs (r));
    }
    ctx.check (rMax < 1.0e-6f, "a silent right channel stays silent (" + juce::String (rMax, 7) + ")");
}

/** 3.2: the footage switch: 16' an octave below 8', 4' an octave above (Bassline and the VA's VCO engine). */
MB_TEST (audit_footage)
{
    struct Case { DeviceType type; const char* param; };
    for (auto c : { Case { DeviceType::Mono, "range" }, Case { DeviceType::Poly, "vaOctave" } })
    {
        auto d = createDevice (c.type);
        presets::loadDefault (*d, true);
        d->prepare (48000.0, 512);
        // a plain voice for the pitch detector: one saw, no stack, chorus, LFO, effects
        if (c.type == DeviceType::Poly)
            for (auto [id, v] : { std::pair<const char*, float> { "engine", 1.0f }, { "vaWave", 0.0f }, { "voiceMode", 0.0f }, { "detune", 0.0f },
                                  { "chorusMode", 0.0f }, { "chorusMix", 0.0f }, { "delayMix", 0.0f }, { "reverb", 0.0f }, { "lfoPitch", 0.0f },
                                  { "sub", 0.0f }, { "noise", 0.0f }, { "drift", 0.0f }, { "cutoff", 8000.0f }, { "aSustain", 1.0f } })
                d->set (id, v);
        d->set (c.param, 1.0f);
        const double p8 = periodOf (*d, 57);
        d->set (c.param, 0.0f);
        const double p16 = periodOf (*d, 57);
        d->set (c.param, 2.0f);
        const double p4 = periodOf (*d, 57);
        ctx.check (std::abs (p16 / p8 - 2.0) < 0.06 && std::abs (p8 / p4 - 2.0) < 0.06,
                   deviceTypeName (c.type) + ": 16' / 8' / 4' periods " + juce::String (p16, 1) + " / " + juce::String (p8, 1) + " / " + juce::String (p4, 1));
    }
}

/** 3.3: the pedal board's dry signal waits for its oversampled path: a part mix is one aligned impulse, not two. */
MB_TEST (audit_acid_pedal_alignment)
{
    dsp::pedal::Board b;
    b.prepare (48000.0f);
    std::vector<float> out;
    for (int i = 0; i < 64; ++i) out.push_back (b.process (i == 0 ? 1.0f : 0.0f, 0, 0.0f, 0.5f, 0.5f));
    int peak = 0;
    for (int i = 0; i < 64; ++i) if (std::abs (out[(size_t) i]) > std::abs (out[(size_t) peak])) peak = i;
    float before = 0.0f;
    for (int i = 0; i < peak - 4; ++i) before = std::max (before, std::abs (out[(size_t) i]));
    ctx.check (peak == dsp::pedal::Board::kLatency, "the impulse comes out once, at the board's latency (" + juce::String (peak) + ")");
    ctx.check (before < 0.02f, "no early dry copy of it (" + juce::String (before, 4) + ")");
}

/** 2.4: a damaged project file (pan 5, NaN-ish values) loads clamped and renders finite audio. */
MB_TEST (audit_project_values_clamped)
{
    Engine e;
    e.prepare (48000.0, 512);
    auto d = createDevice (DeviceType::Poly);
    presets::loadDefault (*d, true);
    e.setDevice (0, 0, std::move (d));
    const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("minibox_audit.minibox");
    ctx.check (serializer::save (e, f).ok, "saved");
    // corrupt the mixer and master values inside the archive's project JSON
    juce::String json;
    {
        juce::ZipFile zip (f);
        for (int i = 0; i < zip.getNumEntries(); ++i)
            if (zip.getEntry (i)->filename.endsWith (".json"))
            {
                std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (i));
                json = in->readEntireStreamAsString();
            }
    }
    ctx.check (json.contains ("\"pan\""), "the project JSON holds the mixer");
    auto v = juce::JSON::parse (json);
    v.getProperty ("patterns", {})[0].getProperty ("slots", {})[0].getProperty ("mixer", {}).getDynamicObject()->setProperty ("pan", 5.0);
    v.getProperty ("master", {}).getDynamicObject()->setProperty ("volume", 40.0);
    Project p;
    serializer::projectFromJson (p, v);
    ctx.check (p.patterns[0].slots[0].mixer.pan <= 1.0f && p.patterns[0].slots[0].mixer.pan >= -1.0f, "pan clamped (" + juce::String (p.patterns[0].slots[0].mixer.pan) + ")");
    ctx.check (p.master.volume <= 1.2f, "master volume clamped");
    f.deleteFile();
}

/** 4.1 / 4.2: a step's lock reaches its notes, early (negative micro-timing) ones too. */
MB_TEST (audit_locks_reach_early_notes)
{
    Engine e;
    e.prepare (48000.0, 128);
    auto probe = std::make_unique<LockProbe>();
    auto* pr = probe.get();
    e.setDevice (0, 0, std::move (probe));
    e.project().countIn = false;
    auto& tr = e.project().patterns[0].slots[0].track;
    tr.steps[4].addNote (60);
    tr.steps[4].microTiming = -12;   // a quarter step early
    tr.lockFor (0, true)->values[4] = 0.75f;
    e.play();
    std::vector<float> l (128), r (128);
    for (int i = 0; i < 400; ++i) e.process (l.data(), r.data(), 128, nullptr);   // past step 5 (0.5 s at 120 BPM)
    ctx.check (! pr->seen.empty() && std::abs (pr->seen[0] - 0.75f) < 1.0e-4f, "the early note plays with its step's lock (" + juce::String (pr->seen.empty() ? -1.0f : pr->seen[0]) + ")");
}

/** 3.7: Acid: releasing the sounding key returns to the key still held (it used to keep droning). */
MB_TEST (audit_acid_last_note_priority)
{
    auto d = createDevice (DeviceType::Acid);
    presets::loadDefault (*d, true);
    d->prepare (48000.0, 512);
    const double p48 = periodOf (*d, 48);
    d->reset();
    auto ev = [&] (NoteEvent::Type t, int n) { NoteEvent e; e.type = t; e.note = n; e.velocity = 0.8f; d->handleEvent (e); };
    ev (NoteEvent::NoteOn, 48);
    ev (NoteEvent::NoteOn, 55);
    ev (NoteEvent::NoteOff, 55);
    std::vector<float> l (512), r (512), x;
    for (int b = 0; b < 30; ++b)
    {
        std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
        d->render (l.data(), r.data(), 512);
        if (b >= 20) x.insert (x.end(), l.begin(), l.end());
    }
    auto ac = [&] (int lag) { double s = 0; for (size_t i = 0; i + (size_t) lag < x.size(); ++i) s += x[i] * x[i + (size_t) lag]; return s; };
    int lag = 30; while (lag < 1500 && ac (lag) > 0.0) ++lag;
    int best = lag; double bv = -1e30;
    for (int k = lag; k < 1500; ++k) { const double v = ac (k); if (v > bv) { bv = v; best = k; } }
    ctx.check (std::abs (best / p48 - 1.0) < 0.03, "back on the held key's pitch (" + juce::String (best) + " vs " + juce::String (p48, 1) + ")");
}

/** 4.7: the sustain pedal is one pedal: switching the selected track with it down still releases the held keys. */
MB_TEST (audit_sustain_pedal_global)
{
    Engine e;
    e.prepare (48000.0, 128);
    auto a = std::make_unique<LockProbe>();
    auto* pa = a.get();
    e.setDevice (0, 0, std::move (a));
    e.setDevice (0, 1, std::make_unique<LockProbe>());
    std::vector<float> l (128), r (128);
    auto send = [&] (const juce::MidiMessage& m) { juce::MidiBuffer b; b.addEvent (m, 0); e.process (l.data(), r.data(), 128, &b); };
    e.focusTrack = 0;
    send (juce::MidiMessage::controllerEvent (1, 64, 127));
    send (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    send (juce::MidiMessage::noteOff (1, 60));
    ctx.check (pa->offs.empty(), "pedal down: the key is held");
    e.focusTrack = 1;
    send (juce::MidiMessage::controllerEvent (1, 64, 0));
    ctx.check (pa->offs.size() == 1 && pa->offs[0] == 60, "pedal up on another track: track 1's key is released");
}

/** 3.12: Drum Sampler: a step lock on Start moves where the pad starts. */
MB_TEST (audit_drum_sampler_start_lock)
{
    auto d = std::make_unique<DrumSamplerDevice>();
    d->prepare (48000.0, 512);
    // a sample silent in its first half, loud in its second
    auto s = std::make_shared<SampleData>();
    s->buffer.setSize (1, 48000);
    for (int i = 0; i < 48000; ++i) s->buffer.setSample (0, i, i < 24000 ? 0.0f : 0.5f);
    s->sampleRate = 48000.0;
    d->pad[0].set (s);
    auto level = [&] (bool lock)
    {
        d->reset();
        d->clearLocks();
        if (lock) d->setLock (d->padParam (0, DrumSamplerDevice::Start), 0.5f);
        NoteEvent on; on.type = NoteEvent::NoteOn; on.note = d->laneNote (0); on.velocity = 1.0f;
        d->handleEvent (on);
        std::vector<float> l (512), r (512);
        d->render (l.data(), r.data(), 512);
        float m = 0.0f;
        for (int i = 100; i < 512; ++i) m = std::max (m, std::abs (l[(size_t) i]));
        return m;
    };
    const float plain = level (false), locked = level (true);
    ctx.check (plain < 1.0e-4f && locked > 0.05f, "Start locked to 50 %: the pad starts in the loud half (" + juce::String (plain, 5) + " -> " + juce::String (locked, 4) + ")");
}
