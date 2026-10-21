#include "PluginEditor.h"

using namespace cyclesemu;
using namespace retro;

namespace
{
constexpr int kW = 960, kH = 600;
// the window's furniture
const juce::Rectangle<int> kTitle(4, 4, kW - 8, 22), kToolbar(10, 32, kW - 20, 124), kTabs(14, 160, kW - 28, 24),
    kPanel(10, 182, kW - 20, 386), kStatus(4, kH - 28, kW - 8, 24);
constexpr int kLogoW = 220;

void setAccent(juce::Component& c, juce::Colour a) { c.getProperties().set("accent", (int)a.getARGB()); }
} // namespace

ParamControl::ParamControl(juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& label,
                           Style s)
    : style(s)
{
    caption.setText(label, juce::dontSendNotification);
    caption.setJustificationType(juce::Justification::centred);
    auto* p = st.getParameter(id);
    jassert(p != nullptr);
    switch (s)
    {
        case Style::Knob:
            addAndMakeVisible(caption);
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 20);
            addAndMakeVisible(knob);
            knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(st, id, knob);
            break;
        case Style::Choice:
            addAndMakeVisible(caption);
            caption.setJustificationType(juce::Justification::centredLeft);
            box.addItemList(p->getAllValueStrings(), 1);
            addAndMakeVisible(box);
            boxAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(st, id, box);
            break;
        case Style::Toggle:
            toggle.setButtonText(label);
            addAndMakeVisible(toggle);
            toggleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(st, id, toggle);
            break;
    }
}

void ParamControl::resized()
{
    auto r = getLocalBounds();
    switch (style)
    {
        case Style::Knob:
            caption.setBounds(r.removeFromTop(18));
            knob.setBounds(r);
            break;
        case Style::Choice:
            r = r.withSizeKeepingCentre(r.getWidth() - 8, 44);
            caption.setBounds(r.removeFromTop(18).withTrimmedLeft(2));
            box.setBounds(r.withHeight(24));
            break;
        case Style::Toggle:
            toggle.setBounds(r.withSizeKeepingCentre(r.getWidth() - 8, 24));
            break;
    }
}

void Section::paint(juce::Graphics& g) { groupBox(g, getLocalBounds(), title, accentFor(*this)); }

void Section::resized()
{
    // A grid as wide as the widest row (at least four columns). Rows of
    // choices and toggles are short; rows with knobs share the rest (up to a
    // knob's worth each) and the spare height spreads out between rows.
    auto r = getLocalBounds().reduced(12, 8);
    r.removeFromTop(16);
    if (rows.empty())
        return;
    size_t cols = 4;
    for (auto& row : rows)
        cols = std::max(cols, row.size());
    int cellW = r.getWidth() / (int)cols;
    auto hasKnob = [](auto& row) {
        return std::any_of(row.begin(), row.end(), [](auto* c) { return c->style == ParamControl::Style::Knob; });
    };
    const int shortH = 52;
    int knobRows = 0, fixed = 0;
    for (auto& row : rows)
        if (hasKnob(row))
            ++knobRows;
        else
            fixed += shortH;
    int knobH = knobRows ? juce::jmin(118, (r.getHeight() - fixed) / knobRows) : 0;
    int gap = (r.getHeight() - fixed - knobH * knobRows) / (int)(rows.size() + 1);
    for (auto& row : rows)
    {
        r.removeFromTop(gap);
        auto line = r.removeFromTop(hasKnob(row) ? knobH : shortH);
        // rows without knobs share the full width (their text needs it)
        int w = hasKnob(row) ? cellW : line.getWidth() / (int)row.size();
        int used = w * (int)row.size();
        line.removeFromLeft((line.getWidth() - used) / 2);
        for (auto* c : row)
            c->setBounds(line.removeFromLeft(w).reduced(3, 0));
    }
}

ParamControl* TrackPage::make(juce::AudioProcessorValueTreeState& st, int track, const char* id,
                              const juce::String& label, ParamControl::Style s)
{
    controls.push_back(std::make_unique<ParamControl>(st, paramId(track, id), label, s));
    return controls.back().get();   // the section it is put in owns its place
}

