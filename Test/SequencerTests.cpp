#include "Tests.h"
#include <map>
#include <iostream>
#include "engine/Engine.h"
#include "model/Serializer.h"
#include "app/presets/PresetBank.h"

using namespace mb;

namespace
{
/** A device that records note-on sample positions instead of making sound. */
struct ProbeDevice : DeviceProcessor
{
    explicit ProbeDevice (int lanes = 0) : DeviceProcessor (DeviceType::Mono), numLanes (lanes) { layout.cont ("x", "X", 0, 1, 0); finishLayout(); }
    void prepare (double sr, int) override { sampleRate = sr; }
    void reset() override {}
    void handleEvent (const NoteEvent& e) override
    {
        if (e.type == NoteEvent::NoteOn) ons.push_back ({ clock + e.sampleOffset, e.note });
        if (e.type == NoteEvent::NoteOn) ++active; else if (e.type == NoteEvent::NoteOff && active > 0) --active;
        if (e.type == NoteEvent::AllNotesOff || e.type == NoteEvent::Panic) active = 0;
        if (e.type == NoteEvent::NoteOff) { ++offs; offTimes.push_back ({ clock + e.sampleOffset, e.note }); }
        if (e.type == NoteEvent::NoteOn) onFlags.push_back (e.flags);
        if (e.type == NoteEvent::NoteOn || e.type == NoteEvent::NoteOff) order.push_back ({ clock + e.sampleOffset, e.type == NoteEvent::NoteOn ? e.note : -e.note });
    }
    void render (float* L, float*, int n) override { clock += n; L[0] += 0.0f; }
    bool anyVoiceActive() const override { return true; }
    bool isSilent() const override { return false; }
    int laneCount() const override { return numLanes; }
    int numLanes;
    int64_t clock = 0;
    int active = 0, offs = 0;
    std::vector<std::pair<int64_t, int>> ons, offTimes;
    std::vector<int> onFlags;
    std::vector<std::pair<int64_t, int>> order;   // every note on (+note) and off (-note) as it arrived
};

std::vector<int64_t> runAndCollect (Engine& e, ProbeDevice* probe, double seconds, int block = 128)
{
    std::vector<float> l ((size_t) block), r ((size_t) block);
    const int total = (int) (seconds * e.getSampleRate());
    for (int pos = 0; pos < total; pos += block) e.process (l.data(), r.data(), block, nullptr);
    std::vector<int64_t> t;
    for (auto& o : probe->ons) t.push_back (o.first);
    return t;
}
} // namespace

MB_TEST (seq_note_positions_tempo_swing)
{
    for (double bpm : { 90.0, 120.0, 174.0 })
        for (float swing : { 0.5f, 0.66f })
        {
            Engine e;
            e.prepare (48000.0, 128);
            auto dev = std::make_unique<ProbeDevice>();
            auto* probe = dev.get();
            e.setDevice (0, 0, std::move (dev));
            e.project().bpm = bpm;
            e.project().swing = swing;
            auto& tr = e.project().patterns[0].slots[0].track;
            for (int s = 0; s < 16; ++s) { tr.steps[(size_t) s].noteCount = 1; tr.steps[(size_t) s].notes[0] = 60; }
            e.play();
            // events land at the start of the sub-block in which they occur: offsets are sample exact
            const auto ons = runAndCollect (e, probe, 60.0 / bpm * 4.0 * 0.99);
            const double stepLen = 48000.0 * 60.0 / bpm / 4.0;
            bool ok = ons.size() == 16;
            double worst = 0.0;
            for (size_t i = 0; i < ons.size() && i < 16; ++i)
            {
                const double expect = i * stepLen + ((i & 1) ? (swing - 0.5) * 2.0 * stepLen : 0.0);
                worst = std::max (worst, std::abs ((double) ons[i] - expect));
            }
            ctx.check (ok && worst <= 1.0, "bpm " + juce::String (bpm) + " swing " + juce::String (swing) + " notes "
                       + juce::String ((int) ons.size()) + " worst error " + juce::String (worst) + " samples");
        }
}

MB_TEST (seq_time_signatures)
{
    struct Sig { int b, u, steps; };
    for (auto s : { Sig { 4, 4, 16 }, Sig { 3, 4, 12 }, Sig { 7, 8, 14 }, Sig { 5, 16, 5 } })
    {
        Project p; p.beats = s.b; p.unit = s.u;
        ctx.check (p.stepsPerBar (0) == s.steps, juce::String (s.b) + "/" + juce::String (s.u) + " gives " + juce::String (p.stepsPerBar (0)));
    }
}

MB_TEST (seq_pattern_switch_on_bar)
{
    Engine e;
    e.prepare (48000.0, 128);
    auto a = std::make_unique<ProbeDevice>(); auto* pa = a.get();
    auto b = std::make_unique<ProbeDevice>(); auto* pb = b.get();
    e.setDevice (0, 0, std::move (a));
    e.setDevice (1, 0, std::move (b));
    e.project().patterns[0].lengthBars = 2;
    for (int pat = 0; pat < 2; ++pat)
    {
        auto& tr = e.project().patterns[(size_t) pat].slots[0].track;
        for (int s = 0; s < 32; s += 4) { tr.steps[(size_t) s].noteCount = 1; tr.steps[(size_t) s].notes[0] = 60 + pat; }
    }
    e.play();
    std::vector<float> l (128), r (128);
    int64_t clock = 0;
    auto block = [&] { pa->clock = pb->clock = clock; e.process (l.data(), r.data(), 128, nullptr); clock += 128; };
    for (int i = 0; i < 20; ++i) block();
    e.queuePattern (1);
    const double barLen = 48000.0 * 2.0;   // 4/4 at 120 BPM
    while (clock < (int64_t) (barLen * 1.5)) block();
    ctx.check (! pb->ons.empty(), "pattern B played after the switch");
    if (! pb->ons.empty())
        ctx.check (std::abs ((double) pb->ons.front().first - barLen) <= 1.0, "switch lands on the bar line: "
                   + juce::String ((int) pb->ons.front().first) + " vs " + juce::String ((int) barLen));
    ctx.check (pa->active == 0, "pattern A notes released after the switch");
}

MB_TEST (seq_drum_lanes_ratchet_probability)
{
    Engine e;
    e.prepare (48000.0, 128);
    auto d = std::make_unique<ProbeDevice> (8); auto* pd = d.get();
    e.setDevice (0, 0, std::move (d));
    auto& tr = e.project().patterns[0].slots[0].track;
    tr.lanes[0][0].on = true; tr.lanes[0][0].ratchet = 4;
    tr.lanes[1][4].on = true; tr.lanes[1][4].probability = 0;
    e.play();
    runAndCollect (e, pd, 0.45);
    ctx.check (pd->ons.size() == 4, "ratchet 4 gives 4 hits and probability 0 none: " + juce::String ((int) pd->ons.size()));
}

MB_TEST (seq_parameter_locks)
{
    Engine e;
    e.prepare (48000.0, 128);
    auto d = std::make_unique<ProbeDevice>(); auto* pd = d.get();
    e.setDevice (0, 0, std::move (d));
    auto& tr = e.project().patterns[0].slots[0].track;
    auto* lane = tr.lockFor (0, true);
    lane->values[2] = 0.75f;
    e.play();
    std::vector<float> l (128), r (128);
    const double stepLen = 48000.0 * 0.125;
    bool sawLock = false, cleared = true;
    for (int pos = 0; pos < (int) (stepLen * 4); pos += 128)
    {
        e.process (l.data(), r.data(), 128, nullptr);
        const int step = e.currentStep();
        if (step == 2 && std::abs (pd->pv (0) - 0.75f) < 1.0e-6f) sawLock = true;
        if (step == 3 && std::abs (pd->pv (0) - 0.75f) < 1.0e-6f) cleared = false;
    }
    ctx.check (sawLock, "lock applies on its step");
    ctx.check (cleared, "lock clears on the next step");
}

namespace
{
struct Rig
{
    Engine e;
    ProbeDevice* probe = nullptr;
    std::vector<float> l = std::vector<float> (128), r = std::vector<float> (128);
    double pos = 0;
    static constexpr double stepLen = 48000.0 * 60.0 / 120.0 / 4.0;
    explicit Rig (int track = 1, int lanes = 0)
    {
        e.prepare (48000.0, 128);
        auto dev = std::make_unique<ProbeDevice> (lanes);
        probe = dev.get();
        e.setDevice (0, track, std::move (dev));
        e.focusTrack = track;
        e.project().countIn = false;   // the timings below start recording at once
    }
    void runTo (double steps) { while (pos < steps * stepLen) { e.process (l.data(), r.data(), 128, nullptr); pos += 128; e.applyRecorded(); } }
    void midi (int ch, int note, bool on)
    {
        juce::MidiBuffer m;
        m.addEvent (on ? juce::MidiMessage::noteOn (ch, note, (uint8_t) 100) : juce::MidiMessage::noteOff (ch, note), 0);
        e.process (l.data(), r.data(), 128, &m); pos += 128;
        e.applyRecorded();
    }
    /** The clip note of a pitch (live recording writes piano-roll clips), or null. */
    const ClipNote* clipNote (int note, int t = 1) { for (auto& n : track (t).clip) if (n.note == note) return &n; return nullptr; }
    int stepT() { return e.project().stepTicks (0); }
    std::vector<int> heard() const { std::vector<int> v; for (auto& o : probe->ons) v.push_back (o.second); return v; }
    Track& track (int t = 1) { return e.project().patterns[0].slots[(size_t) t].track; }
};
} // namespace

/** A keyboard on any MIDI channel plays and records the selected track (channel-per-track is an option). */
MB_TEST (input_midi_follows_selected_track)
{
    Rig rig (2);
    rig.midi (1, 60, true); rig.midi (1, 60, false);
    rig.midi (5, 62, true); rig.midi (5, 62, false);
    ctx.check (rig.heard() == std::vector<int> ({ 60, 62 }), "channels 1 and 5 both play the selected track 3");
    rig.e.project().channelPerTrack = true;
    rig.midi (1, 64, true); rig.midi (1, 64, false);
    ctx.check (rig.heard().size() == 2, "with channel-per-track on, channel 1 goes to track 1 instead");
    rig.midi (3, 65, true); rig.midi (3, 65, false);
    ctx.check (rig.heard().size() == 3 && rig.heard().back() == 65, "and channel 3 to track 3");
}

