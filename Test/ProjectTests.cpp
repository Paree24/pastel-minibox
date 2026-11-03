#include "Tests.h"
#include "model/Serializer.h"
#include "model/MidiFiles.h"
#include "app/presets/PresetBank.h"
#include "devices/SampleSlot.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace mb;

namespace
{
void buildDemo (Engine& e)
{
    const char* kinds[4] = { "drums", "acid", "poly", "drumsampler" };
    forceSyncSampleLoads() = true;
    for (int t = 0; t < 4; ++t)
    {
        auto d = createDevice (deviceTypeFromId (kinds[t]));
        presets::loadDefault (*d, true);
        e.setDevice (0, t, std::move (d));
    }
    auto pd = createDevice (DeviceType::Sampler);
    presets::loadDefault (*pd, true);
    e.setDevice (1, 2, std::move (pd));
    forceSyncSampleLoads() = false;
    auto& p = e.project();
    p.name = "Round Trip";
    p.bpm = 126.5; p.beats = 7; p.unit = 8; p.swing = 0.62f;
    p.patterns[0].lengthBars = 2;
    auto& acid = p.patterns[0].slots[1].track;
    acid.steps[0].addNote (36); acid.steps[0].flags = kAccent;
    acid.steps[3].addNote (48); acid.steps[3].flags = kSlide; acid.steps[3].setLength (200);
    if (auto* lane = acid.lockFor (e.device (0, 1)->params().indexOf ("cutoff"), true)) lane->values[3] = 2400.0f;
    p.patterns[0].slots[0].track.lanes[0][0].on = true;
    p.patterns[0].slots[0].track.lanes[2][2] = { true, 90, 50, 3, -6, 0 };
    p.patterns[0].slots[2].mixer = { 0.6f, -0.3f, 0.2f, 0.4f, true, false };
}
} // namespace

MB_TEST (project_save_load_roundtrip)
{
    Engine a;
    a.prepare (48000.0, 256);
    buildDemo (a);
    auto f = juce::File::createTempFile (".minibox");
    auto r = serializer::save (a, f);
    ctx.check (r.ok, "saved: " + r.error);

    Engine b;
    b.prepare (48000.0, 256);
    forceSyncSampleLoads() = true;
    auto r2 = serializer::load (b, f);
    forceSyncSampleLoads() = false;
    ctx.check (r2.ok, "loaded: " + r2.error);

    auto j1 = juce::JSON::toString (serializer::projectToJson (a.project()));
    auto j2 = juce::JSON::toString (serializer::projectToJson (b.project()));
    ctx.check (j1 == j2, "project settings survive");
    for (int p = 0; p < kPatterns; ++p)
        for (int t = 0; t < kTracks; ++t)
        {
            auto* da = a.device (p, t); auto* db = b.device (p, t);
            ctx.check ((da == nullptr) == (db == nullptr), "device presence " + juce::String (p) + "/" + juce::String (t));
            if (da == nullptr || db == nullptr) continue;
            juce::ValueTree ta ("Device"), tb ("Device");
            da->save (ta); db->save (tb);
            ctx.check (ta.toXmlString() == tb.toXmlString(), "device state " + juce::String (p) + "/" + juce::String (t));
        }
    const auto& sa = a.project().patterns[0].slots[1].track, & sb = b.project().patterns[0].slots[1].track;
    ctx.check (sb.steps[3].hasNote (48) && sb.steps[3].flags == kSlide && sb.steps[3].len[0] == 200, "acid slide step");
    ctx.check (sb.locks.size() == 1 && std::abs (sb.locks[0].values[3] - 2400.0f) < 0.01f, "cutoff lock");
    ctx.check (b.project().patterns[0].slots[0].track.lanes[2][2].ratchet == 3, "drum lane ratchet");
    ctx.check (b.project().patterns[0].slots[2].mixer.mute, "mixer mute");
    juce::ignoreUnused (sa);
    f.deleteFile();
}

