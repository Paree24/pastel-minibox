#pragma once

namespace mb
{
/** What the standalone runs on the master bus after the delay / reverb returns and before glue, volume and the
    limiter: the groovebox's performance and master effects (efx/MasterFx). The plugin has none. */
struct MasterInsert
{
    virtual ~MasterInsert() = default;
    /** Under the engine's lock (Engine::setMasterInsert, Engine::prepare). */
    virtual void prepare (double sampleRate, int maxBlock) = 0;
    /** Audio thread, in place. `ppq`: beats since Play (a free-running clock while stopped). */
    virtual void process (float* L, float* R, int n, double bpm, double ppq, bool playing) = 0;

    /** A track's effect slots, run on its device's output before level, pan and sends (audio thread, in place).
        `active`: whether the track has any to run (an effect's tail keeps a sleeping device's track running). */
    virtual bool trackActive (int, int) const { return false; }
    virtual void processTrack (int /*pattern*/, int /*track*/, float*, float*, int, double, double, bool) {}
};
} // namespace mb