/** Live recording from a MIDI keyboard: notes land on the nearest steps with the held length as gate. */
MB_TEST (record_live_from_midi)
{
    Rig rig (1);
    rig.e.project().overdub = true;   // the pattern loops (Replace grows it: record_live_grows_pattern)
    rig.e.setRecord (true);
    rig.e.play();
    rig.runTo (2); rig.midi (1, 60, true); rig.runTo (3); rig.midi (1, 60, false);
    rig.runTo (5); rig.e.injectNote (1, 64, 0.8f, true); rig.runTo (7); rig.e.injectNote (1, 64, 0.0f, false);
    rig.runTo (16.5);
    auto& tr = rig.track();
    const auto* a = rig.clipNote (60);
    const auto* b = rig.clipNote (64);
    const int st = rig.stepT();
    ctx.check (tr.pianoRoll && a != nullptr && a->start == 2 * st, "live recording writes the piano-roll clip: the note on step 3");
    ctx.check (a != nullptr && std::abs (a->length - st) < st / 10, "its length is the held length (" + juce::String (a ? a->length / (double) st : 0.0) + " steps)");
    ctx.check (b != nullptr && b->length > 1.8 * st, "a two-step note (" + juce::String (b ? b->length / (double) st : 0.0) + " steps)");
    rig.probe->ons.clear();
    rig.runTo (32.5);
    ctx.check (rig.heard() == std::vector<int> ({ 60, 64 }), "and plays back on the next pass");
}

/** A key held across the loop point while overdubbing: its own recorded note (not yet released) must not play on
    the next pass and cut it; released, it plays back. Stopping the take with a key held gives the note its length. */
MB_TEST (record_held_note_survives_the_loop)
{
    Rig rig (1);
    rig.e.project().overdub = true;
    rig.e.setRecord (true);
    rig.e.play();
    rig.runTo (2); rig.midi (1, 60, true);
    rig.probe->offTimes.clear();
    rig.runTo (16 + 6);   // held past the loop point (16 steps) and past where it started
    bool cut = false;
    for (auto& o : rig.probe->offTimes) cut = cut || o.second == 60;
    ctx.check (! cut, "the held note is not cut when the loop comes round");
    rig.midi (1, 60, false);
    rig.runTo (24);
    const auto* a = rig.clipNote (60);
    ctx.check (a != nullptr && ! a->muted && a->length > 12 * rig.stepT(), "released: a long note that plays (" + juce::String (a ? a->length / (double) rig.stepT() : 0.0) + " steps)");

    rig.midi (1, 67, true);
    rig.runTo (28);
    rig.e.setRecord (false);
    rig.runTo (29);
    rig.midi (1, 67, false);
    rig.runTo (30);
    const auto* b = rig.clipNote (67);
    ctx.check (b != nullptr && ! b->muted && b->length >= 3 * rig.stepT() - rig.stepT() / 4, "a key held when the take stops still gets its length");
}

/** A chord held for two bars, recorded into a one-bar pattern (Extend, the default): the pattern grows to two
    bars and the chord plays back whole on every later pass. */
MB_TEST (record_two_bar_chord_plays_back)
{
    Rig rig (1);
    rig.e.setRecord (true);
    rig.e.play();
    rig.runTo (0.1);
    for (int n : { 60, 64, 67 }) rig.midi (1, n, true);
    rig.runTo (32.0);
    for (int n : { 60, 64, 67 }) rig.midi (1, n, false);
    rig.runTo (34);
    rig.e.setRecord (false);
    rig.runTo (40);
    juce::String clip;
    for (auto& n : rig.track().clip) clip << (int) n.note << "@" << n.start / rig.stepT() << "+" << n.length / (double) rig.stepT() << (n.muted ? "M" : "") << " ";
    ctx.check (rig.e.project().patterns[0].lengthBars == 2, "the pattern grew to two bars (" + juce::String (rig.e.project().patterns[0].lengthBars) + ")");
    for (int n : { 60, 64, 67 })
    {
        const auto* c = rig.clipNote (n);
        ctx.check (c != nullptr && ! c->muted && c->length > 30 * rig.stepT(), "note " + juce::String (n) + " is two bars long and plays: " + clip);
    }
    rig.probe->ons.clear(); rig.probe->offTimes.clear();
    const double from = rig.pos;
    rig.runTo (64 + 33);   // the next full passes
    int ons60 = 0;
    for (auto& o : rig.probe->ons) if (o.second == 60) ++ons60;
    juce::String offs;
    for (auto& o : rig.probe->offTimes) if (o.second == 60) offs << juce::String ((o.first - from) / Rig::stepLen, 1) << " ";
    juce::String onsS;
    for (auto& o : rig.probe->ons) if (o.second == 60) onsS << juce::String ((o.first - from) / Rig::stepLen, 1) << " ";
    ctx.check (ons60 >= 1, "plays on later passes; on at " + onsS + " off at " + offs);
}

/** A drawn chord that fills the whole pattern (two bars): every pass plays it whole (the previous pass's note-off
    must not land after the next pass's note-on and cut it). */
MB_TEST (clip_chord_filling_pattern_loops_whole)
{
    Rig rig (1);
    auto& p = rig.e.project();
    p.patterns[0].lengthBars = 2;
    auto& tr = rig.track();
    tr.pianoRoll = true;
    const int total = p.patternTicks (0);
    for (int n : { 60, 64, 67 }) { ClipNote c; c.start = 0; c.length = total; c.note = (uint8_t) n; tr.addClipNote (c); }
    rig.e.play();
    rig.runTo (32 * 3 + 4);
    juce::String log;
    std::vector<std::pair<int64_t, bool>> ev;
    for (auto& o : rig.probe->order) if (std::abs (o.second) == 60) ev.push_back ({ o.first, o.second > 0 });
    for (auto& e : ev) log << (e.second ? "on " : "off ") << juce::String (e.first) << ", ";
    // the order of events: on, (off, on) at each loop point; never an off after an on at the same time
    bool ok = ev.size() >= 5 && ev[0].second;
    for (size_t i = 1; i + 1 < ev.size(); i += 2)
        ok = ok && ! ev[i].second && ev[i + 1].second && ev[i].first <= ev[i + 1].first;
    ctx.check (ok, "each pass: off then on at the loop point: " + log);
}

/** The same with the real instruments: the chord sounds through the middle of every pass, not only the first. */
MB_TEST (clip_chord_filling_pattern_sounds_every_pass)
{
    for (auto type : { DeviceType::Poly, DeviceType::Mono, DeviceType::FM, DeviceType::Acid })
    {
        Engine e;
        e.prepare (48000.0, 128);
        auto dev = createDevice (type);
        presets::loadDefault (*dev, true);
        e.setDevice (0, 1, std::move (dev));
        auto& p = e.project();
        p.patterns[0].lengthBars = 2;
        auto& tr = p.patterns[0].slots[1].track;
        tr.pianoRoll = true;
        for (int n : { 60, 64, 67 }) { ClipNote c; c.start = 0; c.length = p.patternTicks (0); c.note = (uint8_t) n; tr.addClipNote (c); }
        e.play();
        std::vector<float> l (128), r (128);
        const double stepLen = 48000.0 * 60.0 / 120.0 / 4.0;
        juce::String levels;
        bool ok = true;
        double pos = 0;
        for (int pass = 0; pass < 3; ++pass)
            for (int q = 0; q < 4; ++q)
            {
                // a quarter of the pass: its level
                const double end = (pass * 32 + (q + 1) * 8) * stepLen;
                double sum = 0; int cnt = 0;
                while (pos < end) { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); e.process (l.data(), r.data(), 128, nullptr); for (float v : l) sum += v * v; cnt += 128; pos += 128; }
                const float rms = (float) std::sqrt (sum / cnt);
                levels << juce::String (rms, 3) << " ";
                if (q == 2 && rms < 0.003f) ok = false;   // three quarters into a pass: still sounding
            }
        ctx.check (ok, juce::String (deviceTypeName (type)) + ": sounds through every pass: " + levels);
    }
}

/** As the phone records: a chord pad (Chords mode, one key) held for two bars through the UI note path, Extend. */
MB_TEST (record_chord_pad_held_two_bars)
{
    Rig rig (1);
    rig.e.project().patterns[0].slots[1].play.chordOn = true;
    rig.e.setRecord (true);
    rig.e.play();
    rig.runTo (0.1);
    rig.e.injectNote (1, 60, 0.8f, true);
    rig.runTo (32.0);
    rig.e.injectNote (1, 60, 0.0f, false);
    rig.runTo (34);
    rig.e.setRecord (false);
    rig.runTo (40);
    juce::String clip;
    for (auto& n : rig.track().clip) clip << (int) n.note << "@" << n.start / rig.stepT() << "+" << juce::String (n.length / (double) rig.stepT(), 1) << (n.muted ? "M" : "") << " ";
    bool ok = rig.track().clip.size() >= 3;
    for (auto& n : rig.track().clip) ok = ok && ! n.muted && n.length > 30 * rig.stepT();
    ctx.check (ok, "three two-bar notes, unmuted: bars " + juce::String (rig.e.project().patterns[0].lengthBars) + ": " + clip);
    rig.probe->order.clear();
    const double from = rig.pos;
    rig.runTo (40 + 64);
    juce::String log;
    int ons = 0;
    for (size_t i = 0; i < rig.probe->order.size(); ++i)
    {
        const auto& o = rig.probe->order[i];
        log << (o.second > 0 ? "+" : "") << o.second << "@" << juce::String ((o.first - from) / Rig::stepLen, 1) << " ";
        if (o.second > 0) ++ons;
    }
    // each later pass: the chord starts, and its off comes a whole pattern later (never right after the on)
    bool whole = ons >= 3;
    for (size_t i = 0; i < rig.probe->order.size(); ++i)
        if (rig.probe->order[i].second > 0)
            for (size_t j = i + 1; j < rig.probe->order.size(); ++j)
                if (rig.probe->order[j].second == -rig.probe->order[i].second) { whole = whole && rig.probe->order[j].first - rig.probe->order[i].first > 30 * Rig::stepLen; break; }
    ctx.check (whole, "the chord plays whole on later passes: " + log);
}

