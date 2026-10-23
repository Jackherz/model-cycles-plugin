#include "TwinSamples.h"
#include <map>
#include <mutex>

namespace cyclesemu
{
namespace
{
constexpr uint32_t kTable = 0x43030754u;      // the engine's 64 x {length, pointer}
constexpr uint32_t kData = 0x50000000u;       // spare memory above the payload
constexpr int kMaxLength = 0x7ffff;           // the engine reads at most this many samples

void be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}
} // namespace

int TwinSamples::loadFolder(const juce::File& dir)
{
    folder = dir;
    bank = read(dir);
    return count();
}

std::shared_ptr<const TwinSamples::Bank> TwinSamples::read(const juce::File& dir)
{
    auto out = std::make_shared<Bank>();
    if (!dir.isDirectory())
        return out;
    juce::Array<juce::File> files;
    for (const auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.wav;*.aif;*.aiff"))
        if (!f.getFileName().startsWith("."))
            files.add(f);
    // name order, numbers in names counted as numbers (TRANSIENT 2 before 10)
    struct Natural
    {
        static int compareElements(const juce::File& a, const juce::File& b)
        {
            return a.getFileName().compareNatural(b.getFileName());
        }
    } order;
    files.sort(order);
    while (files.size() > kTwinSlots)
        files.removeLast();
    // the same folder, unchanged: the bank already read
    juce::String key = dir.getFullPathName();
    for (const auto& f : files)
        key << "|" << f.getFileName() << ":" << f.getLastModificationTime().toMilliseconds() << ":" << f.getSize();
    static std::mutex lock;
    static std::map<juce::String, std::weak_ptr<const Bank>> cache;
    {
        const std::lock_guard<std::mutex> hold(lock);
        if (auto hit = cache[key].lock())
            return hit;
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const auto& f : files)
    {
        std::unique_ptr<juce::AudioFormatReader> r(formats.createReaderFor(f));
        if (r == nullptr || r->lengthInSamples <= 0)
            continue;
        const int n = (int)juce::jmin<juce::int64>(r->lengthInSamples, 48000 * 30);
        juce::AudioBuffer<float> in((int)r->numChannels, n);
        r->read(&in, 0, n, 0, true, true);
        // mono, then 48 kHz (windowed sinc)
        std::vector<float> mono((size_t)n + 8, 0.0f);
        for (int c = 0; c < in.getNumChannels(); ++c)
            for (int i = 0; i < n; ++i)
                mono[(size_t)i] += in.getSample(c, i) / (float)in.getNumChannels();
        std::vector<float> res;
        if (std::abs(r->sampleRate - 48000.0) < 0.5)
            res.assign(mono.begin(), mono.begin() + n);
        else
        {
            const double ratio = r->sampleRate / 48000.0;
            res.resize((size_t)(n / ratio));
            juce::WindowedSincInterpolator interp;
            interp.process(ratio, mono.data(), res.data(), (int)res.size());
        }
        if ((int)res.size() > kMaxLength)
            res.resize((size_t)kMaxLength);
        std::vector<uint8_t> be(res.size() * 2);
        for (size_t i = 0; i < res.size(); ++i)
        {
            const int v = juce::jlimit(-32768, 32767, juce::roundToInt(res[i] * 32767.0f));
            be[2 * i] = (uint8_t)(v >> 8), be[2 * i + 1] = (uint8_t)v;
        }
        out->names.add(f.getFileNameWithoutExtension());
        out->data.push_back(std::move(be));
    }
    const std::lock_guard<std::mutex> hold(lock);
    cache[key] = out;
    return out;
}

void TwinSamples::install(mc_engine* engine) const
{
    if (engine == nullptr)
        return;
    uint8_t table[kTwinSlots * 8] = {};
    uint32_t at = kData;
    for (int i = 0; i < count(); ++i)
    {
        const auto& d = bank->data[(size_t)i];
        mc_engine_write(engine, at, d.data(), (uint32_t)d.size());
        be32(table + 8 * i, (uint32_t)(d.size() / 2));
        be32(table + 8 * i + 4, at);
        at += ((uint32_t)d.size() + 15) & ~15u;
    }
    mc_engine_write(engine, kTable, table, sizeof table);
}

juce::String TwinSamples::describe(int knob) const
{
    const bool reversed = knob < 64;
    const int slot = reversed ? 63 - knob : knob - 64;
    const juce::String n = name(slot);
    return (reversed ? "<" : "") + (n.isEmpty() ? juce::String(slot + 1) : n);
}
} // namespace cyclesemu