TrackPage::TrackPage(juce::AudioProcessorValueTreeState& st, int t)
{
    using S = ParamControl::Style;
    addAndMakeVisible(sound);
    addAndMakeVisible(lfo);
    sound.addRow({make(st, t, "machine", "Machine", S::Choice), make(st, t, "punch", "Punch", S::Toggle),
                  make(st, t, "gate", "Gate", S::Toggle), make(st, t, "mute", "Mute", S::Toggle)});
    sound.addRow({make(st, t, "pitch", "Pitch", S::Knob), make(st, t, "decay", "Decay", S::Knob),
                  make(st, t, "color", "Color", S::Knob), make(st, t, "shape", "Shape", S::Knob),
                  make(st, t, "sweep", "Sweep", S::Knob), make(st, t, "contour", "Contour", S::Knob)});
    sound.addRow({make(st, t, "voldist", "Vol+Dst", S::Knob), make(st, t, "pan", "Pan", S::Knob),
                  make(st, t, "delaysend", "Delay", S::Knob), make(st, t, "reverbsend", "Reverb", S::Knob),
                  make(st, t, "level", "Level", S::Knob)});
    lfo.addRow({make(st, t, "lfowave", "Waveform", S::Choice), make(st, t, "lforeset", "Trig Mode", S::Choice)});
    lfo.addRow({make(st, t, "lfosync", "Sync", S::Choice), make(st, t, "lfomult", "Multiplier", S::Choice)});
    lfo.addRow({make(st, t, "lfodest", "Destination", S::Choice)});
    lfo.addRow({make(st, t, "lfospeed", "Speed", S::Knob), make(st, t, "lfodepth", "Depth", S::Knob),
                make(st, t, "lfofade", "Fade", S::Knob), make(st, t, "lfophase", "Phase", S::Knob)});
}

void TrackPage::resized()
{
    auto r = getLocalBounds();
    sound.setBounds(r.removeFromLeft(r.getWidth() * 62 / 100).withTrimmedRight(6));
    lfo.setBounds(r.withTrimmedLeft(6));
}

FxPage::FxPage(juce::AudioProcessorValueTreeState& st)
{
    using S = ParamControl::Style;
    auto mk = [&](const char* id, const juce::String& label) {
        controls.push_back(std::make_unique<ParamControl>(st, paramId(kTracks, id), label, S::Knob));
        return controls.back().get();
    };
    addAndMakeVisible(delay);
    addAndMakeVisible(reverb);
    auto* dt = mk("delaytime", "Time");
    auto* fb = mk("delayfeedback", "Feedback");
    auto* sz = mk("reverbsize", "Size");
    auto* tn = mk("reverbtone", "Tone");
    delay.addRow({dt, fb});
    reverb.addRow({sz, tn});
}

void FxPage::paint(juce::Graphics& g)
{
    // a Win95 message: the "i" icon and the text beside it
    auto r = noteArea;
    auto icon = r.removeFromLeft(32).withHeight(32);
    g.setColour(kNavy);
    g.fillEllipse(icon.toFloat());
    g.setColour(kLight);
    g.fillRect(icon.getCentreX() - 2, icon.getY() + 7, 4, 4);
    g.fillRect(icon.getCentreX() - 2, icon.getY() + 13, 4, 12);
    drawWrapped(g,
                "Delay time is tempo-synced (32 = one beat). Reverb size 127 plays as 126 for now: as emulated it "
                "never decays, while the hardware's does.",
                r.withTrimmedLeft(14).withTrimmedTop(2), kText);
}

void FxPage::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop(190);
    delay.setBounds(top.removeFromLeft(top.getWidth() / 2).withTrimmedRight(6));
    reverb.setBounds(top.withTrimmedLeft(6));
    noteArea = r.withTrimmedTop(24).removeFromTop(48).reduced(12, 0);
}

CyclesEmuEditor::CyclesEmuEditor(CyclesEmuProcessor& p) : AudioProcessorEditor(p), proc(p)
{
    setLookAndFeel(&look);
    setAccent(*this, kNavy);
    auto& st = p.state();
    using S = ParamControl::Style;
    auto g = [&](const char* id, const juce::String& label, S s) {
        globals.push_back(std::make_unique<ParamControl>(st, id, label, s));
        addAndMakeVisible(*globals.back());
    };
    g("midimode", "MIDI Mode", S::Choice);
    g("keystrack", "Key Trk", S::Knob);
    g("followhost", "Host Tempo", S::Toggle);
    g("tempo", "Tempo", S::Knob);
    g("outgain", "Output", S::Knob);
    status.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(status);

    for (int i = 0; i <= kTracks; ++i)
    {
        auto* b = tabs.add(new juce::TextButton(i < kTracks ? "Track " + juce::String(i + 1) : juce::String("FX")));
        b->setClickingTogglesState(true);
        b->setRadioGroupId(1);
        b->getProperties().set("tab", true);
        setAccent(*b, kAccents[i]);
        b->onClick = [this, i] { showPage(i); };
        if (i < kTracks)
            pages.add(new TrackPage(st, i));
        else
            pages.add(new FxPage(st));
        setAccent(*pages.getLast(), kAccents[i]);
        addChildComponent(pages.getLast());
    }
    // tabs above the panel's edge, so the selected one opens into it
    for (auto* b : tabs)
        addAndMakeVisible(b);
    setSize(kW, kH);
    showPage(0);
    startTimerHz(8);
}