/** Notes from the UI note path (the phone's pads and roll previews), on each starter instrument, stopped and
    playing: a tap whose on and off arrive in the same audio block, and a note released 250 ms later, both end. */
MB_TEST (ui_note_preview_stops)
{
    for (bool playing : { false, true })
    for (auto type : { DeviceType::Mono, DeviceType::Acid, DeviceType::Poly, DeviceType::FM })
    {
        Engine e;
        e.prepare (48000.0, 192);
        auto dev = createDevice (type);
        presets::loadDefault (*dev, true);
        e.setDevice (0, 1, std::move (dev));
        e.focusTrack = 1;
        if (playing) e.play();
        std::vector<float> l (192), r (192);
        auto run = [&] (double secs)
        {
            double peak = 0;
            for (int i = 0; i < (int) (secs * 48000 / 192); ++i)
            {
                std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
                e.process (l.data(), r.data(), 192, nullptr);
                for (float v : l) peak = std::max (peak, (double) std::abs (v));
            }
            return peak;
        };
        run (0.2);
        {
            // a tap shorter than the audio block: on and off arrive together; the note must still end
            e.injectNote (1, 50, 0.8f, true); e.injectNote (1, 50, 0.0f, false);
            run (2.5);
            const double ring = run (0.5);
            ctx.check (ring < 0.002, juce::String (deviceTypeName (type)) + (playing ? " playing" : " stopped")
                       + ": a tap within one block ends (2.5 s later " + juce::String (ring, 4) + ")");
        }
        e.injectNote (1, 48, 0.8f, true);
        const double during = run (0.25);
        e.injectNote (1, 48, 0.0f, false);
        run (2.5);
        const double after = run (0.5);
        ctx.check (during > 0.01 && after < 0.002, juce::String (deviceTypeName (type)) + (playing ? " playing" : " stopped")
                   + ": sounds, then stops (during " + juce::String (during, 3) + ", 2.5 s after the off " + juce::String (after, 4) + ")");
    }
}

namespace
{
/** The level of one pitch in a stretch of audio (a Goertzel filter at its frequency). */
double pitchLevel (const std::vector<float>& x, int note, double sr)
{
    // the strongest of 13 frequencies across ±3 % (a VCO's pitch drifts a little), over 8192 samples
    const size_t n = std::min<size_t> (x.size(), 8192);
    double best = 0;
    for (int k = -6; k <= 6; ++k)
    {
        const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0) * (1.0 + k * 0.005);
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = 2.0 * std::cos (w);
        double s1 = 0, s2 = 0;
        for (size_t i = 0; i < n; ++i) { const double s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
        best = std::max (best, std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (double) n);
    }
    return best;
}
} // namespace

/** The phone's report (screen recording): a VA chord filling a two-bar pattern; after the first loop two of its
    three notes went silent. Every note of the chord must sound in every pass, in Full and in Eco quality. */
MB_TEST (va_chord_keeps_every_note_on_loop)
{
    for (int variant = 0; variant < 8; ++variant)
    {
        const auto q = (variant & 1) ? Quality::Eco : Quality::Full;
        const bool vco = (variant & 2) != 0, x1 = (variant & 4) != 0;
        qualitySetting() = (int) q;
        oversampleVoices() = ! x1;
        Engine e;
        e.prepare (48000.0, 192);
        auto dev = createDevice (DeviceType::Poly);
        presets::loadDefault (*dev, true);
        dev->set ("engine", vco ? 1.0f : 0.0f);
        e.setDevice (0, 1, std::move (dev));
        auto& p = e.project();
        p.patterns[0].lengthBars = 2;
        auto& tr = p.patterns[0].slots[1].track;
        tr.pianoRoll = true;
        const int chord[3] = { 72, 75, 79 };   // C5 Eb5 G5, as in the recording
        for (int n : chord) { ClipNote c; c.start = 0; c.length = p.patternTicks (0); c.note = (uint8_t) n; tr.addClipNote (c); }
        e.play();
        std::vector<float> l (192), r (192), buf;
        const double passLen = 48000.0 * 4.0;   // two bars at 120 BPM
        juce::String log;
        bool ok = true;
        for (int pass = 0; pass < 3; ++pass)
        {
            // the middle of the pass
            buf.clear();
            for (double t = 0; t < passLen; t += 192)
            {
                std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
                e.process (l.data(), r.data(), 192, nullptr);
                if (t > passLen * 0.4 && t < passLen * 0.6) buf.insert (buf.end(), l.begin(), l.end());
            }
            log << "pass " << pass + 1 << ":";
            for (int n : chord) { const double lv = pitchLevel (buf, n, 48000.0); log << " " << n << "=" << juce::String (lv, 4); ok = ok && lv > 0.002; }
            log << "; ";
        }
        ctx.check (ok, juce::String (q == Quality::Eco ? "Eco" : "Full") + (vco ? " VCO" : " DCO") + (x1 ? " 1x" : " 2x")
                   + ": every note sounds in every pass: " + log);
    }
    qualitySetting() = (int) Quality::Full;
    oversampleVoices() = true;
}

/** Tool (MB_PROJECT=<file>): plays a saved project's pattern A for three passes and prints, per pass, the level of
    each note its clips hold (how the phone's report was checked against the real project). */
MB_TEST (tool_project_note_levels)
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("MB_PROJECT", {});
    if (path.isEmpty()) return;
    Engine e;
    e.prepare (48000.0, 192);
    const auto r = serializer::load (e, juce::File (path));
    ctx.check (r.ok, "loads: " + r.error);
    std::vector<int> notes;
    for (auto& s : e.project().patterns[0].slots) for (auto& n : s.track.clip) notes.push_back (n.note);
    e.play();
    std::vector<float> l (192), r2 (192), buf;
    const double passLen = e.project().patternTicks (0) / (double) kPPQ * 48000.0 * 60.0 / e.project().bpm;
    for (int pass = 0; pass < 3; ++pass)
    {
        buf.clear();
        for (double t = 0; t < passLen; t += 192)
        {
            std::fill (l.begin(), l.end(), 0.0f); std::fill (r2.begin(), r2.end(), 0.0f);
            e.process (l.data(), r2.data(), 192, nullptr);
            if (t > passLen * 0.4 && t < passLen * 0.6) buf.insert (buf.end(), l.begin(), l.end());
        }
        std::cout << "    pass " << pass + 1 << ":";
        for (int n : notes) std::cout << " " << n << "=" << pitchLevel (buf, n, 48000.0);
        std::cout << std::endl;
    }
}

/** The phone's drawn chord: each note was previewed as it was drawn (on and off in the same instant, as the roll
    did), then the pattern played. Every note must sound in every pass. */
MB_TEST (drawn_chord_after_previews_sounds_every_pass)
{
    for (auto type : { DeviceType::Poly, DeviceType::FM })
    {
        Engine e;
        e.prepare (48000.0, 192);
        auto dev = createDevice (type);
        presets::loadDefault (*dev, true);
        e.setDevice (0, 1, std::move (dev));
        e.focusTrack = 1;
        auto& p = e.project();
        p.patterns[0].lengthBars = 2;
        auto& tr = p.patterns[0].slots[1].track;
        tr.pianoRoll = true;
        const int chord[3] = { 72, 75, 79 };
        std::vector<float> l (192), r (192), buf;
        auto block = [&] { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); e.process (l.data(), r.data(), 192, nullptr); };
        for (int n : chord)
        {
            e.injectNote (1, n, 0.8f, true); e.injectNote (1, n, 0.0f, false);   // the roll's preview
            for (int i = 0; i < 100; ++i) block();
            ClipNote c; c.start = 0; c.length = p.patternTicks (0); c.note = (uint8_t) n; tr.addClipNote (c);
        }
        e.play();
        const double passLen = 48000.0 * 4.0;
        juce::String log;
        bool ok = true;
        for (int pass = 0; pass < 3; ++pass)
        {
            buf.clear();
            for (double t = 0; t < passLen; t += 192) { block(); if (t > passLen * 0.4 && t < passLen * 0.6) buf.insert (buf.end(), l.begin(), l.end()); }
            log << "pass " << pass + 1 << ":";
            for (int n : chord) { const double lv = pitchLevel (buf, n, 48000.0); log << " " << n << "=" << juce::String (lv, 4); ok = ok && lv > 0.002; }
            log << "; ";
        }
        ctx.check (ok, juce::String (deviceTypeName (type)) + ": " + log);
    }
}

/** The phone drives the engine prepared for 1024-sample blocks with the device's own callback sizes (often small
    and uneven): a chord filling the pattern must still sound in every pass. */
MB_TEST (chord_loops_with_phone_block_sizes)
{
    for (double sr : { 48000.0, 44100.0 })
    for (int sizes : { 0, 1, 2 })
    {
        Engine e;
        e.prepare (sr, 1024);
        auto dev = createDevice (DeviceType::Poly);
        presets::loadDefault (*dev, true);
        e.setDevice (0, 1, std::move (dev));
        auto& p = e.project();
        p.patterns[0].lengthBars = 2;
        auto& tr = p.patterns[0].slots[1].track;
        tr.pianoRoll = true;
        const int chord[3] = { 72, 75, 79 };
        for (int n : chord) { ClipNote c; c.start = 0; c.length = p.patternTicks (0); c.note = (uint8_t) n; tr.addClipNote (c); }
        e.play();
        std::vector<float> l (1024), r (1024), buf;
        const int pattern[3][4] = { { 96, 96, 96, 96 }, { 240, 240, 240, 240 }, { 96, 192, 37, 480 } };
        const double passLen = sr * 4.0;
        juce::String log;
        bool ok = true;
        double t = 0;
        int k = 0;
        for (int pass = 0; pass < 3; ++pass)
        {
            buf.clear();
            const double end = (pass + 1) * passLen;
            while (t < end)
            {
                const int n = pattern[sizes][k++ % 4];
                std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
                e.process (l.data(), r.data(), n, nullptr);
                const double rel = t - pass * passLen;
                if (rel > passLen * 0.4 && rel < passLen * 0.6) buf.insert (buf.end(), l.begin(), l.begin() + n);
                t += n;
            }
            log << "pass " << pass + 1 << ":";
            for (int n : chord) { const double lv = pitchLevel (buf, n, sr); log << " " << n << "=" << juce::String (lv, 4); ok = ok && lv > 0.002; }
            log << "; ";
        }
        ctx.check (ok, juce::String (sr) + " Hz, blocks " + juce::String (sizes) + ": " + log);
    }
}

