// CyclesEmu's editor: a global strip (MIDI, tempo, output), a page per track
// (sound and LFO, laid out like the Model:Cycles' own pages) and an FX page,
// dressed as a Windows 95 program with PS1/N64-era pixel type (Retro.h).
#pragma once

#include "PluginProcessor.h"
#include "Retro.h"
#include "Help.h"
#include "Icons.h"

// A pictogram button (Icons.h): raised, or a sunken LCD when selected.
class IconButton : public juce::Button
{
public:
    IconButton(cyclesemu::IconSet s, int i) : juce::Button({}), set(s), index(i) {}
    void paintButton(juce::Graphics&, bool over, bool down) override;

private:
    cyclesemu::IconSet set;
    int index;
};

// A labelled control bound to one parameter. Icons: a row of pictogram
// buttons for a choice, captioned with the label and the chosen name.
class ParamControl : public juce::Component
{
public:
    enum class Style { Knob, Choice, Toggle, Icons };
    ParamControl(juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& label, Style,
                 cyclesemu::IconSet icons = cyclesemu::IconSet::Machine);
    void resized() override;
    Style style;
    juce::String paramID;
    int preferredWidth = 0;   // in a row without knobs; 0 shares the rest

    static constexpr int kIcon = 24;

private:
    juce::Label caption;
    juce::String label;
    std::vector<std::unique_ptr<IconButton>> icons;
    std::unique_ptr<juce::ParameterAttachment> iconAtt;
    juce::Slider knob;
    juce::ComboBox box;
    juce::ToggleButton toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> boxAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAtt;
};

// A titled group of controls laid out in rows.
class Section : public juce::Component
{
public:
    explicit Section(const juce::String& title) : title(title) {}
    void addRow(std::vector<ParamControl*> row)
    {
        for (auto* c : row)
            addAndMakeVisible(c);
        rows.push_back({std::move(row), nullptr, 0});
    }
    // a component of its own, as tall as `minHeight` or what knob rows spare
    void addCustomRow(juce::Component* c, int minHeight)
    {
        addAndMakeVisible(c);
        rows.push_back({{}, c, minHeight});
    }
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        std::vector<ParamControl*> controls;
        juce::Component* custom;
        int minHeight;
    };
    juce::String title;
    std::vector<Row> rows;
};

// The LFO as it will play: the waveform with depth, start phase, fade,
// direction and trig mode applied, the ghost of the plain waveform behind
// it, and a dot at the LFO's estimated position (from the time since the
// track's last note and the rate). Redrawn only when something changes.
class LfoScope : public juce::Component, private juce::Timer
{
public:
    LfoScope(CyclesEmuProcessor&, int track);
    void paint(juce::Graphics&) override;

private:
    struct State
    {
        int wave, trig, sync, mult, dest, speed, depth, fade, phase;
        double bpm;
        auto key() const { return std::tie(wave, trig, sync, mult, dest, speed, depth, fade, phase, bpm); }
        bool operator!=(const State& o) const { return key() != o.key(); }
    };
    State read() const;
    double cyclesPerSecond(const State&) const;
    float valueAt(const State&, double cycles) const;   // -1..1, cycles since the trig
    void timerCallback() override;
    juce::Rectangle<int> plotArea() const;

    CyclesEmuProcessor& proc;
    int track;
    std::atomic<float>* raw[9];
    State shown{};
    int dotX = -1;
};

class TrackPage : public juce::Component
{
public:
    TrackPage(CyclesEmuProcessor&, int track);
    void resized() override;

private:
    std::vector<std::unique_ptr<ParamControl>> controls;
    Section sound{"SOUND"}, lfo{"LFO"};
    LfoScope scope;
    ParamControl* make(juce::AudioProcessorValueTreeState&, int track, const char* id, const juce::String& label,
                       ParamControl::Style, cyclesemu::IconSet = cyclesemu::IconSet::Machine);
};

class FxPage : public juce::Component
{
public:
    explicit FxPage(juce::AudioProcessorValueTreeState&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    std::vector<std::unique_ptr<ParamControl>> controls;
    Section delay{"DELAY"}, reverb{"REVERB"};
    juce::Rectangle<int> noteArea;
};

// The "?" mode's tooltip: a Win95 yellow box with a title and wrapped text.
class HelpBubble : public juce::Component
{
public:
    HelpBubble() { setInterceptsMouseClicks(false, false); }
    void show(const cyclesemu::HelpText&, juce::Rectangle<int> target, juce::Rectangle<int> within);
    void paint(juce::Graphics&) override;

private:
    cyclesemu::HelpText text;
};

class CyclesEmuEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CyclesEmuEditor(CyclesEmuProcessor&);
    ~CyclesEmuEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void showPage(int index);   // 0-5 tracks, 6 FX
    void showHelpFor(const juce::String& paramID);   // help mode on, as if hovering that control

    void mouseMove(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent& e) override { mouseMove(e); }
    void mouseExit(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void setHelpMode(bool on);
    void updateHelp(juce::Component* under);

    CyclesEmuProcessor& proc;
    retro::Look look;
    std::vector<std::unique_ptr<ParamControl>> globals;
    juce::Label status;
    juce::TextButton helpButton{"?"};
    HelpBubble bubble;
    bool helpMode = false;
    ParamControl* helpTarget = nullptr;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::OwnedArray<juce::Component> pages;
    int current = 0;
};
