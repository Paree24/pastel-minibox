#include "DeviceProcessor.h"
#include "mono/MonoDevice.h"
#include "poly/PolyDevice.h"
#include "acid/AcidDevice.h"
#include "fm/FmDevice.h"
#include "sampler/SamplerDevice.h"
#include "drums/DrumsDevice.h"
#include "drumsampler/DrumSamplerDevice.h"
#include "wavetable/WavetableDevice.h"

namespace mb
{
std::unique_ptr<DeviceProcessor> createDevice (DeviceType t)
{
    switch (t)
    {
        case DeviceType::Mono: return std::make_unique<MonoDevice>();
        case DeviceType::Poly: return std::make_unique<PolyDevice>();
        case DeviceType::Acid: return std::make_unique<AcidDevice>();
        case DeviceType::FM: return std::make_unique<FmDevice>();
        case DeviceType::Sampler: return std::make_unique<SamplerDevice>();
        case DeviceType::Drums: return std::make_unique<DrumsDevice>();
        case DeviceType::DrumSampler: return std::make_unique<DrumSamplerDevice>();
        case DeviceType::Wavetable: return std::make_unique<WavetableDevice>();
        default: return nullptr;
    }
}
} // namespace mb