/** Tool (MB_CLICKS=1): VA Silk Pad playing C3 Eb3 G3 for two bars, looped, at the phone's settings (1x voices,
    960-sample blocks) and the desktop's; writes the output (raw float32, MB_CLICKS_OUT) and lists clicks: samples
    whose second difference stands far out of its surroundings. */
MB_TEST (tool_va_pad_clicks)
{
    if (juce::SystemStats::getEnvironmentVariable ("MB_CLICKS", {}).isEmpty()) return;
    for (bool phone : { true, false })
    {
        oversampleVoices() = ! phone;
        Engine e;
        e.prepare (48000.0, phone ? 1024 : 512);
        auto dev = createDevice (DeviceType::Poly);
        for (auto& pi : presets::list (DeviceType::Poly)) if (pi.name == "Silk Pad") presets::apply (*dev, pi);
        juce::String pname = dev->presetName;
        // MB_SET=id=value,id=value: parameter overrides (to find which part of the sound makes the clicks)
        for (auto& kv : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("MB_SET", {}), ",", ""))
            if (kv.contains ("=")) dev->set (kv.upToFirstOccurrenceOf ("=", false, false), kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
        e.setDevice (0, 1, std::move (dev));
        auto& p = e.project();
        p.patterns[0].lengthBars = 2;
        auto& tr = p.patterns[0].slots[1].track;
        tr.pianoRoll = true;
        for (int n : { 48, 51, 55 }) { ClipNote c; c.start = 0; c.length = p.patternTicks (0); c.note = (uint8_t) n; tr.addClipNote (c); }
        e.play();
        const int block = phone ? 960 : 256;
        std::vector<float> l ((size_t) block), r ((size_t) block), out;
        for (int b = 0; b < 48000 * 12 / block; ++b) { e.process (l.data(), r.data(), block, nullptr); out.insert (out.end(), l.begin(), l.end()); }
        if (auto o = juce::SystemStats::getEnvironmentVariable ("MB_CLICKS_OUT", {}); o.isNotEmpty())
            juce::File (o + (phone ? "_phone.raw" : "_desktop.raw")).replaceWithData (out.data(), out.size() * sizeof (float));
        // second difference against a running average of its size
        std::vector<double> d2 (out.size(), 0.0);
        for (size_t i = 2; i < out.size(); ++i) d2[i] = std::abs ((double) out[i] - 2.0 * out[i - 1] + out[i - 2]);
        double avg = 1e-6; int clicks = 0; juce::String where;
        for (size_t i = 2; i < out.size(); ++i)
        {
            if (i > 4800 && d2[i] > 12.0 * avg && d2[i] > 1e-3)
            {
                if (clicks < 12) where << juce::String (i / 48000.0, 4) << "s(" << juce::String (d2[i] / avg, 0) << "x) ";
                ++clicks;
            }
            avg += (d2[i] - avg) * 0.002;
        }
        double peak = 0; for (float v : out) peak = std::max (peak, (double) std::abs (v));
        std::cout << "    " << (phone ? "phone  " : "desktop") << " " << pname << ": peak " << peak << ", clicks " << clicks << ": " << where << std::endl;
    }
    oversampleVoices() = true;
}

/** Step recording: each key (or chord) is written at the cursor, which moves on by the step length when the
    keys are released; Rest leaves a gap, Back clears, To start returns the cursor. */
MB_TEST (record_step_mode)
{
    Rig rig (1);
    auto& p = rig.e.project();
    p.recordMode = 1; p.stepLength = 1;   // 2 steps per entry
    rig.e.setRecord (true);
    rig.midi (1, 60, true); rig.midi (1, 64, true); rig.midi (1, 67, true);   // a chord
    rig.midi (1, 60, false); rig.midi (1, 64, false); rig.midi (1, 67, false);
    rig.midi (1, 62, true); rig.midi (1, 62, false);
    rig.e.stepRest(); rig.runTo (0.5);
    rig.midi (1, 65, true); rig.midi (1, 65, false);
    auto& tr = rig.track();
    ctx.check (tr.steps[0].noteCount == 3, "the chord is one entry on step 1");
    ctx.check (tr.steps[0].len[0] > 200 && tr.steps[0].len[0] <= 256, "spanning 2 steps (gate " + juce::String (tr.steps[0].len[0]) + ")");
    ctx.check (tr.steps[2].noteCount == 1 && tr.steps[2].notes[0] == 62, "the next key lands 2 steps on");
    ctx.check (tr.steps[4].noteCount == 0 && tr.steps[6].noteCount == 1, "Rest left a gap");
    ctx.check (rig.e.stepRecordCursor.load() == 8, "cursor at step 9");
    rig.e.stepBack(); rig.runTo (1.0);
    ctx.check (tr.steps[6].noteCount == 0 && rig.e.stepRecordCursor.load() == 6, "Back clears the last entry");
    rig.e.toStart(); rig.runTo (1.5);
    ctx.check (rig.e.stepRecordCursor.load() == 0, "To start returns the cursor");
    rig.midi (1, 72, true); rig.midi (1, 72, false);
    ctx.check (tr.steps[0].noteCount == 1 && tr.steps[0].notes[0] == 72, "and the next key overwrites step 1");
}

/** To start while playing jumps the playhead back to step 1. */
MB_TEST (transport_to_start)
{
    Rig rig (1);
    rig.track().steps[0].noteCount = 1; rig.track().steps[0].notes[0] = 48;
    rig.e.play();
    rig.runTo (6);
    rig.probe->ons.clear();
    rig.e.toStart();
    rig.runTo (6.2);
    ctx.check (rig.heard() == std::vector<int> ({ 48 }), "step 1 plays again right after To start");
}

/** Scale lock snaps keys into the scale; white keys play the scale's degrees; chord mode plays diatonic
    triads; the arp steps through held notes at its rate. */
MB_TEST (input_scale_chord_arp)
{
    Rig rig (1);
    auto& p = rig.e.project();
    p.scale.lock = true; p.scale.root = 0; p.scale.scale = 1;   // C major
    rig.midi (1, 61, true); rig.midi (1, 61, false);
    ctx.check (rig.heard().back() == 60, "C#4 snaps to C4 (nearest, ties down)");
    p.scale.whiteKeys = true; p.scale.scale = 6;   // natural minor
    rig.midi (1, 64, true); rig.midi (1, 64, false);
    ctx.check (rig.heard().back() == 63, "white key E plays the minor third (Eb)");
    p.scale.whiteKeys = false; p.scale.scale = 1;
    auto& play = p.patterns[0].slots[1].play;
    play.chordOn = true;
    rig.probe->ons.clear();
    rig.midi (1, 62, true); rig.midi (1, 62, false);
    auto h = rig.heard(); std::sort (h.begin(), h.end());
    ctx.check (h == std::vector<int> ({ 62, 65, 69 }), "D in C major plays D minor (D F A)");
    play.chordType[1] = 1;   // degree ii forced to major
    rig.probe->ons.clear();
    rig.midi (1, 62, true); rig.midi (1, 62, false);
    h = rig.heard(); std::sort (h.begin(), h.end());
    ctx.check (h == std::vector<int> ({ 62, 66, 69 }), "a per-degree chord overrides it (D major)");
    play.chordOn = false; play.arpOn = true; play.arpRate = 6;   // 1/16
    rig.probe->ons.clear();
    rig.midi (1, 60, true); rig.midi (1, 64, true);
    rig.runTo (rig.pos / Rig::stepLen + 4.0);
    rig.midi (1, 60, false); rig.midi (1, 64, false);
    h = rig.heard();
    ctx.check (h.size() >= 4 && h.size() <= 6 && h[0] == 60 && h[1] == 64 && h[2] == 60, "the arp alternates C E at 1/16 (" + juce::String ((int) h.size()) + " notes)");
    rig.runTo (rig.pos / Rig::stepLen + 2.0);
    ctx.check (rig.probe->active == 0, "and stops cleanly when the keys are released");
}

/** Growing a pattern repeats its bars into the new ones. */
MB_TEST (pattern_grow_copies_bars)
{
    Project p;
    auto& tr = p.patterns[0].slots[0].track;
    tr.steps[3].addNote (60); tr.lanes[2][5].on = true;
    p.setLengthBars (0, 2);
    ctx.check (tr.steps[19].hasNote (60) && tr.lanes[2][21].on, "1 → 2 bars copies bar 1 into bar 2");
    tr.steps[17].addNote (62);
    p.setLengthBars (0, 4);
    ctx.check (tr.steps[35].hasNote (60) && tr.steps[49].hasNote (62), "2 → 4 bars repeats bars 1–2");
    p.setLengthBars (0, 1);
    p.setLengthBars (0, 2, false);
    ctx.check (tr.steps[19].hasNote (60), "shrinking keeps the data; growing without copy leaves it as it was");
}

/** Drums: whatever key plays a part records into that part's lane — exact notes, General MIDI pad notes
    (pedal hat 44, ride 51…) and the C–G keys of any octave — live and in step mode. */
