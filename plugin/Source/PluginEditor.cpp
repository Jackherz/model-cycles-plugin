#include "PluginEditor.h"

using namespace cyclesemu;

namespace
{
const juce::Colour kBack(0xff18181c), kPanel(0xff24242a), kEdge(0xff34343c), kText(0xffe8e8ea),
    kDim(0xff8c8c96), kAccent(0xffff8a3d);
constexpr int kW = 960, kH = 600;
} // namespace

CyclesEmuLook::CyclesEmuLook()
{
    setColour(juce::ResizableWindow::backgroundColourId, kBack);
    setColour(juce::Label::textColourId, kText);
    setColour(juce::Slider::textBoxTextColourId, kText);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    setColour(juce::Slider::rotarySliderOutlineColourId, kEdge);
    setColour(juce::Slider::trackColourId, kAccent);
    setColour(juce::ComboBox::backgroundColourId, kBack);
    setColour(juce::ComboBox::outlineColourId, kEdge);
    setColour(juce::ComboBox::textColourId, kText);
    setColour(juce::ComboBox::arrowColourId, kAccent);
    setColour(juce::PopupMenu::backgroundColourId, kPanel);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha(0.35f));
    setColour(juce::ToggleButton::textColourId, kText);
    setColour(juce::ToggleButton::tickColourId, kAccent);
    setColour(juce::ToggleButton::tickDisabledColourId, kDim);
    setColour(juce::TextButton::buttonColourId, kPanel);
    setColour(juce::TextButton::buttonOnColourId, kAccent);
    setColour(juce::TextButton::textColourOffId, kText);
    setColour(juce::TextButton::textColourOnId, juce::Colours::black);
}

void CyclesEmuLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start,
                                     float end, juce::Slider& s)
{
    auto r = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(4.0f);
    float size = juce::jmin(r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre(size, size);
    float radius = size / 2.0f, line = 3.5f;
    float angle = start + pos * (end - start);
    juce::Path track, value;
    track.addCentredArc(c.getCentreX(), c.getCentreY(), radius - line, radius - line, 0.0f, start, end, true);
    g.setColour(kEdge);
    g.strokePath(track, juce::PathStrokeType(line, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // bipolar parameters fill from the centre
    bool bipolar = s.getMinimum() == 0 && s.getMaximum() == 127 && s.getTextFromValue(64.0).trim() == "0";
    float from = bipolar ? (start + end) / 2.0f : start;
    value.addCentredArc(c.getCentreX(), c.getCentreY(), radius - line, radius - line, 0.0f,
                        juce::jmin(from, angle), juce::jmax(from, angle), true);
    g.setColour(kAccent);
    g.strokePath(value, juce::PathStrokeType(line, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Point<float> tip(c.getCentreX() + (radius - 9.0f) * std::sin(angle),
                           c.getCentreY() - (radius - 9.0f) * std::cos(angle));
    g.setColour(kText);
    g.drawLine({c.getCentre(), tip}, 2.0f);
}

ParamControl::ParamControl(juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& label,
                           Style s)
    : style(s)
{
    caption.setText(label, juce::dontSendNotification);
    caption.setJustificationType(juce::Justification::centred);
    caption.setColour(juce::Label::textColourId, kDim);
    caption.setFont(juce::FontOptions(12.0f));
    auto* p = st.getParameter(id);
    jassert(p != nullptr);
    switch (s)
    {
        case Style::Knob:
            addAndMakeVisible(caption);
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
            addAndMakeVisible(knob);
            knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(st, id, knob);
            break;
        case Style::Choice:
            addAndMakeVisible(caption);
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
            caption.setBounds(r.removeFromTop(16));
            knob.setBounds(r);
            break;
        case Style::Choice:
            caption.setBounds(r.removeFromTop(16));
            box.setBounds(r.withSizeKeepingCentre(r.getWidth() - 8, 26));
            break;
        case Style::Toggle:
            toggle.setBounds(r.withSizeKeepingCentre(r.getWidth() - 8, 26));
            break;
    }
}

void Section::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(kPanel);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(kEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);
    g.setColour(kAccent);
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(title, getLocalBounds().reduced(12, 6).removeFromTop(18), juce::Justification::left);
}

void Section::resized()
{
    // A grid as wide as the widest row (at least four columns), rows centred
    // and no taller than a knob needs: every control of a kind gets one size.
    auto r = getLocalBounds().reduced(10, 8);
    r.removeFromTop(22);
    if (rows.empty())
        return;
    size_t cols = 4;
    for (auto& row : rows)
        cols = std::max(cols, row.size());
    int cellW = r.getWidth() / (int)cols;
    int rowH = juce::jmin(118, r.getHeight() / (int)rows.size());
    for (auto& row : rows)
    {
        auto line = r.removeFromTop(rowH);
        // rows of choices share the full width (their text needs it)
        bool choices = std::all_of(row.begin(), row.end(),
                                   [](auto* c) { return c->style == ParamControl::Style::Choice; });
        int w = choices ? line.getWidth() / (int)row.size() : cellW;
        int used = w * (int)row.size();
        line.removeFromLeft((line.getWidth() - used) / 2);
        for (auto* c : row)
            c->setBounds(line.removeFromLeft(w).reduced(3));
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
    sound.addRow({make(st, t, "voldist", "Vol+Dist", S::Knob), make(st, t, "pan", "Pan", S::Knob),
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
    sound.setBounds(r.removeFromLeft(r.getWidth() * 60 / 100).reduced(6));
    lfo.setBounds(r.reduced(6));
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
    note.setText("Delay time is tempo-synced (32 = one beat). Reverb size 127 plays as 126 for now: as emulated "
                 "it never decays, while the hardware's does.",
                 juce::dontSendNotification);
    note.setColour(juce::Label::textColourId, kDim);
    note.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(note);
}

void FxPage::resized()
{
    auto r = getLocalBounds();
    note.setBounds(r.removeFromBottom(40).reduced(12, 4));
    auto top = r.removeFromTop(170);
    delay.setBounds(top.removeFromLeft(top.getWidth() / 2).reduced(6));
    reverb.setBounds(top.reduced(6));
}

CyclesEmuEditor::CyclesEmuEditor(CyclesEmuProcessor& p) : AudioProcessorEditor(p), proc(p)
{
    setLookAndFeel(&look);
    auto& st = p.state();
    using S = ParamControl::Style;
    auto g = [&](const char* id, const juce::String& label, S s) {
        globals.push_back(std::make_unique<ParamControl>(st, id, label, s));
        addAndMakeVisible(*globals.back());
    };
    g("midimode", "MIDI Mode", S::Choice);
    g("keystrack", "Keys Track", S::Knob);
    g("followhost", "Follow Host Tempo", S::Toggle);
    g("tempo", "Tempo", S::Knob);
    g("outgain", "Output", S::Knob);
    status.setColour(juce::Label::textColourId, kDim);
    status.setFont(juce::FontOptions(12.0f));
    status.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(status);

    for (int i = 0; i <= kTracks; ++i)
    {
        auto* b = tabs.add(new juce::TextButton(i < kTracks ? "Track " + juce::String(i + 1) : juce::String("FX")));
        b->setClickingTogglesState(true);
        b->setRadioGroupId(1);
        b->onClick = [this, i] { showPage(i); };
        addAndMakeVisible(b);
        if (i < kTracks)
            pages.add(new TrackPage(st, i));
        else
            pages.add(new FxPage(st));
        addChildComponent(pages.getLast());
    }
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
}

void CyclesEmuEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBack);
    auto top = getLocalBounds().removeFromTop(78);
    g.setColour(kPanel);
    g.fillRect(top);
    g.setColour(kText);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("CyclesEmu", top.reduced(16, 0).removeFromTop(48), juce::Justification::bottomLeft);
    g.setColour(kDim);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("Model:Cycles firmware engine", top.reduced(16, 0).removeFromBottom(26),
               juce::Justification::topLeft);
}

void CyclesEmuEditor::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop(78).reduced(8, 4);
    top.removeFromLeft(220);                       // title
    status.setBounds(top.removeFromRight(170));
    int widths[] = {190, 64, 150, 70, 70};
    for (size_t i = 0; i < globals.size(); ++i)
        globals[i]->setBounds(top.removeFromLeft(widths[i]).reduced(4, 0));
    auto bar = r.removeFromTop(40).reduced(10, 6);
    int w = bar.getWidth() / tabs.size();
    for (auto* b : tabs)
        b->setBounds(bar.removeFromLeft(w).reduced(3, 0));
    for (auto* p : pages)
        p->setBounds(r.reduced(4));
}

void CyclesEmuEditor::timerCallback()
{
    if (proc.engineStatus().isNotEmpty())
        status.setText(proc.engineStatus(), juce::dontSendNotification);
    else
        status.setText("Engine tempo " + juce::String(proc.currentBpm(), 1) + " BPM", juce::dontSendNotification);
}
