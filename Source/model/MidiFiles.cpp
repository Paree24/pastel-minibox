#include "MidiFiles.h"

namespace mb::midifile
{
namespace
{
constexpr int kFilePPQ = 960;

bool readFile (const juce::File& f, juce::MidiFile& mf, juce::String& error)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) { error = "Can't open " + f.getFileName(); return false; }
    if (! mf.readFrom (in)) { error = f.getFileName() + " is not a MIDI file"; return false; }
    if (mf.getTimeFormat() <= 0) { error = "SMPTE-timed MIDI files are not supported"; return false; }
    return true;
}

juce::String trackName (const juce::MidiMessageSequence& seq)
{
    for (auto* e : seq) if (e->message.isTrackNameEvent()) return e->message.getTextFromTextMetaEvent();
    return {};
}
} // namespace

Result exportPattern (Engine& engine, int pattern, const juce::File& file)
{
    Result r;
    const auto& proj = engine.project();
    const auto& pt = proj.patterns[(size_t) pattern];
    const int stT = proj.stepTicks (pattern), total = proj.patternSteps (pattern);
    const double toFile = kFilePPQ / (double) kPPQ;
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (kFilePPQ);
    {
        juce::MidiMessageSequence meta;
        auto name = juce::MidiMessage::textMetaEvent (3, proj.name + " - Pattern " + juce::String::charToString ("ABCD"[pattern]));
        meta.addEvent (name, 0);
        meta.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / std::max (20.0, proj.bpm))), 0);
        meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (proj.beats, proj.unit), 0);
        meta.addEvent (juce::MidiMessage::endOfTrack(), (double) proj.patternTicks (pattern) * toFile);
        mf.addTrack (meta);
    }
    for (int t = 0; t < kTracks; ++t)
    {
        auto* dev = engine.device (pattern, t);
        if (dev == nullptr) continue;
        const bool drum = dev->laneCount() > 0;
        const int ch = drum ? 10 : t + 1;
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, deviceTypeName (dev->type())), 0);
        auto note = [&] (int tick, int length, int n, int vel)
        {
            const double a = tick * toFile, b = (tick + std::max (1, length)) * toFile;
            seq.addEvent (juce::MidiMessage::noteOn (ch, n, (juce::uint8) std::clamp (vel, 1, 127)), a);
            seq.addEvent (juce::MidiMessage::noteOff (ch, n), b);
            ++r.notes;
        };
        const auto& tr = pt.slots[(size_t) t].track;
        if (drum)
        {
            for (int l = 0; l < dev->laneCount(); ++l)
                for (int s = 0; s < total; ++s)
                {
                    const auto& d = tr.lanes[(size_t) l][(size_t) s];
                    if (! d.on) continue;
                    const int rat = std::clamp ((int) d.ratchet, 1, 4);
                    for (int k = 0; k < rat; ++k)
                        note (std::max (0, s * stT + d.microTiming * stT / kTicksPerStep + k * stT / rat), stT / rat / 2, dev->laneNote (l), d.velocity);
                }
        }
        else
        {
            // a step sequence is written as a converted copy (exact positions, lengths and ratchets)
            Track copy = tr;
            copy.convertToPianoRoll (stT, total);
            for (auto& n : copy.clip)
                if (! n.muted && n.start < total * stT) note (n.start, n.length, n.note, n.vel);
        }
        seq.updateMatchedPairs();
        seq.addEvent (juce::MidiMessage::endOfTrack(), seq.getEndTime());
        mf.addTrack (seq);
    }
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! mf.writeTo (out)) { r.error = "Can't write " + file.getFullPathName(); return r; }
    r.ok = true;
    return r;
}

std::vector<TrackInfo> tracksWithNotes (const juce::File& file)
{
    std::vector<TrackInfo> out;
    juce::MidiFile mf;
    juce::String err;
    if (! readFile (file, mf, err)) return out;
    for (int i = 0; i < mf.getNumTracks(); ++i)
    {
        int n = 0;
        for (auto* e : *mf.getTrack (i)) n += e->message.isNoteOn() ? 1 : 0;
        if (n > 0) out.push_back ({ i, trackName (*mf.getTrack (i)), n });
    }
    return out;
}

Result importTrack (Engine& engine, int pattern, int track, const juce::File& file, int fileTrack)
{
    Result r;
    juce::MidiFile mf;
    if (! readFile (file, mf, r.error)) return r;
    auto* dev = engine.device (pattern, track);
    if (dev == nullptr) { r.error = "The track has no instrument"; return r; }
    const double toClip = kPPQ / (double) mf.getTimeFormat();
    struct N { int start, length, note, vel; };
    std::vector<N> notes;
    for (int i = 0; i < mf.getNumTracks(); ++i)
    {
        if (fileTrack >= 0 && i != fileTrack) continue;
        juce::MidiMessageSequence seq (*mf.getTrack (i));
        seq.updateMatchedPairs();
        for (int k = 0; k < seq.getNumEvents(); ++k)
        {
            auto* e = seq.getEventPointer (k);
            if (! e->message.isNoteOn()) continue;
            const double on = e->message.getTimeStamp();
            const double off = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() : on + mf.getTimeFormat() / 4.0;
            notes.push_back ({ (int) std::lround (on * toClip), std::max (1, (int) std::lround ((off - on) * toClip)), e->message.getNoteNumber(), e->message.getVelocity() });
        }
    }
    if (notes.empty()) { r.error = "No notes in that track"; return r; }
    auto& proj = engine.project();
    const Engine::ScopedEdit edit (engine);
    // the pattern grows to hold the notes (up to its longest)
    int end = 0;
    for (auto& n : notes) end = std::max (end, n.start + 1);
    const int barT = proj.stepsPerBar (pattern) * proj.stepTicks (pattern);
    const int needBars = (end + barT - 1) / barT;
    if (needBars > proj.patterns[(size_t) pattern].lengthBars) proj.setLengthBars (pattern, needBars, false);
    const int stT = proj.stepTicks (pattern), totalT = proj.patternTicks (pattern);
    auto& tr = proj.patterns[(size_t) pattern].slots[(size_t) track].track;
    if (dev->laneCount() > 0)
    {
        for (auto& lane : tr.lanes) for (auto& d : lane) d.clear();
        for (auto& n : notes)
        {
            const int l = dev->laneForNote (n.note), s = (int) std::lround (n.start / (double) stT);
            if (l < 0 || n.start >= totalT || s >= proj.patternSteps (pattern)) { ++r.dropped; continue; }
            auto& d = tr.lanes[(size_t) l][(size_t) s];
            d.on = true; d.velocity = (uint8_t) std::clamp (n.vel, 1, 127);
            d.microTiming = (int8_t) std::clamp ((int) std::lround ((n.start - s * stT) * kTicksPerStep / (double) stT), -24, 24);
            ++r.notes;
        }
    }
    else
    {
        for (auto& s : tr.steps) s.clear();
        tr.clip.clear();
        tr.pianoRoll = true;
        for (auto& n : notes)
        {
            if (n.start >= totalT) { ++r.dropped; continue; }
            ClipNote c;
            c.start = n.start; c.length = n.length; c.note = (uint8_t) std::clamp (n.note, 0, 127); c.vel = (uint8_t) std::clamp (n.vel, 1, 127);
            tr.addClipNote (c);
            ++r.notes;
        }
    }
    r.ok = true;
    return r;
}
} // namespace mb::midifile