MB_TEST (record_drum_hats)
{
    for (int mode : { 0, 1 })
    {
        Engine e;
        e.prepare (48000.0, 128);
        e.setDevice (0, 0, createDevice (DeviceType::Drums));
        e.focusTrack = 0;
        e.project().recordMode = mode;
        e.project().countIn = false;
        e.setRecord (true);
        if (mode == 0) e.play();
        std::vector<float> l (128), r (128);
        double pos = 0;
        const double stepLen = 48000.0 * 60.0 / 120.0 / 4.0;
        auto runTo = [&] (double steps) { while (pos < steps * stepLen) { e.process (l.data(), r.data(), 128, nullptr); pos += 128; } };
        auto hit = [&] (int note, double atStep)
        {
            if (mode == 0) runTo (atStep);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (10, note, (uint8_t) 100), 0);
            m.addEvent (juce::MidiMessage::noteOff (10, note), 64);
            e.process (l.data(), r.data(), 128, &m); pos += 128;
        };
        hit (42, 1); hit (44, 3); hit (46, 5); hit (64, 7); hit (51, 9);   // closed, pedal, open, E4 (closed by pitch class), ride
        auto& tr = e.project().patterns[0].slots[0].track;
        const int stepOf[5] = { mode == 0 ? 1 : 0, mode == 0 ? 3 : 1, mode == 0 ? 5 : 2, mode == 0 ? 7 : 3, mode == 0 ? 9 : 4 };
        const int laneOf[5] = { 4, 4, 5, 4, 6 };
        for (int i = 0; i < 5; ++i)
            ctx.check (tr.lanes[(size_t) laneOf[i]][(size_t) stepOf[i]].on,
                       juce::String (mode == 0 ? "live" : "step") + ": hit " + juce::String (i + 1) + " lands in lane " + juce::String (laneOf[i] + 1));
    }
}

/** Trig conditions play on the Nth of every M passes; muted notes stay silent. */
MB_TEST (seq_trig_conditions_and_mute)
{
    Rig rig (1);
    auto& tr = rig.track();
    tr.steps[0].addNote (60); tr.steps[0].cond = 1;           // 1:2
    tr.steps[4].addNote (62); tr.steps[4].cond = 10;          // First
    tr.steps[8].addNote (64); tr.steps[8].addNote (67); tr.steps[8].setMuted (1, true);
    rig.e.play();
    rig.runTo (16 * 4 - 0.5);   // four passes
    int n60 = 0, n62 = 0, n64 = 0, n67 = 0;
    for (auto& o : rig.probe->ons) { n60 += o.second == 60; n62 += o.second == 62; n64 += o.second == 64; n67 += o.second == 67; }
    ctx.check (n60 == 2, "1:2 plays on passes 1 and 3 of 4 (" + juce::String (n60) + ")");
    ctx.check (n62 == 1, "First plays once (" + juce::String (n62) + ")");
    ctx.check (n64 >= 4 && n67 == 0, "a muted chord note never plays");
}

/** Live recording: a chord struck around a step's midpoint (here the bar line) stays together on one step, and
    Replace keeps every note of the chord. */
MB_TEST (record_live_chord_stays_together)
{
    for (const bool overdub : { true, false })
    {
        Rig rig (1);
        auto& p = rig.e.project();
        p.patterns[0].lengthBars = 2;
        p.overdub = overdub;
        rig.e.setRecord (true);
        rig.e.play();
        rig.runTo (15.44);
        for (int n : { 60, 64, 67 }) rig.midi (1, n, true);   // about 8 ms apart, across step 16's midpoint
        rig.runTo (17);
        for (int n : { 60, 64, 67 }) rig.midi (1, n, false);
        rig.runTo (18);
        const auto *a = rig.clipNote (60), *b = rig.clipNote (64), *c = rig.clipNote (67);
        ctx.check (a && b && c && a->start == b->start && b->start == c->start && a->start % rig.stepT() == 0,
                   juce::String (overdub ? "overdub" : "replace") + ": the chord's notes start together on a step");
    }
}

/** Off-grid notes play where they are drawn: a note early of its step plays before the step starts (also on the
    pattern's first step), a late one after it. */
MB_TEST (seq_off_grid_notes)
{
    Rig rig (1);
    auto& tr = rig.track();
    tr.steps[4].addNote (60); tr.steps[4].off[0] = -12;   // a quarter step early
    tr.steps[8].addNote (62); tr.steps[8].off[0] = 12;    // a quarter step late
    tr.steps[0].addNote (64); tr.steps[0].off[0] = -6;    // early on the first step: as soon as play starts
    rig.e.play();
    rig.runTo (15.0);
    std::map<int, double> at;
    for (auto& [t, n] : rig.probe->ons) if (at.find (n) == at.end()) at[n] = t / Rig::stepLen;
    ctx.check (at.count (64) && at[64] < 0.05, "the first step's early note plays at the start");
    ctx.check (at.count (60) && std::abs (at[60] - 3.75) < 0.03, "an early note plays before its step (" + juce::String (at[60], 3) + ")");
    ctx.check (at.count (62) && std::abs (at[62] - 8.25) < 0.03, "a late note after it (" + juce::String (at[62], 3) + ")");
    rig.probe->ons.clear();
    rig.runTo (31.0);
    int n60 = 0; for (auto& o : rig.probe->ons) n60 += o.second == 60;
    ctx.check (n60 == 1, "and once per pass");
}

/** Steps and piano roll (docs/sequencer.md): converting keeps every note where it was; a clip plays its notes
    at their exact positions and lengths; Replace recording clears what the playhead passes; step recording on a
    piano-roll track writes the clip. */
MB_TEST (seq_piano_roll_clip)
{
    Rig rig (1);
    auto& tr = rig.track();
    const int st = rig.stepT();
    tr.steps[0].addNote (60, 100, 256);
    tr.steps[4].addNote (62, 90, 64); tr.steps[4].off[0] = -12;
    tr.steps[8].addNote (64); tr.steps[8].addNote (67);
    tr.convertToPianoRoll (st, rig.e.project().patternSteps (0));
    ctx.check (tr.pianoRoll && tr.clip.size() == 4 && ! tr.hasStepNotes(), "converted: four clip notes, no steps left");
    const auto* n62 = rig.clipNote (62);
    ctx.check (n62 && n62->start == 4 * st - st / 4 && n62->length == st / 2 && n62->vel == 90, "an early note keeps its position, length and velocity");
    // free notes: two of the same pitch inside one step, and one between steps
    ClipNote a; a.start = 10 * st; a.length = st / 4; a.note = 70; tr.addClipNote (a);
    a.start = 10 * st + st / 2; tr.addClipNote (a);
    a.start = 12 * st + st / 3; a.note = 72; tr.addClipNote (a);
    rig.e.play();
    rig.runTo (15.5);
    std::vector<double> at70; double at72 = -1, at60 = -1;
    for (auto& [t, n] : rig.probe->ons)
    {
        if (n == 70) at70.push_back (t / Rig::stepLen);
        if (n == 72) at72 = t / Rig::stepLen;
        if (n == 60 && at60 < 0) at60 = t / Rig::stepLen;
    }
    ctx.check (at60 >= 0 && at60 < 0.02, "the clip plays from the start");
    ctx.check (at70.size() == 2 && std::abs (at70[0] - 10.0) < 0.02 && std::abs (at70[1] - 10.5) < 0.02, "the same pitch twice in one step");
    ctx.check (std::abs (at72 - 12.333) < 0.02, "a note between steps plays where it is (" + juce::String (at72, 3) + ")");
    rig.e.stop(); rig.runTo (16.5); rig.e.stop(); rig.runTo (17.0);

    // recording over a clip adds to it (nothing is erased)
    Rig rec (1);
    auto& rt = rec.track();
    rt.pianoRoll = true;
    ClipNote old; old.start = 2 * st; old.note = 50; rt.addClipNote (old);
    old.start = 20 * st; rt.addClipNote (old);   // beyond what the take passes
    rec.e.project().patterns[0].lengthBars = 2;
    rec.e.project().overdub = false;
    rec.e.setRecord (true);
    rec.e.play();
    rec.runTo (5.0); rec.midi (1, 61, true); rec.runTo (6.0); rec.midi (1, 61, false);
    rec.runTo (8.0);
    int n50 = 0; for (auto& n : rt.clip) n50 += n.note == 50;
    ctx.check (n50 == 2, "recording never deletes what was there (" + juce::String (n50) + ")");
    ctx.check (rec.clipNote (61) != nullptr, "and adds the recorded note");
    rec.e.setRecord (false); rec.e.stop(); rec.runTo (8.5);

    // step recording on a piano-roll track writes the clip at the cursor
    rec.e.project().recordMode = 1;
    rec.e.stepRecordCursor = 3;
    rec.e.setRecord (true);
    rec.midi (1, 66, true); rec.midi (1, 66, false);
    const auto* s66 = rec.clipNote (66);
    ctx.check (s66 && s66->start == 3 * st, "step recording on a piano-roll track: the clip note at the cursor");
}


/** Transport (docs/sequencer.md P0): Stop returns to the pattern's start; a seek plays from the step; a loop
    range repeats inside the pattern. */
MB_TEST (seq_pause_seek_loop)
{
    Rig rig (1);
    auto& tr = rig.track();
    for (int i = 0; i < 16; ++i) tr.steps[(size_t) i].addNote (40 + i, 100, 64);
    rig.e.play();
    rig.runTo (5.5);
    rig.e.stop(); rig.runTo (6.0);
    rig.probe->ons.clear();
    ctx.check (rig.e.currentStep() == 0, "Stop returns the cursor to the pattern's start");
    rig.e.play(); rig.runTo (6.2);
    ctx.check (! rig.probe->ons.empty() && rig.probe->ons.front().second == 40, "Play after Stop starts from the beginning (" + juce::String (rig.probe->ons.empty() ? -1 : rig.probe->ons.front().second) + ")");
    rig.runTo (7.2);
    rig.e.seek (12); rig.probe->ons.clear(); rig.runTo (7.9);
    ctx.check (! rig.probe->ons.empty() && rig.probe->ons.front().second == 52, "a seek plays from that step");
    auto& pt = rig.e.project().patterns[0];
    pt.loopOn = true; pt.loopStart = 4; pt.loopEnd = 8;
    rig.e.seek (4); rig.probe->ons.clear(); rig.runTo (17.0);
    std::vector<int> heard;
    for (auto& o : rig.probe->ons) heard.push_back (o.second - 40);
    ctx.check (heard.size() >= 8 && heard[0] == 4 && heard[3] == 7 && heard[4] == 4 && heard[7] == 7, "the loop range repeats steps 5-8");
    pt.loopOn = false;
}

