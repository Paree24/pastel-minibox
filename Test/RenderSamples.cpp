#include "Tests.h"
#include "app/presets/PresetBank.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace mb;

/** Not a check: records the factory sample content from Minibox's own engines.
    Reads build/sample_jobs.json: [{ file, device, values, note, hold, max, part? }]; a drum job plays one
    part (`part`, 0..7) instead of a note. Writes Factory/samples/<file>.wav (mono, 16-bit, 48 kHz),
    trimmed at −70 dB, peak-normalised to −1 dBFS. */
MB_TEST (tool_render_samples)
{
    const auto root = juce::File (MINIBOX_SOURCE_DIR);
    const auto jobs = juce::JSON::parse (root.getChildFile ("build/sample_jobs.json"));
    ctx.check (jobs.isArray(), "jobs found");
    if (! jobs.isArray()) return;
    const auto outDir = root.getChildFile ("Factory/samples");
    outDir.createDirectory();
    const double sr = 48000.0;
    int written = 0;
    for (auto& j : *jobs.getArray())
    {
        const auto type = deviceTypeFromId (j.getProperty ("device", "").toString());
        auto d = createDevice (type);
        auto* doc = new juce::DynamicObject();
        doc->setProperty ("type", deviceTypeId (type));
        doc->setProperty ("values", j.getProperty ("values", {}));
        presets::fromJson (*d, juce::var (doc));
        d->prepare (sr, 256);
        const double hold = (double) j.getProperty ("hold", 1.0), maxLen = (double) j.getProperty ("max", 4.0);
        const int part = (int) j.getProperty ("part", -1);
        const int note = part >= 0 ? d->laneNote (part) : (int) j.getProperty ("note", 60);
        const int total = (int) (maxLen * sr);
        std::vector<float> l ((size_t) total, 0.0f), r ((size_t) total, 0.0f);
        TransportInfo ti; ti.bpm = 120.0; ti.playing = false;
        const auto* hits = j.getProperty ("hits", {}).getArray();   // [[seconds, part, velocity], ...] for loops
        for (int pos = 0; pos < total; pos += 256)
        {
            if (hits != nullptr)
            {
                for (auto& h : *hits)
                {
                    const int at = (int) ((double) h[0] * sr);
                    if (at >= pos && at < pos + 256) { NoteEvent e; e.note = d->laneNote ((int) h[1]); e.velocity = (float) (double) h[2]; d->handleEvent (e); }
                }
            }
            else if (pos == 0) { NoteEvent e; e.note = note; e.velocity = 0.85f; d->handleEvent (e); }
            if (pos <= (int) (hold * sr) && pos + 256 > (int) (hold * sr)) { NoteEvent e; e.type = NoteEvent::NoteOff; e.note = note; d->handleEvent (e); }
            d->setTransport (ti);
            d->render (l.data() + pos, r.data() + pos, std::min (256, total - pos));
        }
        std::vector<float> m ((size_t) total);
        float pk = 1.0e-9f;
        for (int i = 0; i < total; ++i) { m[(size_t) i] = 0.5f * (l[(size_t) i] + r[(size_t) i]); pk = std::max (pk, std::abs (m[(size_t) i])); }
        // trim the tail below −70 dB (relative), fade the last 5 ms
        int end = total;
        const float floorAbs = pk * 3.0e-4f;
        while (hits == nullptr && end > 1 && std::abs (m[(size_t) end - 1]) < floorAbs) --end;   // loops keep their length
        end = std::min (total, end + (int) (0.01 * sr));
        const int fade = std::min (end, (int) (0.005 * sr));
        for (int i = 0; i < fade; ++i) m[(size_t) (end - fade + i)] *= 1.0f - (float) i / (float) fade;
        const float g = 0.89f / pk;
        juce::AudioBuffer<float> b (1, end);
        for (int i = 0; i < end; ++i) b.setSample (0, i, m[(size_t) i] * g);
        const auto f = outDir.getChildFile (j.getProperty ("file", "x").toString() + ".wav");
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 1, 16, {}, 0));
        if (w == nullptr) continue;
        os.release();
        w->writeFromAudioSampleBuffer (b, 0, end);
        ++written;
    }
    std::printf ("    wrote %d samples\n", written);
    ctx.check (written == jobs.getArray()->size(), "all rendered");
}