CyclesEmuEditor::~CyclesEmuEditor() { setLookAndFeel(nullptr); }

void CyclesEmuEditor::showPage(int index)
{
    current = index;
    for (int i = 0; i < pages.size(); ++i)
    {
        pages[i]->setVisible(i == index);
        tabs[i]->setToggleState(i == index, juce::dontSendNotification);
    }
    tabs[index]->toFront(false);
    repaint();
}

void CyclesEmuEditor::paint(juce::Graphics& g)
{
    auto all = getLocalBounds();
    g.fillAll(kFace);
    raised(g, all);

    // title bar: navy to blue through a dither, a four-colour icon, the
    // caption buttons
    ditherGradient(g, kTitle, kNavy, kTitleEnd);
    for (int i = 0; i < 4; ++i)
    {
        g.setColour(kAccents[i]);
        g.fillRect(kTitle.getX() + 4 + (i % 2) * 7, kTitle.getY() + 4 + (i / 2) * 7, 6, 6);
    }
    drawText(g, "CyclesEmu.exe", kTitle.getX() + 24, kTitle.getY() + 4, kLight);
    auto caps = kTitle.withTrimmedRight(3).withSizeKeepingCentre(kTitle.getWidth() - 6, 16);
    const char* glyph[] = {"x", "[", "_"};
    for (int i = 0; i < 3; ++i)
    {
        auto b = caps.removeFromRight(18);
        if (i == 0)
            caps.removeFromRight(2);
        g.setColour(kFace);
        g.fillRect(b);
        raised(g, b, false);
        if (i == 1)
        {
            g.setColour(kDark);
            g.drawRect(b.getX() + 4, b.getY() + 3, 9, 8, 1);
            g.fillRect(b.getX() + 4, b.getY() + 3, 9, 2);
        }
        else
            drawText(g, glyph[i], b.withTrimmedTop(1), juce::Justification::centred, kDark, 1);
    }

    // toolbar: the logo and the global settings in a group box
    groupBox(g, kToolbar, "System", kNavy);
    auto logo = kToolbar.withTrimmedTop(26).withTrimmedLeft(18).withWidth(kLogoW);
    const juce::String word = "CYCLES";
    for (int i = 0; i < word.length(); ++i)
    {
        int x = logo.getX() + i * 6 * 5;
        drawText(g, word.substring(i, i + 1), x + 3, logo.getY() + 3, kDark, 5);
        drawText(g, word.substring(i, i + 1), x, logo.getY(), kAccents[i % 4], 5);
    }
    drawText(g, "Emu  v1.0", logo.getX() + 2, logo.getY() + 46, kText);
    drawText(g, "Model:Cycles", logo.getX() + 2, logo.getY() + 64, kDimText);

    // the page panel, and the status bar
    g.setColour(kFace);
    g.fillRect(kPanel);
    raised(g, kPanel);
    auto sb = kStatus;
    auto right = sb.removeFromRight(200);
    sunken(g, sb.withTrimmedRight(2), false);
    sunken(g, right, false);
    juce::String where = current < kTracks ? "Track " + juce::String(current + 1) + " of 6" : juce::String("Send FX");
    g.setColour(kAccents[current]);
    g.fillRect(right.getX() + 8, right.getCentreY() - 4, 8, 8);
    drawText(g, where, right.withTrimmedLeft(24), juce::Justification::centredLeft, kText);
}

void CyclesEmuEditor::resized()
{
    auto top = kToolbar.withTrimmedTop(20).reduced(12, 6);
    top.removeFromLeft(kLogoW);
    int widths[] = {252, 100, 160, 86, 86};
    for (size_t i = 0; i < globals.size(); ++i)
        globals[i]->setBounds(top.removeFromLeft(widths[i]).reduced(4, 0));
    status.setBounds(kStatus.withTrimmedRight(202).reduced(8, 2));
    auto bar = kTabs;
    for (auto* b : tabs)
        b->setBounds(bar.removeFromLeft(120).withTrimmedRight(-1).withHeight(26));
    for (auto* p : pages)
        p->setBounds(kPanel.reduced(10, 12));
}

void CyclesEmuEditor::timerCallback()
{
    if (proc.engineStatus().isNotEmpty())
        status.setText(proc.engineStatus(), juce::dontSendNotification);
    else
        status.setText("Ready.  Engine tempo " + juce::String(proc.currentBpm(), 1) + " BPM",
                       juce::dontSendNotification);
}