/** Stuck notes: every way a sounding note can be cut short still ends it. */
MB_TEST (seq_no_stuck_notes)
{
    Rig rig (1);
    auto& tr = rig.track();
    const int st = rig.stepT();
    tr.pianoRoll = true;
    ClipNote n; n.start = 0; n.length = 40 * st; n.note = 60; tr.addClipNote (n);   // longer than the pattern
    rig.e.play();
    rig.runTo (2.0);
    ctx.check (rig.probe->active == 1, "a long note sounds");
    rig.e.stop(); rig.runTo (2.5);
    ctx.check (rig.probe->active == 0, "Stop ends it");
    rig.e.play(); rig.runTo (3.0);
    rig.e.seek (8); rig.runTo (3.2);
    ctx.check (rig.probe->active == 0, "a seek ends what was sounding");
    rig.e.stop(); rig.runTo (3.5); rig.e.stop(); rig.runTo (4.0);
    // deleting a sounding note: its note-off still comes
    tr.clip.clear();
    n.length = 4 * st; tr.addClipNote (n);
    rig.e.play(); rig.runTo (5.0);
    ctx.check (rig.probe->active == 1, "(sounding)");
    tr.clip.clear();
    rig.runTo (9.0);
    ctx.check (rig.probe->active == 0, "deleting a sounding note still ends it");
    // a note longer than the loop wraps and still ends
    n.length = 20 * st; tr.addClipNote (n);
    rig.e.toStart(); rig.runTo (60.0);
    tr.clip.clear();
    rig.runTo (100.0);
    ctx.check (rig.probe->active == 0, "notes longer than the pattern end too (" + juce::String (rig.probe->active) + ")");
    // a pattern switch ends the old pattern's notes
    tr.addClipNote (n);
    rig.runTo (102.0);
    rig.e.queuePattern (1);
    rig.runTo (120.0);
    ctx.check (rig.probe->active == 0, "switching pattern ends them");
}

/** The sustain pedal: keys released while it is down sound (and record) until it comes up, chord-mode notes too. */
MB_TEST (input_sustain_pedal)
{
    Rig rig (1);
    auto& ps = rig.e.project().patterns[0].slots[1].play;
    ps.chordOn = true;
    rig.e.setRecord (true);
    rig.e.play();
    rig.runTo (2.0);
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
    rig.e.process (rig.l.data(), rig.r.data(), 128, &m); rig.pos += 128;
    rig.midi (1, 60, true);
    rig.runTo (3.0);
    rig.midi (1, 60, false);
    rig.runTo (6.0);
    ctx.check (rig.probe->active >= 2, "the chord keeps sounding while the pedal is down (" + juce::String (rig.probe->active) + ")");
    m.clear();
    m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
    rig.e.process (rig.l.data(), rig.r.data(), 128, &m); rig.pos += 128;
    rig.e.applyRecorded();
    rig.runTo (6.5);
    ctx.check (rig.probe->active == 0, "pedal up ends every note of the chord");
    const auto* n = rig.clipNote (60);
    ctx.check (n != nullptr && n->length > 3 * rig.stepT(), "the recorded length runs to the pedal (" + juce::String (n ? n->length / (double) rig.stepT() : 0.0) + " steps)");
}

/** Controller lanes and expression (docs/sequencer.md P1): a clip's lanes play at their ticks and are chased
    from a seek; the pitch wheel is recorded into the lane; a macro lane automates the instrument. */
MB_TEST (seq_controller_lanes)
{
    Rig rig (1);
    auto& tr = rig.track();
    const int st = rig.stepT();
    tr.pianoRoll = true;
    tr.setPoint (DeviceProcessor::kBend, 2 * st, 1.0f);
    tr.setPoint (DeviceProcessor::kBend, 6 * st, -0.5f);
    tr.setPoint (DeviceProcessor::kModWheel, 0, 0.7f);
    rig.e.play();
    rig.runTo (1.5);
    ctx.check (std::abs (rig.probe->bend) < 1e-6f && std::abs (rig.probe->modWheel - 0.7f) < 1e-6f, "the lanes start at their first points");
    rig.runTo (3.0);
    ctx.check (std::abs (rig.probe->bend - 1.0f) < 1e-6f, "pitch bend up at step 3");
    rig.runTo (7.0);
    ctx.check (std::abs (rig.probe->bend + 0.5f) < 1e-6f, "and down at step 7");
    rig.e.stop(); rig.runTo (7.5);
    ctx.check (std::abs (rig.probe->bend) < 1e-6f, "Stop resets the controllers");
    rig.e.seek (4); rig.e.play(); rig.runTo (8.0);
    ctx.check (std::abs (rig.probe->bend - 1.0f) < 1e-6f, "a seek to step 5 chases the bend from step 3");
    rig.e.stop(); rig.runTo (8.5); rig.e.stop(); rig.runTo (9.0);

    // recording the pitch wheel
    Rig rec (1);
    rec.e.setRecord (true);
    rec.e.play();
    rec.runTo (3.0);
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 0);
    rec.e.process (rec.l.data(), rec.r.data(), 128, &m); rec.pos += 128; rec.e.applyRecorded();
    rec.runTo (5.0);
    const auto& cc = rec.track().cc;
    ctx.check (rec.track().pianoRoll && cc.size() == 1 && cc[0].lane == DeviceProcessor::kBend && cc[0].value > 0.99f
               && std::abs (cc[0].tick - 3 * rec.stepT()) < rec.stepT() / 4, "the pitch wheel is recorded into the bend lane");
    ctx.check (std::abs (rec.probe->bend - 1.0f) < 0.01f, "and plays while recording");
}

/** Pitch bend and the mod wheel move an instrument's pitch; a macro lane automates its macro. */
MB_TEST (device_expression)
{
    for (auto type : { DeviceType::Poly, DeviceType::Mono, DeviceType::Acid, DeviceType::FM, DeviceType::Sampler })
    {
    auto dev = createDevice (type);
    presets::loadDefault (*dev, true);
    dev->prepare (48000.0, 512);
    auto crossings = [&] (float bend)
    {
        dev->reset();
        dev->setExpression (DeviceProcessor::kBend, bend);
        NoteEvent on; on.type = NoteEvent::NoteOn; on.note = 57; on.velocity = 0.9f;
        dev->handleEvent (on);
        std::vector<float> l (512), r (512), x;
        for (int b = 0; b < 30; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
            dev->render (l.data(), r.data(), 512);
            if (b >= 20) x.insert (x.end(), l.begin(), l.end());
        }
        NoteEvent off; off.type = NoteEvent::AllNotesOff; dev->handleEvent (off);
        // the period: the lag of the strongest autocorrelation after the first dip (a fundamental 50–800 Hz)
        auto ac = [&] (int lag) { double s = 0; for (size_t i = 0; i + (size_t) lag < x.size(); ++i) s += x[i] * x[i + (size_t) lag]; return s; };
        int lag = 60; while (lag < 960 && ac (lag) > 0.0) ++lag;
        int best = lag; double bv = -1e30;
        for (int k = lag; k < 960; ++k) { const double v = ac (k); if (v > bv) { bv = v; best = k; } }
        // refine with a parabola
        const double a1 = ac (best - 1), a2 = ac (best), a3 = ac (best + 1);
        return best + 0.5 * (a1 - a3) / (a1 - 2 * a2 + a3);
    };
    const double p0 = crossings (0.0f), p1 = crossings (1.0f);
    const double ratio = p0 / p1;
    ctx.check (std::abs (ratio - std::pow (2.0, 2.0 / 12.0)) < 0.01, deviceTypeName (type) + ": full bend up is two semitones (" + juce::String (ratio, 4) + ")");
    }
    auto acid = createDevice (DeviceType::Acid);
    Engine e;
    e.prepare (48000.0, 256);
    e.setDevice (0, 1, std::move (acid));
    auto& tr = e.project().patterns[0].slots[1].track;
    tr.pianoRoll = true;
    tr.setPoint (100, 0, 0.83f);
    e.play();
    std::vector<float> l (256), r (256);
    for (int i = 0; i < 20; ++i) e.process (l.data(), r.data(), 256, nullptr);
    ctx.check (std::abs (e.device (0, 1)->getMacro (0) - 0.83f) < 0.02f, "a macro lane sets the macro (" + juce::String (e.device (0, 1)->getMacro (0), 3) + ")");
}

/** Live recording in Replace (the default): the pattern never loops back while recording, it grows a bar at a
    time (the other tracks repeat into it); when the take stops it is the bars its notes reach. With Overdub the
    pattern loops and layers. */
MB_TEST (record_live_grows_pattern)
{
    for (const bool overdub : { false, true })
    {
        Rig rig (1);
        auto& p = rig.e.project();
        ctx.check (! p.overdub, "Overdub is off by default");
        p.overdub = overdub;
        auto drums = std::make_unique<ProbeDevice> (8);
        rig.e.setDevice (0, 0, std::move (drums));
        p.patterns[0].slots[0].track.lanes[0][0].on = true;
        rig.e.setRecord (true);
        rig.e.play();
        // two one-bar chords
        rig.runTo (0.05); for (int n : { 60, 64, 67 }) rig.midi (1, n, true);
        rig.runTo (15.9); for (int n : { 60, 64, 67 }) rig.midi (1, n, false);
        rig.runTo (16.05); for (int n : { 62, 65, 69 }) rig.midi (1, n, true);
        rig.runTo (31.9); for (int n : { 62, 65, 69 }) rig.midi (1, n, false);
        rig.runTo (40.0);
        if (! overdub)
        {
            ctx.check (p.patterns[0].lengthBars == 4, "recording: the pattern doubled (1 → 2 → 4) instead of looping (" + juce::String (p.patterns[0].lengthBars) + " bars)");
            ctx.check (p.patterns[0].slots[0].track.lanes[0][16].on && p.patterns[0].slots[0].track.lanes[0][32].on, "the drums repeat into the new bars");
            const auto* d = rig.clipNote (62);
            ctx.check (d != nullptr && d->start == 16 * rig.stepT(), "the second chord is in bar 2");
            rig.e.setRecord (false);
            ctx.check (p.patterns[0].lengthBars == 2, "two one-bar chords: a 2-bar pattern (" + juce::String (p.patterns[0].lengthBars) + ")");
            rig.runTo (100.0);
            ctx.check (p.patterns[0].lengthBars == 2, "which then loops");
        }
        else
        {
            ctx.check (p.patterns[0].lengthBars == 1, "Overdub: the pattern loops (" + juce::String (p.patterns[0].lengthBars) + " bar)");
            rig.e.setRecord (false);
        }
    }
    // a take that plays nothing leaves the length alone
    Rig idle (1);
    idle.e.setRecord (true); idle.e.play(); idle.runTo (40.0);
    idle.e.setRecord (false);
    ctx.check (idle.e.project().patterns[0].lengthBars == 1, "a silent take: back to 1 bar");
}

