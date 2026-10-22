// CyclesEmu's editor: a global strip (MIDI, tempo, output), a page per track
// (sound and LFO, laid out like the Model:Cycles' own pages) and an FX page,
// dressed as a Windows 95 program with PS1/N64-era pixel type (Retro.h).
#pragma once

#include "PluginProcessor.h"
#include "Retro.h"
#include "Help.h"
#include "Icons.h"
#include "Presets.h"
#include "Randomise.h"

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
    enum class Style { Knob, Choice, Toggle, Icons, Switch };   // Switch: a toggle drawn as a lit plate
    ParamControl(juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& label, Style,
                 cyclesemu::IconSet icons = cyclesemu::IconSet::Machine);
    void resized() override;
    void setLabel(const juce::String&);   // a new caption (knobs, toggles)
    Style style;
    juce::String paramID;
    int preferredWidth = 0;   // in a row without knobs; 0 shares the rest

    static constexpr int kIcon = 24;

private:
    juce::Label caption;
    juce::String label;
    std::vector<std::unique_ptr<IconButton>> icons;
    std::unique_ptr<juce::ParameterAttachment> iconAtt;
    // a knob that returns to the hardware's default on double-click
    // a knob that returns to the hardware's default on double-click, and,
    // while dragged, snaps to its musical values (delay time, LFO speed)
    // when within a step and a half of one; Alt turns it freely
    struct Knob : juce::Slider
    {
        std::function<void()> onDoubleClick;
        const std::vector<int>* snaps = nullptr;
        void mouseDoubleClick(const juce::MouseEvent&) override
        {
            if (onDoubleClick)
                onDoubleClick();
        }
        double snapValue(double v, DragMode mode) override
        {
            if (snaps == nullptr || mode == notDragging || juce::ModifierKeys::currentModifiers.isAltDown())
                return v;
            for (int s : *snaps)
                if (std::abs(v - s) <= 1.5)
                    return s;
            return v;
        }
    } knob;
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
    // a die on the title line: randomise the section (shift: a variation)
    void enableDice(std::function<void(bool variation)> onDice);
    int minColumns = 4;   // the grid's narrowest: 4 keeps knobs alike across pages

private:
    std::unique_ptr<IconButton> dice;
    struct Row
    {
        std::vector<ParamControl*> controls;
        juce::Component* custom;
        int minHeight;
    };
    juce::String title;
    std::vector<Row> rows;
};

// The LFO as it will play over one bar from the note: the waveform with
// depth, start phase, fade, direction and trig mode applied, over the ghost
// of the plain waveform. The faster the LFO, the more cycles fit in the bar,
// growing continuously (one cycle at least, 64 at most), so the drawing
// itself shows the speed as it changes. Redrawn only when something changes.
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
    double windowCycles(const State&) const;            // cycles drawn
    float valueAt(const State&, double cycles, double window) const;   // -1..1
    void timerCallback() override;
    juce::Rectangle<int> plotArea() const;

    CyclesEmuProcessor& proc;
    int track;
    std::atomic<float>* raw[9];
    State shown{};
};

// The tempo as a Metal Gear Solid Codec: a TEMPO plate, a screen with
// signal bars (the output level) and a seven-segment readout, arrows that
// step the tempo, and a HOST plate that follows the host's tempo when lit.
// Drag the screen up or down to change the tempo.
class CodecTempo : public juce::Component, private juce::Timer
{
public:
    explicit CodecTempo(CyclesEmuProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<int> screen() const, left() const, right() const, hostPlate() const;
    bool following() const;
    void step(double delta);

    CyclesEmuProcessor& proc;
    juce::RangedAudioParameter *tempo, *follow;
    float level = 0.0f;
    double shownBpm = 0.0, dragStart = 0.0;
    bool dragging = false;
};

// The track filter's response on a Codec screen: low-pass or high-pass at
// the cutoff with its resonance peak, flat when OFF, on a log-frequency
// scale; the envelope's amount and decay in the header.
class FilterScope : public juce::Component, private juce::Timer
{
public:
    FilterScope(CyclesEmuProcessor&, int track);
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;
    std::atomic<float>* raw[4];
    int shown[4] = {-1, -1, -1, -1};
};

class TrackPage : public juce::Component
{
public:
    TrackPage(CyclesEmuProcessor&, int track);
    std::function<void(const juce::String&)> beforeChange;   // the editor's undo snapshot
    void resized() override;

private:
    std::vector<std::unique_ptr<ParamControl>> controls;
    Section sound{"SOUND"}, lfo{"LFO"}, filter{"FILTER"};
    LfoScope scope;
    FilterScope filterScope;
    // the machine knobs (color, shape, sweep, contour) and punch, relabelled
    // for the Syntakt engines, which map them to parameters of their own
    ParamControl* macroKnobs[5] = {};
    juce::Random rng;
    std::unique_ptr<juce::ParameterAttachment> machineAtt;
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
    Section delay{"DELAY"}, reverb{"REVERB"}, input{"INPUT"};
    bool hasInput = false;   // CyclesFX: the sends' amounts and Mix
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

// A Win95 menu bar: names that open popup menus.
class MenuStrip : public juce::Component
{
public:
    struct Menu
    {
        juce::String name;
        std::function<juce::PopupMenu()> build;
    };
    std::vector<Menu> menus;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> itemArea(int i) const;
    int open = -1;
};

// The mixer: a strip per track, with a Codec meter (the track's own signal,
// before its level; dimmed while it is silenced), Level, Pan, the two sends,
// Mute and Solo. The knobs are the track pages' own parameters.
class MixerPage : public juce::Component, private juce::Timer
{
public:
    explicit MixerPage(CyclesEmuProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    juce::Rectangle<int> strip(int t) const;
    juce::Rectangle<int> meter(int t) const;

    CyclesEmuProcessor& proc;
    std::vector<std::unique_ptr<ParamControl>> controls;   // per strip: level, pan, delay, reverb, mute, solo
    float shown[cyclesemu::kTracks] = {};
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
    juce::PopupMenu kitMenu(), trackMenu(), viewMenu(), helpMenu();
    void setTheme(int index);
    void chooseFile(bool kits, bool saving);
    void randomiseTrack(cyclesemu::RandomSection, bool variation);
    void updateHelp(juce::Component* under);

    CyclesEmuProcessor& proc;
    retro::Look look;
    std::vector<std::unique_ptr<ParamControl>> globals;
    CodecTempo codecTempo{proc};
    juce::Label status;
    juce::TextButton helpButton{"?"};
    HelpBubble bubble;
    bool helpMode = false;
    juce::Component* helpTarget = nullptr;
    MenuStrip menuBar;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ValueTree trackClipboard;
    // one step of undo for the menus' and dice's changes: the kit before it
    juce::ValueTree undoKit;
    juce::String undoWhat;
    void remember(const juce::String& what);
    juce::Random rng;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::OwnedArray<juce::Component> pages;
    int current = 0;
};