MB_TEST (project_new_is_empty)
{
    Engine e;
    e.prepare (48000.0, 256);
    buildDemo (e);
    e.newProject();
    int devices = 0, notes = 0;
    for (int p = 0; p < kPatterns; ++p)
        for (int t = 0; t < kTracks; ++t)
        {
            devices += e.device (p, t) != nullptr;
            for (auto& s : e.project().patterns[(size_t) p].slots[(size_t) t].track.steps) notes += s.noteCount;
        }
    ctx.check (devices == 0 && notes == 0, "a new project has no devices and no notes");
    ctx.check (e.project().name == "Untitled", "named Untitled");
}

MB_TEST (project_export_wav)
{
    Engine e;
    e.prepare (48000.0, 256);
    buildDemo (e);
    auto f = juce::File::createTempFile (".wav");
    auto r = serializer::exportWav (e, f, 1);
    ctx.check (r.ok, "export: " + r.error);
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    ctx.check (rd != nullptr, "wav readable");
    if (rd)
    {
        juce::AudioBuffer<float> buf (2, (int) rd->lengthInSamples);
        rd->read (&buf, 0, buf.getNumSamples(), 0, true, true);
        ctx.check (buf.getMagnitude (0, buf.getNumSamples()) > 0.01f, "export has audio");
        ctx.check (rd->bitsPerSample == 24 && rd->numChannels == 2, "24-bit stereo");
    }
    f.deleteFile();
}

/** MIDI files: a pattern written out (clip, steps, drums) comes back note for note. */
MB_TEST (midi_file_round_trip)
{
    Engine e;
    e.prepare (48000.0, 512);
    e.setDevice (0, 0, createDevice (DeviceType::Drums));
    e.setDevice (0, 1, createDevice (DeviceType::Acid));
    e.setDevice (0, 2, createDevice (DeviceType::Poly));
    auto& p = e.project();
    p.patterns[0].lengthBars = 2;
    const int st = p.stepTicks (0);
    p.patterns[0].slots[0].track.lanes[0][0].on = true;
    p.patterns[0].slots[0].track.lanes[4][2].on = true;
    p.patterns[0].slots[1].track.steps[3].addNote (48, 90, 128);
    auto& clip = p.patterns[0].slots[2].track;
    clip.pianoRoll = true;
    ClipNote c; c.start = st / 3; c.length = 5 * st; c.note = 67; c.vel = 77; clip.addClipNote (c);
    c.start = 20 * st; c.note = 70; clip.addClipNote (c);
    const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("minibox-test.mid");
    const auto w = midifile::exportPattern (e, 0, f);
    ctx.check (w.ok && w.notes == 5, "the pattern is written (" + juce::String (w.notes) + " notes)");
    const auto tracks = midifile::tracksWithNotes (f);
    ctx.check (tracks.size() == 3, "three instrument tracks with notes");
    // into pattern B: the VA clip, the Acid step and the drums
    e.setDevice (1, 0, createDevice (DeviceType::Drums));
    e.setDevice (1, 2, createDevice (DeviceType::Poly));
    e.setDevice (1, 1, createDevice (DeviceType::Acid));
    const auto r = midifile::importTrack (e, 1, 2, f, tracks.size() == 3 ? tracks[2].index : -1);
    const auto& got = p.patterns[1].slots[2].track;
    ctx.check (r.ok && got.pianoRoll && got.clip.size() == 2 && p.patterns[1].lengthBars == 2, "the clip comes back (and the pattern grew to 2 bars)");
    ctx.check (got.clip.size() == 2 && got.clip[0].start == st / 3 && got.clip[0].length == 5 * st && got.clip[0].vel == 77, "at its exact position, length and velocity");
    midifile::importTrack (e, 1, 1, f, tracks[1].index);
    const auto& acid = p.patterns[1].slots[1].track;
    ctx.check (acid.pianoRoll && acid.clip.size() == 1 && acid.clip[0].start == 3 * st && acid.clip[0].note == 48, "a step note comes back as a clip note");
    midifile::importTrack (e, 1, 0, f, tracks[0].index);
    const auto& dr = p.patterns[1].slots[0].track;
    ctx.check (dr.lanes[0][0].on && dr.lanes[4][2].on, "drums land on their parts and steps");
    f.deleteFile();
}