MB_TEST (seq_performance_mode)
{
    Rig rig (1);
    auto& p = rig.e.project();
    p.perform = true;
    p.patterns[0].lengthBars = 2;
    rig.e.play();
    rig.runTo (3.0);
    rig.e.queuePattern (1); rig.runTo (3.5);
    rig.runTo (31.5);
    ctx.check (rig.e.activePattern() == 0, "a 2-bar pattern plays to its end first");
    rig.runTo (32.5);
    ctx.check (rig.e.activePattern() == 1, "then the picked one starts");
    rig.e.queuePattern (0); rig.runTo (33.0);
    rig.e.queuePattern (-1); rig.runTo (50.0);
    ctx.check (rig.e.activePattern() == 1, "picking again cancels");
    p.patterns[1].loopOn = true; p.patterns[1].loopStart = 4; p.patterns[1].loopEnd = 8;
    rig.e.seek (4); rig.runTo (51.0);
    rig.e.queuePattern (0); rig.runTo (51.5);
    rig.runTo (55.5);
    ctx.check (rig.e.activePattern() == 0, "a loop range: at the loop's end (" + juce::String (rig.e.activePattern()) + ")");
}

/** The app's timing: 512-sample blocks, the recorder written back by the UI's 30 Hz timer, a human playing two
    one-bar chords (a little early or late), waiting, then Stop. The pattern is 2 bars holding both chords. */
MB_TEST (record_two_chords_like_the_app)
{
    for (const bool countIn : { false, true })
    {
        Engine e;
        e.prepare (48000.0, 512);
        auto dev = std::make_unique<ProbeDevice>();
        e.setDevice (0, 1, std::move (dev));
        e.focusTrack = 1;
        e.project().countIn = countIn;
        std::vector<float> l (512), r (512);
        double samples = 0;
        int sinceUi = 0;
        const double stepLen = 48000.0 * 60.0 / 120.0 / 4.0, offset = countIn ? 16 * stepLen : 0.0;
        auto block = [&] (juce::MidiBuffer* m)
        {
            e.process (l.data(), r.data(), 512, m);
            samples += 512;
            if ((sinceUi += 512) >= 1600) { sinceUi = 0; e.applyRecorded(); }   // the UI timer
        };
        auto runTo = [&] (double steps) { while (samples < offset + steps * stepLen) block (nullptr); };
        auto chord = [&] (std::initializer_list<int> notes, bool on)
        {
            juce::MidiBuffer m;
            int k = 0;
            for (int n : notes) m.addEvent (on ? juce::MidiMessage::noteOn (1, n, (uint8_t) 100) : juce::MidiMessage::noteOff (1, n), 40 * k++);
            block (&m);
        };
        e.setRecord (true);
        e.play();
        runTo (0.2);  chord ({ 60, 64, 67 }, true);
        runTo (15.6); chord ({ 60, 64, 67 }, false);
        runTo (15.8); chord ({ 62, 65, 69 }, true);    // a little early for bar 2
        runTo (31.5); chord ({ 62, 65, 69 }, false);
        runTo (16 * 6.5);                              // waiting
        e.stop();
        runTo (16 * 6.6); e.applyRecorded();
        const auto& tr = e.project().patterns[0].slots[1].track;
        int bar1 = 0, bar2 = 0;
        const int st = e.project().stepTicks (0);
        for (auto& n : tr.clip) { bar1 += n.start < 16 * st; bar2 += n.start >= 16 * st && n.start < 32 * st; }
        const auto tag = juce::String (countIn ? "count-in: " : "");
        ctx.check (bar1 == 3 && bar2 == 3, tag + "both chords recorded (" + juce::String (bar1) + " + " + juce::String (bar2) + " notes)");
        ctx.check (e.project().patterns[0].lengthBars == 2, tag + "the pattern is 2 bars (" + juce::String (e.project().patterns[0].lengthBars) + ")");
        e.setRecord (false);
    }
}

/** Two one-bar chords with count-in, struck early, on time or late (and the first one inside the count-in's last
    moments): each lands on its own bar's downbeat and the take is 2 bars. */
MB_TEST (record_chords_after_count_in)
{
    for (int earlyMs : { -90, -30, 0, 30 })
    {
        Engine e;
        e.prepare (48000.0, 512);
        e.setDevice (0, 1, createDevice (DeviceType::Acid));
        e.focusTrack = 1;
        auto& p = e.project();
        ctx.check (p.countIn, "count-in is on by default");
        std::vector<float> l (512), r (512);
        double samples = 0; int sinceUi = 0;
        const double stepLen = 48000.0 * 60.0 / 120.0 / 4.0, countIn = 16 * stepLen;
        auto block = [&] (juce::MidiBuffer* m) { e.process (l.data(), r.data(), 512, m); samples += 512; if ((sinceUi += 512) >= 1600) { sinceUi = 0; e.applyRecorded(); } };
        auto at = [&] (double steps, std::initializer_list<int> notes, bool on, int ms)
        {
            const double target = countIn + steps * stepLen + ms * 48.0;
            while (samples + 512 <= target) block (nullptr);
            juce::MidiBuffer m;
            for (int n : notes) m.addEvent (on ? juce::MidiMessage::noteOn (1, n, (uint8_t) 100) : juce::MidiMessage::noteOff (1, n), (int) (target - samples));
            block (&m);
        };
        e.setRecord (true);
        e.play();
        // the first chord up to a step early (inside the count-in) still belongs to bar 1; the second within half a step
        at (0, { 60, 64, 67 }, true, earlyMs); at (15.5, { 60, 64, 67 }, false, 0);
        at (16, { 62, 65, 69 }, true, std::max (earlyMs, -30)); at (31.5, { 62, 65, 69 }, false, 0);
        at (40, {}, true, 0);
        e.stop(); block (nullptr); e.applyRecorded();
        const int st = p.stepTicks (0);
        int bar1 = 0, bar2 = 0;
        for (auto& n : p.patterns[0].slots[1].track.clip)
        {
            bar1 += n.start == 0 && (n.note == 60 || n.note == 64 || n.note == 67);
            bar2 += n.start == 16 * st && (n.note == 62 || n.note == 65 || n.note == 69);
        }
        const auto tag = juce::String (earlyMs) + " ms: ";
        ctx.check (bar1 == 3 && bar2 == 3, tag + "chord 1 on bar 1, chord 2 on bar 2 (" + juce::String (bar1) + ", " + juce::String (bar2) + ")");
        ctx.check (p.patterns[0].lengthBars == 2, tag + "a 2-bar pattern");
    }
}

/** Motion recording: a knob turned while recording writes the steps it passes (no gaps), a macro on a piano-roll
    track writes its lane; neither silences the instrument. */
MB_TEST (record_knob_motion)
{
    Engine e;
    e.prepare (48000.0, 512);
    auto dev = createDevice (DeviceType::Mono);
    presets::loadDefault (*dev, true);
    auto* d = dev.get();
    e.setDevice (0, 1, std::move (dev));
    e.project().countIn = false;
    auto& tr = e.project().patterns[0].slots[1].track;
    for (int s = 0; s < 16; s += 2) tr.steps[(size_t) s].addNote (36, 100, 200);
    const int cut = d->params().indexOf ("cutoff");
    e.setRecord (true);
    e.play();
    std::vector<float> l (512), r (512);
    const double stepLen = 6000.0;
    double samples = 0;
    float v = 0.0f;
    while (samples < 8 * stepLen)   // turning slowly for half a bar (a change every ~20 ms; the UI timer at 30 Hz)
    {
        v = d->params()[cut].fromNorm (0.4f + 0.3f * (float) (samples / (8 * stepLen)));
        d->set (cut, v);
        e.touchParam (0, 1, cut, v);
        e.process (l.data(), r.data(), 512, nullptr); samples += 512;
        e.recordTouched();
    }
    auto* lane = tr.lockFor (cut, false);
    int written = 0;
    for (int s = 0; s < 8; ++s) written += lane != nullptr && ! std::isnan (lane->values[(size_t) s]);
    ctx.check (written >= 7, "every step the sweep passed holds a value (" + juce::String (written) + " of 8)");
    // a macro on a piano-roll track: its lane
    auto& clipTr = e.project().patterns[0].slots[1].track;
    clipTr.convertToPianoRoll (e.project().stepTicks (0), e.project().patternSteps (0));
    e.recordMacro (d, 0, 0.25f);
    bool point = false; for (auto& p : clipTr.cc) point = point || (p.lane == 100 && std::abs (p.value - 0.25f) < 1e-6f);
    ctx.check (point, "a macro turned on a piano-roll track writes its lane");
    e.setRecord (false);
}

/** Doubling during a take copies the recorded track's earlier content into the new bars, never the take; when
    the take stops, the bars past its last note go, copies included. */
