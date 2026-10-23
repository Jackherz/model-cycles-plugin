// Loads the installed plugins as a host would (JUCE's VST3 hosting, the
// format Ableton Live loads) and runs them like Live: 48 kHz, small and
// uneven blocks, automation, the editor open. A crash here is a crash in a
// host. Usage: CyclesEmuLoadTest [path.vst3 ...]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <thread>

// Live restarts a plugin (setActive -> prepareToPlay) whenever it reports a
// latency change: count the reports, so a plugin that reports one on every
// prepare (an endless restart loop in Live, a hang) fails here.
struct LatencyWatch : juce::AudioProcessorListener
{
    int changes = 0;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& d) override { changes += d.latencyChanged; }
};

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray paths;
    for (int i = 1; i < argc; ++i)
        paths.add(argv[i]);
    if (paths.isEmpty())
        for (auto* name : {"CyclesFX", "CyclesEmu", "CyclesVoice"})
            paths.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                          .getChildFile("Library/Audio/Plug-Ins/VST3")
                          .getChildFile(juce::String(name) + ".vst3")
                          .getFullPathName());
    juce::VST3PluginFormat vst3;
    int failures = 0;
    for (auto& path : paths)
    {
        juce::OwnedArray<juce::PluginDescription> found;
        vst3.findAllTypesForFile(found, path);
        if (found.isEmpty())
        {
            std::printf("%s: no plugin found\n", path.toRawUTF8());
            ++failures;
            continue;
        }
        for (const double rate : {48000.0, 44100.0})
        {
            juce::String error;
            auto plugin = vst3.createInstanceFromDescription(*found[0], rate, 64, error);
            if (plugin == nullptr)
            {
                std::printf("%s: %s\n", path.toRawUTF8(), error.toRawUTF8());
                ++failures;
                continue;
            }
            const int ins = plugin->getTotalNumInputChannels(), outs = plugin->getTotalNumOutputChannels();
            // as Live: prepare, and prepare again for each latency change
            // reported, up to 10 times; a settled plugin stops reporting
            LatencyWatch watch;
            plugin->addListener(&watch);
            plugin->prepareToPlay(rate, 64);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            int restarts = 0;
            for (int seen = 0; watch.changes > seen && restarts < 10; ++restarts)
            {
                seen = watch.changes;
                plugin->releaseResources();
                plugin->prepareToPlay(rate, 64);
                juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            }
            plugin->removeListener(&watch);
            if (restarts >= 10)
            {
                std::printf("%s at %.0f Hz: reports a latency change on every prepare (Live would restart it "
                            "forever) -> FAIL\n", found[0]->name.toRawUTF8(), rate);
                ++failures;
                continue;
            }
            std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorIfNeeded());
            if (editor != nullptr)   // on screen, as Live opens it: painted, its timers running
            {
                editor->addToDesktop(juce::ComponentPeer::windowHasTitleBar);
                editor->setVisible(true);
            }
            juce::AudioBuffer<float> buf(juce::jmax(2, juce::jmax(ins, outs)), 64);
            juce::Random rng(1);
            auto& params = plugin->getParameters();
            const int blocks = (int)(40.0 * rate / 64);
            float peak = 0;
            for (int b = 0; b < blocks; ++b)
            {
                const int n = (b % 7 == 3) ? 1 + rng.nextInt(63) : 64;   // Live splits blocks at automation
                juce::AudioBuffer<float> view(buf.getArrayOfWritePointers(), buf.getNumChannels(), n);
                for (int c = 0; c < view.getNumChannels(); ++c)
                    for (int i = 0; i < n; ++i)
                        view.setSample(c, i, (rng.nextFloat() - 0.5f) * 0.2f);
                juce::MidiBuffer midi;
                if (b % 400 == 0)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 36 + rng.nextInt(40), (juce::uint8)100), 0);
                if (b % 400 == 200)
                    midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
                if (b % 50 == 0 && params.size() > 0)   // automation
                    params[rng.nextInt(params.size())]->setValue(rng.nextFloat());
                plugin->processBlock(view, midi);
                peak = juce::jmax(peak, view.getMagnitude(0, n));
                if (b % 200 == 0)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(5);   // paint, timers
            }
            juce::MemoryBlock state;
            plugin->getStateInformation(state);
            plugin->setStateInformation(state.getData(), (int)state.getSize());
            editor.reset();
            plugin->releaseResources();
            std::printf("%s at %.0f Hz: latency settled after %d restart(s) (%d samples); loaded, 40 s in 64-sample (and split) blocks with automation and the editor "
                        "open, state saved and restored; peak %.3f -> %s\n",
                        found[0]->name.toRawUTF8(), rate, restarts, plugin->getLatencySamples(), peak, std::isfinite(peak) ? "PASS" : "FAIL");
            failures += !std::isfinite(peak);
        }
    }
    // all of them at once in one process, as in a Live set: each processed
    // on its own thread (Live runs tracks in parallel), editors open
    {
        std::vector<std::unique_ptr<juce::AudioPluginInstance>> all;
        std::vector<std::unique_ptr<juce::AudioProcessorEditor>> editors;
        for (auto& path : paths)
        {
            juce::OwnedArray<juce::PluginDescription> found;
            vst3.findAllTypesForFile(found, path);
            juce::String error;
            if (found.isEmpty())
                continue;
            for (int copy = 0; copy < 2; ++copy)   // two of each
            {
                auto plugin = vst3.createInstanceFromDescription(*found[0], 48000.0, 64, error);
                if (plugin == nullptr)
                    continue;
                plugin->prepareToPlay(48000.0, 64);
                editors.emplace_back(plugin->createEditorIfNeeded());
                if (editors.back() != nullptr)
                {
                    editors.back()->addToDesktop(juce::ComponentPeer::windowHasTitleBar);
                    editors.back()->setVisible(true);
                }
                all.push_back(std::move(plugin));
            }
        }
        std::atomic<bool> stop{false};
        std::atomic<int> blocksDone{0};
        std::vector<std::thread> threads;
        for (auto& pl : all)
            threads.emplace_back([&, p = pl.get()] {
                juce::AudioBuffer<float> buf(2, 64);
                juce::Random rng((juce::int64)(size_t)p);
                for (int b = 0; b < 48000 * 20 / 64 && !stop; ++b)
                {
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < 64; ++i)
                            buf.setSample(c, i, (rng.nextFloat() - 0.5f) * 0.2f);
                    juce::MidiBuffer midi;
                    if (b % 300 == 0)
                        midi.addEvent(juce::MidiMessage::noteOn(1 + rng.nextInt(6), 36 + rng.nextInt(40), (juce::uint8)100), 0);
                    p->processBlock(buf, midi);
                    ++blocksDone;
                }
            });
        const auto t0 = juce::Time::getMillisecondCounter();
        while (blocksDone < (int)all.size() * (48000 * 20 / 64) && juce::Time::getMillisecondCounter() - t0 < 120000)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);   // editors paint meanwhile
        stop = true;
        for (auto& t : threads)
            t.join();
        editors.clear();
        std::printf("together: %d instances (two of each) processed in parallel for 20 s with their editors open -> PASS\n",
                    (int)all.size());
    }
    return failures;
}
