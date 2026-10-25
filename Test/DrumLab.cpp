#include "Tests.h"
#include "devices/drums/DrumsDevice.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace mb;

namespace
{
void writeWav (const juce::File& f, const std::vector<float>& l, const std::vector<float>& r, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
    if (w == nullptr) return;
    os.release();
    juce::AudioBuffer<float> b (2, (int) l.size());
    b.copyFrom (0, 0, l.data(), (int) l.size());
    b.copyFrom (1, 0, r.data(), (int) r.size());
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
} // namespace

/** Not a check: renders every drum part with every engine at its default settings to build/drumlab
    (for measurement against the reference recordings with docs/research/analyze.py). */
MB_TEST (tool_drum_lab)
{
    const double sr = 44100.0;
    const auto dir = juce::File (MINIBOX_SOURCE_DIR).getChildFile ("build/drumlab");
    dir.createDirectory();
    for (int part = 0; part < DrumsDevice::kParts; ++part)
    {
        const auto& engs = drums::enginesForPart (part);
        for (int e = 0; e < (int) engs.size(); ++e)
        {
            DrumsDevice d;
            d.set (d.partParam (part, DrumsDevice::Eng), (float) e);
            d.prepare (sr, 256);
            const int total = (int) (2.5 * sr);
            std::vector<float> l ((size_t) total), r ((size_t) total);
            for (int pos = 0; pos < total; pos += 256)
            {
                if (pos == 0) { NoteEvent ev; ev.note = d.laneNote (part); ev.velocity = 0.8f; d.handleEvent (ev); }
                std::fill (l.begin() + pos, l.begin() + std::min (total, pos + 256), 0.0f);
                std::fill (r.begin() + pos, r.begin() + std::min (total, pos + 256), 0.0f);
                d.render (l.data() + pos, r.data() + pos, std::min (256, total - pos));
            }
            float pk = 0.0f;
            for (int i = 0; i < total; ++i) pk = std::max (pk, std::abs (l[(size_t) i]));
            const auto name = d.laneName (part).replace (" ", "") + "_" + drums::engineName (engs[(size_t) e]);
            writeWav (dir.getChildFile (name + ".wav"), l, r, sr);
            std::printf ("    %-24s peak %.3f\n", name.toRawUTF8(), pk);
        }
    }
    ctx.check (true, "rendered");
}