MB_TEST (record_doubling_copies_pre_take_content)
{
    Rig rig (1);
    auto& p = rig.e.project();
    auto& tr = rig.track();
    const int st = rig.stepT();
    tr.pianoRoll = true;
    ClipNote bass; bass.start = 0; bass.length = st * 4; bass.note = 36; tr.addClipNote (bass);   // there before the take
    rig.e.setRecord (true);
    rig.e.play();
    rig.runTo (0.1);  rig.midi (1, 60, true); rig.runTo (12.0); rig.midi (1, 60, false);
    rig.runTo (16.1); rig.midi (1, 62, true); rig.runTo (28.0); rig.midi (1, 62, false);
    rig.runTo (70.0);   // on into bars 3–5 (the pattern doubles to 8)
    auto count = [&] (int note, int bar) { int c = 0; for (auto& n : tr.clip) c += n.note == note && n.start / (16 * st) == bar; return c; };
    ctx.check (p.patterns[0].lengthBars == 8, "recording: doubled to 8 bars (" + juce::String (p.patterns[0].lengthBars) + ")");
    ctx.check (count (36, 1) == 1 && count (36, 2) == 1 && count (36, 5) == 1, "the bass that was there repeats into the new bars");
    ctx.check (count (60, 1) == 0 && count (60, 4) == 0, "the take's own notes are not copied");
    rig.e.stop(); rig.runTo (70.5);
    ctx.check (p.patterns[0].lengthBars == 2, "stopped: 2 bars, the take's reach (" + juce::String (p.patterns[0].lengthBars) + ")");
    ctx.check (count (36, 0) == 1 && count (36, 1) == 1 && count (62, 1) == 1 && count (36, 2) == 0 && count (36, 5) == 0,
               "bar 2 holds the bass copy and the second chord; the copies past bar 2 are gone");
}

/** Recording only automation never lengthens the pattern or copies its clips: the motion recorded past the
    pattern's end folds back onto it, the later pass winning. */
MB_TEST (record_automation_does_not_duplicate)
{
    Rig rig (1);
    auto& p = rig.e.project();
    auto& tr = rig.track();
    const int st = rig.stepT();
    tr.pianoRoll = true;
    ClipNote bass; bass.start = 0; bass.length = st * 4; bass.note = 36; tr.addClipNote (bass);
    auto other = std::make_unique<ProbeDevice> (0);
    rig.e.setDevice (0, 2, std::move (other));
    rig.track (2).steps[0].addNote (60);
    rig.e.setRecord (true);
    rig.e.play();
    // a lane point every step for two and a half passes (the pattern doubles while recording)
    for (int s = 0; s < 40; ++s)
    {
        rig.runTo (s + 0.5);
        juce::MidiBuffer cc;                                  // the mod wheel, from the keyboard
        cc.addEvent (juce::MidiMessage::controllerEvent (1, 1, juce::roundToInt (s / 40.0f * 127.0f)), 0);
        rig.e.process (rig.l.data(), rig.r.data(), 128, &cc); rig.pos += 128; rig.e.applyRecorded();
        rig.e.recordKnob (0, 1, 0, s / 40.0f);              // a lock on parameter 0
    }
    rig.runTo (40.5);
    ctx.check (p.patterns[0].lengthBars == 1, "while recording automation alone the pattern loops: still 1 bar (" + juce::String (p.patterns[0].lengthBars) + ")");
    rig.e.stop(); rig.runTo (41.0);
    ctx.check (p.patterns[0].lengthBars == 1, "automation alone: still 1 bar (" + juce::String (p.patterns[0].lengthBars) + ")");
    int basses = 0; for (auto& n : tr.clip) basses += n.note == 36;
    ctx.check (basses == 1, "the clip is not duplicated (" + juce::String (basses) + " bass notes)");
    int chords = 0; for (int s = 0; s < kMaxSteps; ++s) chords += rig.track (2).steps[(size_t) s].noteCount;
    ctx.check (chords == 1, "the other track is not duplicated either");
    auto* lock = tr.lockFor (0, false);
    ctx.check (lock != nullptr && std::abs (lock->values[3] - 35 / 40.0f) < 1e-4f, "step 4 holds the last pass's lock (" + juce::String (lock ? lock->values[3] : -1.0f) + ")");
    ctx.check (lock != nullptr && std::abs (lock->values[10] - 26 / 40.0f) < 1e-4f, "step 11: its value from pass 2 (pass 3 stopped before it)");
    int points = 0; float at3 = -1.0f;
    for (auto& c : tr.cc) if (c.lane == 1) { ++points; if (c.tick >= 3 * st && c.tick < 4 * st) at3 = c.value; }
    ctx.check (points > 0 && points <= 16 && std::abs (at3 - juce::roundToInt (35 / 40.0f * 127.0f) / 127.0f) < 1e-3f, "mod wheel: one pass's points, the latest at step 4 (" + juce::String (points) + ", " + juce::String (at3) + ")");
}

/** Note repeat (the phone's drum roll): the press sounds at once, then the note repeats on the beat grid; while
    playing the hits land exactly on the sequencer's steps; releasing stops it. */
MB_TEST (roll_repeats_on_the_grid)
{
    Rig rig (0, 8);   // a drum track: hits go straight to the device
    rig.runTo (0.3);
    rig.e.setRoll (0, 38, 0.9f, 0.25, true);   // 1/16 at 120 BPM = one step
    rig.runTo (8.3);
    rig.e.setRoll (0, 38, 0.0f, 0.25, false);
    rig.runTo (12);
    const auto& ons = rig.probe->ons;
    ctx.check (ons.size() == 9, "stopped: the press plus a hit per step for 8 steps (" + juce::String ((int) ons.size()) + ")");
    bool even = true;
    for (size_t i = 2; i < ons.size(); ++i) even = even && std::abs ((double) (ons[i].first - ons[i - 1].first) - Rig::stepLen) <= 1.0;
    ctx.check (even, "evenly spaced, a step apart");
    ctx.check (ons.size() > 1 && ons[1].first - ons[0].first < Rig::stepLen, "the first repeat waits for the grid, not a whole step after the press");

    rig.probe->ons.clear();
    rig.e.play();
    rig.runTo (14.5);
    rig.e.setRoll (0, 36, 0.9f, 0.5, true);  // 1/8
    rig.runTo (22.5);
    rig.e.setRoll (0, 36, 0.0f, 0.5, false);
    std::vector<int64_t> t;
    for (auto& o : rig.probe->ons) if (o.second == 36) t.push_back (o.first);
    ctx.check (t.size() >= 4, "playing: the roll repeats (" + juce::String ((int) t.size()) + " hits)");
    bool spaced = true;
    for (size_t i = 2; i < t.size(); ++i) spaced = spaced && std::abs ((double) (t[i] - t[i - 1]) - 2 * Rig::stepLen) <= 1.0;
    ctx.check (spaced, "every other step (1/8)");
}

/** Note repeat on everything played (the desktop's N): a held key rolls at the chosen rate; a synth holds each
    repeat for half the rate; releasing ends it cleanly, even if repeat was switched off meanwhile. */
MB_TEST (note_repeat_rolls_played_keys)
{
    Rig rig (1);   // a synth track
    rig.e.noteRepeat = true;
    rig.e.repeatBeats = 0.5;   // 1/8: two steps
    rig.runTo (0.3);
    rig.midi (1, 60, true);
    rig.runTo (8.3);
    rig.midi (1, 60, false);
    rig.runTo (12);
    const auto& p = *rig.probe;
    ctx.check (p.ons.size() == 5, "the press plus a hit every other step for 8 steps (" + juce::String ((int) p.ons.size()) + ")");
    ctx.check (p.offTimes.size() == p.ons.size(), "every repeat ends");
    bool gate = true;
    for (size_t i = 1; i + 1 < p.ons.size() && i < p.offTimes.size(); ++i)
        gate = gate && std::abs ((double) (p.offTimes[i].first - p.ons[i].first) - Rig::stepLen) <= 2.0;
    ctx.check (gate, "each repeat lasts half the rate (one step)");

    rig.probe->ons.clear(); rig.probe->offTimes.clear();
    rig.midi (1, 62, true);
    rig.e.noteRepeat = false;
    rig.runTo (20);
    rig.midi (1, 62, false);
    rig.runTo (24);
    const auto n = rig.probe->ons.size();
    rig.runTo (30);
    ctx.check (n > 1 && rig.probe->ons.size() == n, "switched off while held: the release still stops it");
    ctx.check (rig.probe->offTimes.size() == rig.probe->ons.size(), "no stuck note");

    rig.probe->ons.clear(); rig.probe->offTimes.clear();
    rig.midi (1, 64, true); rig.runTo (34); rig.midi (1, 64, false);
    ctx.check (rig.probe->ons.size() == 1 && rig.probe->offTimes.size() == 1, "off again: a key plays once");
}

/** An Acid slide step holds its note into the next step's (the overlap is what slides), carrying the slide flag. */
MB_TEST (seq_slide_overlaps_next_note)
{
    Rig rig (1);
    auto& tr = rig.track();
    tr.steps[0].addNote (36); tr.steps[0].flags = kSlideFlag;
    tr.steps[1].addNote (43);
    rig.e.play();
    rig.runTo (3);
    const auto& p = *rig.probe;
    int64_t on2 = -1, off1 = -1;
    for (auto& o : p.ons) if (o.second == 43 && on2 < 0) on2 = o.first;
    for (auto& o : p.offTimes) if (o.second == 36 && off1 < 0) off1 = o.first;
    ctx.check (! p.onFlags.empty() && (p.onFlags[0] & kSlideFlag) != 0, "the slide step's note carries the slide flag");
    ctx.check (on2 >= 0 && off1 > on2, "its note-off comes after the next note-on (on " + juce::String (on2) + ", off " + juce::String (off1) + ")");
}

/** The same for a piano-roll clip: a slide note holds into the next note, however short it was drawn. */
MB_TEST (seq_clip_slide_overlaps_next_note)
{
    Rig rig (1);
    auto& tr = rig.track();
    tr.pianoRoll = true;
    const int st = rig.stepT();
    ClipNote a; a.start = 0; a.length = st / 2; a.note = 36; a.flags = kSlideFlag; tr.addClipNote (a);
    ClipNote b; b.start = 2 * st; b.length = st / 2; b.note = 43; tr.addClipNote (b);
    rig.e.play();
    rig.runTo (4);
    const auto& p = *rig.probe;
    int64_t on2 = -1, off1 = -1;
    for (auto& o : p.ons) if (o.second == 43 && on2 < 0) on2 = o.first;
    for (auto& o : p.offTimes) if (o.second == 36 && off1 < 0) off1 = o.first;
    ctx.check (on2 >= 0 && off1 > on2, "the slide note lasts past the next note-on (on " + juce::String (on2) + ", off " + juce::String (off1) + ")");
}