/** Export song: the patterns with notes, in order, each once: B's notes come after A's whole length, empty C is
    skipped, D follows B. */
MB_TEST (export_song_plays_patterns_in_order)
{
    Engine e;
    e.prepare (48000.0, 512);
    for (int p : { 0, 1, 3 }) e.setDevice (p, 0, createDevice (DeviceType::Mono));
    auto& pr = e.project();
    pr.countIn = false;
    for (int p : { 0, 1, 3 }) { pr.patterns[(size_t) p].lengthBars = 1; pr.patterns[(size_t) p].slots[0].track.steps[0].addNote (48 + p * 7); }
    const auto f = juce::File::createTempFile (".wav");
    auto r = serializer::exportSongWav (e, f);
    ctx.check (r.ok, "song rendered: " + r.error);
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    const double bar = 48000.0 * 2.0;   // one 4/4 bar at 120
    ctx.check (rd != nullptr && std::abs ((double) rd->lengthInSamples - (3 * bar + 3 * 48000.0)) < 2048, "three one-bar patterns and the tail ("
               + juce::String (rd ? rd->lengthInSamples / bar : 0.0, 2) + " bars)");
    if (rd != nullptr)
    {
        juce::AudioBuffer<float> b (2, (int) rd->lengthInSamples);
        rd->read (&b, 0, b.getNumSamples(), 0, true, true);
        auto onsetAfter = [&] (double from) { for (int i = (int) from; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > 0.01f) return i; return -1; };
        const int a = onsetAfter (0), bOn = onsetAfter (bar - 2000 > 0 ? bar * 0.98 : 0);
        ctx.check (a >= 0 && a < 2000, "A starts at once");
        ctx.check (bOn >= 0, "B plays");
    }
    f.deleteFile();
}

/** Song and pattern exports in FLAC and Ogg Vorbis (by the file's extension): readable, the right length. */
MB_TEST (export_flac_and_ogg)
{
    Engine e;
    e.prepare (48000.0, 512);
    e.setDevice (0, 0, createDevice (DeviceType::Mono));
    e.project().patterns[0].slots[0].track.steps[0].addNote (48);
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    for (auto ext : { ".flac", ".ogg" })
    {
        const auto f = juce::File::createTempFile (ext);
        auto r = serializer::exportSongWav (e, f);
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        ctx.check (r.ok && rd != nullptr && rd->getFormatName() == (juce::String (ext) == ".flac" ? "FLAC file" : "Ogg-Vorbis file")
                   && std::abs (rd->lengthInSamples - (2 * 48000 + 3 * 48000)) < 4096,
                   juce::String (ext) + " song: " + (rd ? rd->getFormatName() + ", " + juce::String (rd->lengthInSamples) : r.error));
        f.deleteFile();
    }
}

/** The metronome is for playing along: an export with it on has no click (a project with no notes renders silence). */
MB_TEST (export_has_no_metronome)
{
    Engine e;
    e.prepare (48000.0, 256);
    e.project().metronome = true;
    e.project().clickLevel = 1.0f;
    auto f = juce::File::createTempFile (".wav");
    auto r = serializer::exportWav (e, f, 1);
    ctx.check (r.ok, "export: " + r.error);
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd)
    {
        juce::AudioBuffer<float> buf (2, (int) rd->lengthInSamples);
        rd->read (&buf, 0, buf.getNumSamples(), 0, true, true);
        ctx.check (buf.getMagnitude (0, buf.getNumSamples()) < 1.0e-4f, "silent: no click in the file, peak " + juce::String (buf.getMagnitude (0, buf.getNumSamples())));
    }
    ctx.check (e.project().metronome, "the live project keeps its metronome on");
    f.deleteFile();
}
