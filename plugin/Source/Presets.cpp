#include "Presets.h"
#include "PluginProcessor.h"

namespace cyclesemu
{
namespace
{
// a track preset leaves the mix alone: level and mute belong to the kit
bool inTrackPreset(const char* id)
{
    const juce::String s(id);
    return s != "level" && s != "mute" && s != "solo";
}

void putParams(juce::AudioProcessorValueTreeState& st, juce::ValueTree& tree, int track, bool all)
{
    const ParamDef* defs = track < kTracks ? kTrackParams : kFxParams;
    const int n = track < kTracks ? kNumTrackParams : kNumFxParams;
    for (int i = 0; i < n; ++i)
        if (all || inTrackPreset(defs[i].id))
            if (auto* p = st.getParameter(paramId(track, defs[i].id)))
                tree.setProperty(defs[i].id, p->convertFrom0to1(p->getValue()), nullptr);
}

void getParams(juce::AudioProcessorValueTreeState& st, const juce::ValueTree& tree, int track, bool all)
{
    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        auto name = tree.getPropertyName(i).toString();
        if (!all && !inTrackPreset(name.toRawUTF8()))
            continue;
        setPlain(st, paramId(track, name.toRawUTF8()), (float)tree.getProperty(name));
    }
}
} // namespace

juce::File presetFolder(bool kits)
{
    auto f = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                 .getChildFile("CyclesEmu")
                 .getChildFile(kits ? "Kits" : "Tracks");
    f.createDirectory();
    return f;
}

void setPlain(juce::AudioProcessorValueTreeState& st, const juce::String& id, float value)
{
    if (auto* p = st.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(value));
        p->endChangeGesture();
    }
}

juce::ValueTree trackToTree(juce::AudioProcessorValueTreeState& st, int track)
{
    juce::ValueTree t("CyclesTrack");
    t.setProperty("version", 1, nullptr);
    juce::ValueTree params("Params");
    putParams(st, params, track, false);
    t.appendChild(params, nullptr);
    return t;
}

void treeToTrack(juce::AudioProcessorValueTreeState& st, int track, const juce::ValueTree& t)
{
    if (t.hasType("CyclesTrack"))
        getParams(st, t.getChildWithName("Params"), track, false);
}

juce::ValueTree kitToTree(juce::AudioProcessorValueTreeState& st)
{
    juce::ValueTree k("CyclesKit");
    k.setProperty("version", 1, nullptr);
    for (int t = 0; t < kTracks; ++t)
    {
        juce::ValueTree tr("Track");
        tr.setProperty("index", t, nullptr);
        putParams(st, tr, t, true);
        k.appendChild(tr, nullptr);
    }
    juce::ValueTree fx("FX");
    putParams(st, fx, kTracks, true);
    k.appendChild(fx, nullptr);
    return k;
}

void treeToKit(juce::AudioProcessorValueTreeState& st, const juce::ValueTree& k)
{
    if (!k.hasType("CyclesKit"))
        return;
    for (auto tr : k)
        if (tr.hasType("Track"))
        {
            const int t = tr.getProperty("index", -1);
            if (t >= 0 && t < kTracks)
                getParams(st, tr, t, true);
        }
        else if (tr.hasType("FX"))
            getParams(st, tr, kTracks, true);
}

bool writeTree(const juce::ValueTree& t, const juce::File& f)
{
    if (auto xml = t.createXml())
        return xml->writeTo(f);
    return false;
}

juce::ValueTree readTree(const juce::File& f)
{
    if (auto xml = juce::XmlDocument::parse(f))
        return juce::ValueTree::fromXml(*xml);
    return {};
}
} // namespace cyclesemu
