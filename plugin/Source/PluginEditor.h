// CyclesEmu's editor: a global strip (MIDI, tempo, output), a page per track
// (sound and LFO, laid out like the Model:Cycles' own pages) and an FX page,
// dressed as a Windows 95 program with PS1/N64-era pixel type (Retro.h).
#pragma once

#include "PluginProcessor.h"
#include "Retro.h"

// A labelled control bound to one parameter.
class ParamControl : public juce::Component
{
public:
    enum class Style { Knob, Choice, Toggle };
    ParamControl(juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& label, Style);
    void resized() override;
    Style style;

private:
    juce::Label caption;
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
        rows.push_back(std::move(row));
    }
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::String title;
    std::vector<std::vector<ParamControl*>> rows;
};

class TrackPage : public juce::Component
{
public:
    TrackPage(juce::AudioProcessorValueTreeState&, int track);
    void resized() override;

private:
    std::vector<std::unique_ptr<ParamControl>> controls;
    Section sound{"SOUND"}, lfo{"LFO"};
    ParamControl* make(juce::AudioProcessorValueTreeState&, int track, const char* id, const juce::String& label,
                       ParamControl::Style);
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

class CyclesEmuEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CyclesEmuEditor(CyclesEmuProcessor&);
    ~CyclesEmuEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void showPage(int index);   // 0-5 tracks, 6 FX

private:
    void timerCallback() override;

    CyclesEmuProcessor& proc;
    retro::Look look;
    std::vector<std::unique_ptr<ParamControl>> globals;
    juce::Label status;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::OwnedArray<juce::Component> pages;
    int current = 0;
};
