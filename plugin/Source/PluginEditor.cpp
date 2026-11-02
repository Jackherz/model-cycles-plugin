#include "PluginEditor.h"
#include "Settings.h"
#include "Fanfare.h"
#include "LfoShape.h"

using namespace cyclesemu;
using namespace retro;

namespace
{
constexpr int kW = 960, kH = 780;
// the window's furniture
const juce::Rectangle<int> kTitle(4, 4, kW - 8, 22), kMenu(4, 27, kW - 8, 20), kToolbar(10, 52, kW - 20, 124),
    kTabs(14, 180, kW - 28, 24);
// CyclesFX has one page: no tabs, and a window only as tall as its page
constexpr int kFxH = 560;
constexpr int kLogoW = 176;

void setAccent(juce::Component& c, juce::Colour a) { c.getProperties().set("accent", (int)a.getARGB()); }
} // namespace

void IconButton::paintButton(juce::Graphics& g, bool, bool down)
{
    auto r = getLocalBounds();
    if (getToggleState())
    {
        g.setColour(kLcd);
        g.fillRect(r);
        sunken(g, r);
        drawIcon(g, set, index, r.translated(1, 1), kLcdText);
        return;
    }
    g.setColour(kFace);
    g.fillRect(r);
    down ? sunken(g, r) : raised(g, r);
    drawIcon(g, set, index, down ? r.translated(1, 1) : r, kText);
}

ParamControl::ParamControl(juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& lbl,
                           Style s, IconSet iconSet)
    : style(s), paramID(id), label(lbl)
{
    getProperties().set("helpId", id);
    caption.setText(label, juce::dontSendNotification);
    caption.setJustificationType(juce::Justification::centred);
    auto* p = st.getParameter(id);
    jassert(p != nullptr);
    switch (s)
    {
        case Style::Knob:
            addAndMakeVisible(caption);
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            {
                const auto bare = id.startsWith("fx_") || (id.length() > 3 && id[0] == 't' && id[2] == '_')
                                      ? id.substring(3) : id;
                if (!cyclesemu::snapPoints(bare).empty())
                    knob.snaps = &cyclesemu::snapPoints(bare);
            }
            knob.onDoubleClick = [&st, id, p] {
                // the hardware's default (the processor knows the kit and the
                // machine), else the parameter's own
                float v = -1.0f;
                if (auto* proc = dynamic_cast<CyclesEmuProcessor*>(&st.processor))
                    v = proc->defaultFor(id);
                if (v < 0.0f)
                    v = p->convertFrom0to1(p->getDefaultValue());
                cyclesemu::setPlain(st, id, v);
            };
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, 20);
            addAndMakeVisible(knob);
            knobAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(st, id, knob);
            break;
        case Style::Choice:
            addAndMakeVisible(caption);
            caption.setJustificationType(juce::Justification::centredLeft);
            box.addItemList(p->getAllValueStrings(), 1);
            addAndMakeVisible(box);
            boxAtt = std::make_unique<juce::ParameterAttachment>(
                *p,
                [this](float v) {
                    const juce::ScopedValueSetter<bool> updating(boxUpdating, true);
                    box.setSelectedItemIndex(juce::roundToInt(v), juce::dontSendNotification);
                },
                nullptr);
            box.onChange = [this] {
                if (boxUpdating)
                    return;
                const int i = box.getSelectedItemIndex();
                boxAtt->setValueAsCompleteGesture((float)i);
                if (onPicked)
                    onPicked(i);
            };
            boxAtt->sendInitialUpdate();
            break;
        case Style::Switch:
            toggle.getProperties().set("switch", true);
            [[fallthrough]];
        case Style::Toggle:
            toggle.setButtonText(label);
            addAndMakeVisible(toggle);
            toggleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(st, id, toggle);
            break;
        case Style::Icons:
        {
            addAndMakeVisible(caption);
            caption.setJustificationType(juce::Justification::centredLeft);
            auto names = p->getAllValueStrings();
            for (int i = 0; i < names.size(); ++i)
            {
                icons.push_back(std::make_unique<IconButton>(iconSet, i));
                icons.back()->onClick = [this, i] {
                    iconAtt->setValueAsCompleteGesture((float)i);
                    if (onPicked)
                        onPicked(i);
                };
                addAndMakeVisible(*icons.back());
            }
            iconAtt = std::make_unique<juce::ParameterAttachment>(
                *p,
                [this, names](float v) {
                    int sel = juce::roundToInt(v);
                    for (int i = 0; i < (int)icons.size(); ++i)
                        icons[(size_t)i]->setToggleState(i == sel, juce::dontSendNotification);
                    caption.setText(label.isEmpty() ? names[sel] : label + " " + names[sel],
                                    juce::dontSendNotification);
                },
                nullptr);
            iconAtt->sendInitialUpdate();
            // wide enough for the icons and the longest caption
            int textW = 0;
            for (auto& n : names)
                textW = juce::jmax(textW, retro::textWidth(label.isEmpty() ? n : label + " " + n));
            preferredWidth = juce::jmax(textW, (int)icons.size() * kIcon) + 4;
            break;
        }
    }
}

void ParamControl::setLabel(const juce::String& text)
{
    label = text;
    if (style == Style::Toggle)
        toggle.setButtonText(text);
    else
        caption.setText(text, juce::dontSendNotification);
}

void ParamControl::setItemText(int index, const juce::String& text)
{
    // the shown text too, if it is the one selected: ComboBox::setText looks
    // the text up in its items and dropped the selection instead
    const int id = box.getSelectedId();
    box.changeItemText(index + 1, text);
    if (id == index + 1)
    {
        box.setSelectedId(0, juce::dontSendNotification);
        box.setSelectedId(id, juce::dontSendNotification);
    }
}

juce::String ParamControl::itemText(int index) const { return box.getItemText(index); }

juce::String ParamControl::shownText() const { return box.getText(); }

void ParamControl::setValueText(std::function<juce::String(double)> f)
{
    knob.textFromValueFunction = std::move(f);
    knob.updateText();
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
        case Style::Switch:   // a plate, never taller than a button (in a row of knobs too)
            toggle.setBounds(r.withSizeKeepingCentre(r.getWidth(), juce::jmin(r.getHeight(), 32)));
            break;
        case Style::Icons:
        {
            r = r.withSizeKeepingCentre(r.getWidth(), 18 + kIcon);
            caption.setBounds(r.removeFromTop(18));
            for (auto& b : icons)
                b->setBounds(r.removeFromLeft(kIcon));
            break;
        }
    }
}

void Section::paint(juce::Graphics& g) { groupBox(g, getLocalBounds(), title, accentFor(*this)); }

void Section::enableDice(std::function<void(bool)> onDice)
{
    dice = std::make_unique<IconButton>(IconSet::Dice, 0);
    dice->onClick = [onDice] { onDice(juce::ModifierKeys::currentModifiers.isShiftDown()); };
    dice->getProperties().set("helpId", "dice");
    addAndMakeVisible(*dice);
}

void Section::resized()
{
    // A grid as wide as the widest row (at least four columns). Rows of
    // choices, toggles and icons are short; rows with knobs (and custom
    // rows) share the rest, and the spare height spreads out between rows.
    if (dice)
        dice->setBounds(getWidth() - 36, 0, 24, 18);
    // only what is shown: hidden controls take no cell, and a row with none
    // shown takes no line (Twinshot's own row, step 38)
    std::vector<Row> rows;
    for (auto& row : this->rows)
    {
        Row shown{{}, row.custom != nullptr && row.custom->isVisible() ? row.custom : nullptr, row.minHeight};
        for (auto* c : row.controls)
            if (c->isVisible())
                shown.controls.push_back(c);
        if (shown.custom != nullptr || !shown.controls.empty())
            rows.push_back(std::move(shown));
    }
    auto r = getLocalBounds().reduced(12, 8);
    r.removeFromTop(16);
    if (rows.empty())
        return;
    size_t cols = (size_t)minColumns;
    for (auto& row : rows)
        cols = std::max(cols, row.controls.size());
    int cellW = r.getWidth() / (int)cols;
    auto hasKnob = [](const Row& row) {
        return std::any_of(row.controls.begin(), row.controls.end(),
                           [](auto* c) { return c->style == ParamControl::Style::Knob; });
    };
    const int shortH = 52;
    int knobRows = 0, fixed = 0;
    for (auto& row : rows)
        if (row.custom != nullptr)
            fixed += row.minHeight;
        else if (hasKnob(row))
            ++knobRows;
        else
            fixed += shortH;
    int knobH = knobRows ? juce::jmin(118, (r.getHeight() - fixed) / knobRows) : 0;
    int spare = r.getHeight() - fixed - knobH * knobRows;
    // custom rows take the spare height first, then gaps share what is left
    int customs = (int)std::count_if(rows.begin(), rows.end(), [](auto& row) { return row.custom != nullptr; });
    int extra = customs ? spare * 3 / 4 / customs : 0;
    int gap = (spare - extra * customs) / (int)(rows.size() + 1);
    for (auto& row : rows)
    {
        r.removeFromTop(gap);
        if (row.custom != nullptr)
        {
            row.custom->setBounds(r.removeFromTop(row.minHeight + extra).reduced(3, 0));
            continue;
        }
        auto line = r.removeFromTop(hasKnob(row) ? knobH : shortH);
        auto& cs = row.controls;
        if (hasKnob(row))
        {
            line.removeFromLeft((line.getWidth() - cellW * (int)cs.size()) / 2);
            for (auto* c : cs)
                c->setBounds(line.removeFromLeft(cellW).reduced(3, 0));
            continue;
        }
        // without knobs: fixed widths as asked, the rest shared; with no
        // flexible control the leftover becomes even gaps
        int fixedW = 0, flex = 0;
        for (auto* c : cs)
            c->preferredWidth > 0 ? (void)(fixedW += c->preferredWidth) : (void)++flex;
        int left = line.getWidth() - fixedW;
        int flexW = flex ? left / flex : 0, between = flex ? 0 : left / (int)(cs.size() + 1);
        for (auto* c : cs)
        {
            line.removeFromLeft(between);
            c->setBounds(line.removeFromLeft(c->preferredWidth > 0 ? c->preferredWidth : flexW));
        }
    }
}

ParamControl* TrackPage::make(juce::AudioProcessorValueTreeState& st, int track, const char* id,
                              const juce::String& label, ParamControl::Style s, IconSet icons)
{
    controls.push_back(std::make_unique<ParamControl>(st, paramId(track, id), label, s, icons));
    return controls.back().get();   // the section it is put in owns its place
}

LfoScope::LfoScope(CyclesEmuProcessor& p, int t, const char* prefix) : proc(p), track(t)
{
    const char* ids[] = {"wave", "reset", "sync", "mult", "dest", "speed", "depth", "fade", "phase"};
    for (int i = 0; i < 9; ++i)
        raw[i] = p.state().getRawParameterValue(paramId(t, (juce::String(prefix) + ids[i]).toRawUTF8()));
    setInterceptsMouseClicks(false, false);
    startTimerHz(30);
}

LfoScope::State LfoScope::read() const
{
    State s;
    int* f[] = {&s.wave, &s.trig, &s.sync, &s.mult, &s.dest, &s.speed, &s.depth, &s.fade, &s.phase};
    for (int i = 0; i < 9; ++i)
        *f[i] = raw[i] != nullptr ? juce::roundToInt(raw[i]->load()) : 64;   // (LFO 2: no fade)
    s.bpm = proc.currentBpm();
    return s;
}

// Measured (NOTES.md step 11): speed x multiplier / 128 cycles a bar, at
// the tempo when synced and at 120 BPM when free.
double LfoScope::cyclesPerSecond(const State& s) const
{
    double perBar = std::abs(s.speed - 64) * std::ldexp(1.0, s.mult) / 128.0;
    return perBar / (240.0 / (s.sync == 0 ? juce::jmax(1.0, s.bpm) : 120.0));
}

// One bar from the note: one bar of the tempo when synced, of 120 BPM (two
// seconds) when free. At least one cycle is shown, so slow LFOs keep
// their shape; 64 at most, past which the header's rate says the rest.
double LfoScope::windowCycles(const State& s) const
{
    const double perBar = std::abs(s.speed - 64) * std::ldexp(1.0, s.mult) / 128.0;
    return juce::jlimit(1.0, 64.0, perBar);
}

// Fade's time scale is not measured: drawn as |fade| / 64 of the window.
float LfoScope::valueAt(const State& s, double t, double window) const
{
    double run = t;
    if (s.trig == 2)
        run = 0.0;                     // Hold: frozen at the note
    double dir = s.speed >= 64 ? 1.0 : -1.0;
    float v = lfoWave(s.wave, s.phase / 128.0 + dir * run);
    if ((s.trig == 3 || s.trig == 4) && !lfoFromNote(s.wave)
        && t >= lfoOneShotLength(s.trig, s.phase / 128.0, dir < 0))   // One, Half: stopped
        v = lfoStoppedValue(s.wave, s.trig);
    v *= (float)(s.depth - 64) / 64.0f;
    if (int f = s.fade - 64; f != 0)
    {
        double len = std::abs(f) / 64.0 * window;
        v *= (float)(f > 0 ? juce::jmax(0.0, 1.0 - t / len) : juce::jmin(1.0, t / len));
    }
    return juce::jlimit(-1.0f, 1.0f, v);
}

juce::Rectangle<int> LfoScope::plotArea() const { return getLocalBounds().reduced(12, 8).withTrimmedTop(20); }

void LfoScope::timerCallback()
{
    if (!isShowing())
        return;
    if (auto s = read(); s != shown)
    {
        shown = s;
        repaint();
    }
}

void LfoScope::paint(juce::Graphics& g)
{
    const auto s = read();
    auto r = getLocalBounds();
    codecScreen(g, r);
    const auto dim = kPhosphorDim, faint = kPhosphorFaint;

    // header: waveform and rate, destination
    auto head = r.reduced(10, 6).removeFromTop(14);
    juce::String rate;
    double cps = cyclesPerSecond(s);
    auto num = [](double v) {
        auto t = juce::String(v, v < 10 ? 2 : 1);
        return t.contains(".") ? t.trimCharactersAtEnd("0").trimCharactersAtEnd(".") : t;
    };
    if (cps <= 0.0)
        rate = "stopped";
    else if (s.sync == 0)
    {
        double beats = s.bpm / 60.0 / cps;
        rate = beats >= 4 ? num(beats / 4) + " bar" : beats >= 0.25 ? num(beats) + " beat" : num(cps) + " Hz";
    }
    else
        rate = num(cps) + " Hz";
    static const char* shortWave[] = {"Tri", "Sin", "Sqr", "Saw", "Env", "Rmp", "Rnd"};
    // the scope's HUD: "- ZOOM LEVEL - - 100 -"
    drawText(g, "- " + juce::String(shortWave[s.wave]) + " - " + rate + " -", head, juce::Justification::centredLeft,
             kLcdText);

    auto p = plotArea();
    const int cy = p.getCentreY(), half = p.getHeight() / 2 - 1, w = p.getWidth();
    // HUD rules above the plot and corner brackets around it
    g.setColour(dim);
    g.fillRect(p.getX(), p.getY() - 3, w, 1);
    const int b = 6;
    for (auto [x, y, dx, dy] : {std::tuple{p.getX() - 2, p.getY() - 1, 1, 1},
                                std::tuple{p.getRight() + 1, p.getY() - 1, -1, 1},
                                std::tuple{p.getX() - 2, p.getBottom(), 1, -1},
                                std::tuple{p.getRight() + 1, p.getBottom(), -1, -1}})
    {
        g.fillRect(dx > 0 ? x : x - b + 1, y, b, 1);
        g.fillRect(x, dy > 0 ? y : y - b + 1, 1, b);
    }
    const double window = windowCycles(s);
    // dashed zero line; a tick per cycle while they are few enough to count
    g.setColour(faint);
    for (int x = p.getX(); x < p.getRight(); x += 6)
        g.fillRect(x, cy, 3, 1);
    if (window <= 16.0)
        for (int k = 1; k < (int)std::ceil(window); ++k)
            for (int y = p.getY(); y < p.getBottom(); y += 6)
                g.fillRect(p.getX() + (int)(k / window * w), y, 1, 3);
    // the note: a bar at the left
    g.setColour(dim);
    g.fillRect(p.getX(), p.getY(), 2, p.getHeight());

    // Each 2-pixel column draws the curve's range over its slice of time,
    // so a dense LFO reads as a band rather than aliasing.
    auto curve = [&](auto value, juce::Colour c) {
        g.setColour(c);
        int prevLo = -1, prevHi = -1;
        const double per = window / (w / 2.0);                 // cycles per column
        const int sub = juce::jlimit(1, 64, (int)std::ceil(per * 24.0));
        for (int x = 0; x < w; x += 2)
        {
            float lo = 2.0f, hi = -2.0f;
            for (int i = 0; i <= sub; ++i)
            {
                float v = value((x / 2 + (double)i / sub) * per);
                lo = juce::jmin(lo, v), hi = juce::jmax(hi, v);
            }
            int yTop = juce::jlimit(p.getY(), p.getBottom() - 2, (cy - juce::roundToInt(hi * half)) & ~1);
            int yBot = juce::jlimit(p.getY(), p.getBottom() - 2, (cy - juce::roundToInt(lo * half)) & ~1);
            if (prevLo >= 0)   // join to the previous column
                yTop = juce::jmin(yTop, prevHi), yBot = juce::jmax(yBot, prevLo);
            g.fillRect(p.getX() + x, yTop, 2, yBot - yTop + 2);
            prevHi = juce::jlimit(p.getY(), p.getBottom() - 2, (cy - juce::roundToInt(hi * half)) & ~1);
            prevLo = juce::jlimit(p.getY(), p.getBottom() - 2, (cy - juce::roundToInt(lo * half)) & ~1);
        }
    };
    // the plain waveform behind, then the LFO as it plays
    const double dir = s.speed >= 64 ? 1.0 : -1.0;
    curve([&](double t) { return lfoWave(s.wave, s.phase / 128.0 + dir * t); }, faint);
    curve([&](double t) { return valueAt(s, t, window); }, kLcdText);
    // the destination, tagged on the plot like the scope's "FLY"
    {
        const auto tag = "> " + (destName ? destName(s.dest) : kLfoDestNames[s.dest]);
        auto box = juce::Rectangle<int>(0, 0, textWidth(tag) + 8, textHeight() + 4)
                       .withPosition(p.getRight() - textWidth(tag) - 10, p.getBottom() - textHeight() - 6);
        g.setColour(kCodecBg);
        g.fillRect(box);
        g.setColour(dim);
        g.drawRect(box, 1);
        drawText(g, tag, box, juce::Justification::centred, s.dest == 0 ? dim : kLcdText);
    }
    scanlines(g, r.reduced(4));
}

CodecTempo::CodecTempo(CyclesEmuProcessor& p) : proc(p)
{
    tempo = p.state().getParameter("tempo");
    follow = p.state().getParameter("followhost");
    getProperties().set("helpId", "tempo");
    shownBpm = p.currentBpm();
    startTimerHz(20);
}

juce::Rectangle<int> CodecTempo::hostPlate() const
{
    return getLocalBounds().removeFromBottom(18).withSizeKeepingCentre(76, 16);
}
juce::Rectangle<int> CodecTempo::screen() const
{
    return getLocalBounds().withTrimmedTop(22).withTrimmedBottom(24).reduced(28, 0);
}
juce::Rectangle<int> CodecTempo::left() const
{
    auto s = screen();
    return {s.getX() - 24, s.getCentreY() - 9, 18, 18};
}
juce::Rectangle<int> CodecTempo::right() const
{
    auto s = screen();
    return {s.getRight() + 6, s.getCentreY() - 9, 18, 18};
}
bool CodecTempo::following() const { return follow->getValue() >= 0.5f; }

void CodecTempo::step(double delta)
{
    auto v = juce::jlimit(30.0, 300.0, (double)tempo->convertFrom0to1(tempo->getValue()) + delta);
    tempo->beginChangeGesture();
    tempo->setValueNotifyingHost(tempo->convertTo0to1((float)v));
    tempo->endChangeGesture();
}

void CodecTempo::mouseDown(const juce::MouseEvent& e)
{
    auto pos = e.getPosition();
    if (hostPlate().contains(pos))
    {
        follow->beginChangeGesture();
        follow->setValueNotifyingHost(following() ? 0.0f : 1.0f);
        follow->endChangeGesture();
    }
    else if (!following() && left().contains(pos))
        step(e.mods.isShiftDown() ? -0.1 : -1.0);
    else if (!following() && right().contains(pos))
        step(e.mods.isShiftDown() ? 0.1 : 1.0);
    else if (!following() && screen().contains(pos))
    {
        dragging = true;
        dragStart = tempo->convertFrom0to1(tempo->getValue());
        tempo->beginChangeGesture();
    }
    repaint();
}

void CodecTempo::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging)
        return;
    // 1 BPM per 2 pixels, 0.05 with shift
    const double per = e.mods.isShiftDown() ? 0.025 : 0.5;
    auto v = juce::jlimit(30.0, 300.0, dragStart - e.getDistanceFromDragStartY() * per);
    tempo->setValueNotifyingHost(tempo->convertTo0to1((float)v));
}

void CodecTempo::mouseUp(const juce::MouseEvent&)
{
    if (dragging)
        tempo->endChangeGesture();
    dragging = false;
}

void CodecTempo::timerCallback()
{
    // the meter falls like the Codec's bars: fast up, slow down
    const float peak = juce::jmin(1.0f, proc.outputPeak());
    const float next = peak > level ? peak : level * 0.85f;
    const double bpm = proc.currentBpm();
    if (std::abs(next - level) > 0.004f || std::abs(bpm - shownBpm) > 0.004)
    {
        level = next;
        shownBpm = bpm;
        repaint();
    }
}

void CodecTempo::paint(juce::Graphics& g)
{
    auto top = getLocalBounds().removeFromTop(16).withSizeKeepingCentre(76, 14);
    labelPlate(g, top, "Tempo");
    auto in = codecScreen(g, screen());
    // signal bars: the output level, on a dB scale (-48..0 dB)
    auto bars = in.removeFromLeft(44).reduced(6, 8);
    const float db = juce::Decibels::gainToDecibels(level, -48.0f);
    signalBars(g, bars, 8, (db + 48.0f) / 48.0f, kPhosphor, kPhosphorFaint);
    g.setColour(kPhosphorFaint);
    g.fillRect(in.getX(), in.getY() + 4, 1, in.getHeight() - 8);
    sevenSeg(g, juce::String(shownBpm, 2).paddedLeft(' ', 6), in.reduced(4, 0), juce::jmin(30, in.getHeight() - 12),
             kPhosphor, kPhosphorFaint);
    scanlines(g, screen().reduced(4));
    // arrows: the Codec's frequency buttons, dim while the host sets the tempo
    for (auto [r, dir] : {std::pair{left(), -1}, std::pair{right(), 1}})
    {
        g.setColour(following() ? kGunLight : kGunDark);
        for (int i = 0; i < 7; ++i)   // a pixel triangle
        {
            int hgt = 2 * (dir < 0 ? i : 6 - i) + 2;
            g.fillRect(r.getCentreX() - 4 + i, r.getCentreY() - hgt / 2, 1, hgt);
        }
    }
    labelPlate(g, hostPlate(), "Host", following());
}

FilterScope::FilterScope(CyclesEmuProcessor& p, int t)
{
    const char* ids[] = {"fcut", "freso", "fenv", "fdec", "ftype", "fextra"};
    for (int i = 0; i < 6; ++i)
        raw[i] = p.state().getRawParameterValue(paramId(t, ids[i]));
    getProperties().set("helpId", paramId(t, "fcut"));
    setInterceptsMouseClicks(false, false);
    startTimerHz(30);
}

void FilterScope::timerCallback()
{
    if (!isShowing())
        return;
    bool changed = false;
    for (int i = 0; i < 6; ++i)
        if (int v = juce::roundToInt(raw[i]->load()); v != shown[i])
            shown[i] = v, changed = true;
    if (changed)
        repaint();
}

void FilterScope::paint(juce::Graphics& g)
{
    // The cutoff law from Modded-Cycles' notes/47: about 2 semitones a
    // step, low-pass L64 5 Hz .. L1 20 kHz, high-pass H1 5 Hz .. H63 19 kHz;
    // Q 0.5 .. 50 over the resonance knob. Approximate: the exact table
    // is in the tweak's code.
    const int cut = juce::roundToInt(raw[0]->load()), res = juce::roundToInt(raw[1]->load());
    // the Digitone II's types: the engine's own laws (mc_filter_db)
    const int type = juce::roundToInt(raw[4]->load()), extra = juce::roundToInt(raw[5]->load());
    const bool dn2 = type > 0, comb = type == 5 || type == 6;   // (the menu's numbers)
    const bool off = !dn2 && cut == 64, lp = cut < 64;
    const double fc = dn2 ? 5.0 * std::exp(0.065606 * cut)
                    : lp ? 20000.0 * std::pow(5.0 / 20000.0, (63 - cut) / 63.0)
                         : 5.0 * std::pow(19000.0 / 5.0, (cut - 65) / 62.0);
    const double q = 0.5 * std::pow(100.0, res / 127.0);

    // the curve only, no numbers: moves are made by ear
    auto r = getLocalBounds();
    auto in = codecScreen(g, r);
    auto p = in.reduced(8, 6);
    // log frequency 20 Hz .. 20 kHz; -30 .. +24 dB
    auto xOf = [&](double hz) { return p.getX() + (int)(std::log(hz / 20.0) / std::log(1000.0) * p.getWidth()); };
    auto yOf = [&](double db) {
        return p.getY() + (int)((24.0 - juce::jlimit(-30.0, 24.0, db)) / 54.0 * (p.getHeight() - 2));
    };
    g.setColour(kPhosphorFaint);
    for (double hz : {100.0, 1000.0, 10000.0})          // decade lines
        for (int y = p.getY(); y < p.getBottom(); y += 4)
            g.fillRect(xOf(hz), y, 1, 2);
    for (int x = p.getX(); x < p.getRight(); x += 6)    // 0 dB
        g.fillRect(x, yOf(0.0), 3, 1);
    g.setColour(kPhosphor);
    int prev = -1;
    for (int x = 0; x < p.getWidth(); x += 2)
    {
        const double hz = 20.0 * std::pow(1000.0, (double)x / p.getWidth());
        double db = 0.0;
        if (dn2)
            db = mc_filter_db(filterEngineType(type), cut, res, extra, hz);
        else if (!off)
        {
            // the 2-pole state-variable filter's magnitude
            const double u = hz / fc, den = std::sqrt((1 - u * u) * (1 - u * u) + (u / q) * (u / q));
            db = 20.0 * std::log10((lp ? 1.0 : u * u) / den);
        }
        const int y = yOf(db) & ~1;
        g.fillRect(p.getX() + x, prev < 0 ? y : juce::jmin(prev, y), 2, prev < 0 ? 2 : std::abs(prev - y) + 2);
        prev = y;
    }
    if (!off && !comb && type != 3)   // the cutoff's mark (a comb's or a bell's is plain to see)
    {
        g.setColour(kPhosphorDim);
        g.fillRect(xOf(juce::jlimit(20.0, 20000.0, fc)), p.getY(), 1, p.getHeight());
    }
    scanlines(g, in);
}

ModPanel::ModPanel(juce::AudioProcessorValueTreeState& st, int t)
{
    using S = ParamControl::Style;
    auto add = [&](const juce::String& id, const juce::String& label, S style) {
        controls.push_back(std::make_unique<ParamControl>(st, paramId(t, id.toRawUTF8()), label, style));
        addAndMakeVisible(*controls.back());
    };
    for (const char* kind : {"vm", "km"})
        for (int i = 1; i <= 4; ++i)
        {
            add(juce::String(kind) + juce::String(i) + "dst", "Dest " + juce::String(i), S::Choice);
            add(juce::String(kind) + juce::String(i) + "dep", "Depth", S::Knob);
        }
    add("velvol", "Vel > Vol", S::Choice);
    add("fkey", "Filt Key", S::Knob);
    setWantsKeyboardFocus(true);   // Esc closes it
}

bool ModPanel::keyPressed(const juce::KeyPress& k)
{
    if (k != juce::KeyPress::escapeKey || !onClose)
        return false;
    onClose();
    return true;
}

void ModPanel::setDestText(int entry, const juce::String& text)
{
    for (int i = 0; i < 16; i += 2)
        controls[(size_t)i]->setItemText(entry, text);
}

void ModPanel::paint(juce::Graphics& g)
{
    g.fillAll(kFace);
    groupBox(g, velArea, "Velocity Mod", accentFor(*this));
    groupBox(g, keyArea, "Key Tracking", accentFor(*this));
    groupBox(g, otherArea, "Sound Setup", accentFor(*this));
}

void ModPanel::resized()
{
    auto r = getLocalBounds();
    otherArea = r.removeFromRight(200).withTrimmedLeft(6);
    velArea = r.removeFromTop(r.getHeight() / 2).withTrimmedBottom(3);
    keyArea = r.withTrimmedTop(3);
    // each block: four columns of a destination over its depth
    for (int block = 0; block < 2; ++block)
    {
        auto a = (block ? keyArea : velArea).reduced(12, 8).withTrimmedTop(16);
        const int w = a.getWidth() / 4;
        for (int i = 0; i < 4; ++i)
        {
            auto col = a.removeFromLeft(w).reduced(6, 0);
            controls[(size_t)(block * 8 + 2 * i)]->setBounds(col.removeFromTop(52));
            controls[(size_t)(block * 8 + 2 * i + 1)]->setBounds(col.withSizeKeepingCentre(col.getWidth(), juce::jmin(col.getHeight(), 112)));
        }
    }
    auto o = otherArea.reduced(12, 8).withTrimmedTop(16);
    controls[16]->setBounds(o.removeFromTop(52));
    controls[17]->setBounds(o.removeFromTop(juce::jmin(112, o.getHeight())));
}

// SP TWINSHOT's page 1 (the macro knobs as Sample 1, Filter 1, Mix) or page
// 2 (Sample 2, and Contour as Tune 2: each sample's knobs on its page); other
// machines: Color .. Contour
void TrackPage::showTwinPage()
{
    const bool p2 = twinMachine && twinOnPage2;
    for (int i = 0; i < 3; ++i)   // Color .. Sweep
        macroKnobs[i]->setVisible(!p2);
    macroKnobs[3]->setVisible(!twinMachine || p2);   // Contour, Tune 2 on Twinshot's page 2
    twinSample2->setVisible(p2);
    twinPage.setVisible(twinMachine);
    twinPage.setButtonText(p2 ? "Page 2" : "Page 1");
    sound.resized();
}

void TrackPage::refreshTwinNames()
{
    machineAtt->sendInitialUpdate();   // Sample 1's text, if Twinshot
    twinSample2->setValueText([this](double x) { return processor->twinSamples().describe(juce::roundToInt(x)); });
}

void TrackPage::showAmpView(bool on)
{
    ampView = on;
    for (auto* c : filterControls)
        c->setVisible(!on);
    for (auto* c : ampControls)
        c->setVisible(on);
    ampPage.setButtonText(on ? "Filter" : "Amp");
    filter.setTitle(on ? "AMP" : "FILTER");
    filter.resized();
}

void TrackPage::showLfo2(bool on)
{
    lfo2View = on;
    for (auto* c : lfo1Controls)
        c->setVisible(!on);
    scope.setVisible(!on);
    for (auto* c : lfo2Controls)
        c->setVisible(on);
    scope2.setVisible(on);
    lfoPage.setButtonText(on ? "LFO 1" : "LFO 2");
    lfo.setTitle(on ? "LFO 2" : "LFO");
    lfo.resized();
}

void TrackPage::showSetup(bool on)
{
    modPanel.setVisible(on);
    // one button opens and closes it, in the same place: over the panel too
    setupButton.setButtonText(on ? "Close" : "Setup");
    setupButton.setToggleState(on, juce::dontSendNotification);
    if (on)
    {
        modPanel.toFront(false);
        modPanel.grabKeyboardFocus();
    }
    setupButton.toFront(false);
}

TrackPage::TrackPage(CyclesEmuProcessor& proc, int t)
    : scope(proc, t), scope2(proc, t, "l2"), filterScope(proc, t), modPanel(proc.state(), t)
{
    using S = ParamControl::Style;
    auto& st = proc.state();
    addAndMakeVisible(sound);
    addAndMakeVisible(lfo);
    auto* punch = make(st, t, "punch", "Punch", S::Toggle);
    auto* machineControl = make(st, t, "machine", "Machine", S::Icons, IconSet::Machine);
    // a machine picked here starts the amp envelope's MODE as the Syntakt
    // does: ADSR for the SY machines, AHD for the others
    machineControl->onPicked = [&st, t](int m) {
        setPlain(st, paramId(t, "amode"), (float)CyclesEmuProcessor::ampDefaultFor("amode", m));
    };
    sound.addRow({machineControl, punch,
                  make(st, t, "gate", "Gate", S::Toggle), make(st, t, "mute", "Mute", S::Toggle)});
    auto* color = make(st, t, "color", "Color", S::Knob);
    auto* shape = make(st, t, "shape", "Shape", S::Knob);
    auto* sweep = make(st, t, "sweep", "Sweep", S::Knob);
    auto* contour = make(st, t, "contour", "Contour", S::Knob);
    // SP TWINSHOT's page 2 shares this row: its knobs hidden until then
    processor = &proc;
    twinSample2 = make(st, t, "twsamp2", "Smp 2", S::Knob);
    twinSample2->setValueText([this](double x) { return processor->twinSamples().describe(juce::roundToInt(x)); });
    sound.addRow({make(st, t, "pitch", "Pitch", S::Knob), make(st, t, "decay", "Decay", S::Knob), color, shape,
                  sweep, twinSample2, contour});   // (page 2: Sample 2 before Tune 2)
    twinSample2->setVisible(false);
    sound.minColumns = 6;   // the knobs one size on either Twinshot page
    twinPage.onClick = [this] {
        twinOnPage2 = !twinOnPage2;
        showTwinPage();
    };
    twinPage.getProperties().set("helpId", paramId(t, "twsamp2"));
    addChildComponent(twinPage);
    macroKnobs[0] = color, macroKnobs[1] = shape, macroKnobs[2] = sweep, macroKnobs[3] = contour,
    macroKnobs[4] = punch;
    // Modded-Cycles' guide: what each Syntakt engine maps the knobs to
    // (full names in Help.cpp), shortened to fit the pixel font
    machineAtt = std::make_unique<juce::ParameterAttachment>(
        *st.getParameter(paramId(t, "machine")),
        [this](float v) {
            static const char* labels[7][5] = {
                {"Color", "Shape", "Sweep", "Contour", "Punch"},   // the six originals
                {"Inharm", "Frq Cpx", "P.Sweep", "Mod Env", "Punch"},
                {"Body Ch", "Balance", "Spc Crn", "Body Ev", "Punch"},
                {"Form", "Impact", "Bright", "Prt Dec", "Punch"},
                {"Detune", "Balance", "Rate Rx", "Wave", "Bits"},
                {"Nse Mod", "Det Anm", "Detune", "Osc Mix", "Sub"},
                {"Smp 1", "Flt 1", "Mix", "Tune 2", "-"},   // SP TWINSHOT (Punch unused)
            };
            // and the LFO's destinations for them (entries 2-6: color, shape,
            // sweep, contour, punch), longer names where the list has room
            static const char* dests[7][5] = {
                {"Color", "Shape", "Sweep", "Contour", "Punch"},
                {"Inharm", "Freq Complex", "Pitch Sweep", "Mod Envelope", "Punch"},
                {"Body Char", "Balance", "Spacing Crnch", "Body Envelope", "Punch"},
                {"Form", "Impact", "Bright", "Partial Decay", "Punch"},
                {"Detune", "Balance", "Rate Redux", "Waveform", "Bit Redux"},
                {"Noise Mod", "Detune Anim", "Detune", "Osc Mix", "Sub-octave"},
                {"Sample 1", "Filter 1", "Mix", "Tune 2", "Punch"},
            };
            int m = juce::roundToInt(v), row = m < 6 ? 0 : m - 5;
            for (int i = 0; i < 5; ++i)
            {
                macroKnobs[i]->setLabel(labels[row][i]);
                if (destControl != nullptr)
                    destControl->setItemText(2 + i, dests[row][i]);
                if (dest2Control != nullptr && i < 4)   // LFO 2: Color .. Contour are entries 2-5 too
                    dest2Control->setItemText(2 + i, dests[row][i]);
                if (i < 4)   // the modulation's lists: Color..Contour are entries 2-5 (short names)
                    modPanel.setDestText(2 + i, labels[row][i]);
            }
            // SP TWINSHOT: its pages, the sample names, no Punch
            const bool twinOn = m == 11;
            macroKnobs[4]->setEnabled(!twinOn);
            twinMachine = twinOn;
            if (!twinOn)
                twinOnPage2 = false;
            showTwinPage();
            if (twinOn)
                macroKnobs[0]->setValueText([this](double x) { return processor->twinSamples().describe(juce::roundToInt(x)); });
            else
                macroKnobs[0]->setValueText([](double x) { return juce::String(juce::roundToInt(x)); });
            scope.repaint();   // its destination tag
        },
        nullptr);
    sound.addRow({make(st, t, "voldist", "Vol+Dst", S::Knob), make(st, t, "pan", "Pan", S::Knob),
                  make(st, t, "delaysend", "Delay", S::Knob), make(st, t, "reverbsend", "Reverb", S::Knob),
                  make(st, t, "level", "Level", S::Knob)});

    auto* wave1 = make(st, t, "lfowave", "Wave", S::Icons, IconSet::Wave);
    auto* trig1 = make(st, t, "lforeset", "Trig", S::Icons, IconSet::Trig);
    lfo.addRow({wave1, trig1});
    auto* dest = make(st, t, "lfodest", "Destination", S::Choice);
    destControl = dest;
    scope.destName = [this](int i) { return destControl->itemText(i); };
    machineAtt->sendInitialUpdate();   // now that the destination list exists too
    auto* mult = make(st, t, "lfomult", "Mult", S::Choice);
    mult->preferredWidth = 84;
    auto* sync1 = make(st, t, "lfosync", "", S::Icons, IconSet::Clock);
    lfo.addRow({sync1, mult, dest});
    lfo.addCustomRow(&scope, 84);
    auto* speed1 = make(st, t, "lfospeed", "Speed", S::Knob);
    auto* depth1 = make(st, t, "lfodepth", "Depth", S::Knob);
    auto* fade1 = make(st, t, "lfofade", "Fade", S::Knob);
    auto* phase1 = make(st, t, "lfophase", "Phase", S::Knob);
    lfo.addRow({speed1, depth1, fade1, phase1});
    lfo1Controls = {wave1, trig1, sync1, mult, dest, speed1, depth1, fade1, phase1};
    // LFO 2, the plugin's: the same layout, no fade (the section's other page)
    auto* wave2 = make(st, t, "l2wave", "Wave", S::Icons, IconSet::Wave);
    auto* trig2 = make(st, t, "l2reset", "Trig", S::Icons, IconSet::Trig);
    lfo.addRow({wave2, trig2});
    dest2Control = make(st, t, "l2dest", "Destination", S::Choice);
    scope2.destName = [this](int i) { return dest2Control->itemText(i); };
    auto* mult2 = make(st, t, "l2mult", "Mult", S::Choice);
    mult2->preferredWidth = 84;
    auto* sync2 = make(st, t, "l2sync", "", S::Icons, IconSet::Clock);
    lfo.addRow({sync2, mult2, dest2Control});
    lfo.addCustomRow(&scope2, 84);
    auto* speed2 = make(st, t, "l2speed", "Speed", S::Knob);
    auto* depth2 = make(st, t, "l2depth", "Depth", S::Knob);
    auto* phase2 = make(st, t, "l2phase", "Phase", S::Knob);
    lfo.addRow({speed2, depth2, phase2});
    lfo2Controls = {wave2, trig2, sync2, mult2, dest2Control, speed2, depth2, phase2};
    for (auto* c : lfo2Controls)
        c->setVisible(false);
    scope2.setVisible(false);
    machineAtt->sendInitialUpdate();   // LFO 2's destination names too
    lfoPage.onClick = [this] { showLfo2(!lfo2View); };
    lfoPage.getProperties().set("helpId", paramId(t, "l2dest"));
    addAndMakeVisible(lfoPage);
    addAndMakeVisible(filter);
    auto* ftype = make(st, t, "ftype", "Type", S::Choice);
    ftype->preferredWidth = 240;
    filter.addRow({ftype});
    filterCut = make(st, t, "fcut", "Cutoff", S::Knob);
    filterRes = make(st, t, "freso", "Reso", S::Knob);
    filterExtra = make(st, t, "fextra", "Morph", S::Knob);
    auto* fenv = make(st, t, "fenv", "Env", S::Knob);
    auto* fdec = make(st, t, "fdec", "Decay", S::Knob);
    filter.addRow({filterCut, filterRes, filterExtra, fenv, fdec});
    filterControls = {ftype, filterCut, filterRes, filterExtra, fenv, fdec};
    // the Syntakt amp envelope: the section's other page
    auto* ampOn = make(st, t, "amp", "On", S::Switch);
    auto* amode = make(st, t, "amode", "Mode", S::Choice);
    amode->preferredWidth = 120;
    auto* aenr = make(st, t, "aenr", "Reset", S::Switch);
    filter.addRow({ampOn, amode, aenr});
    auto* aatk = make(st, t, "aatk", "Atk", S::Knob);   // the Syntakt's names
    auto* ahold = make(st, t, "ahold", "Hold", S::Knob);
    auto* adec = make(st, t, "adec", "Dec", S::Knob);
    auto* asus = make(st, t, "asus", "Sus", S::Knob);
    auto* arel = make(st, t, "arel", "Rel", S::Knob);
    filter.addRow({aatk, ahold, adec, asus, arel});
    ampControls = {ampOn, amode, aenr, aatk, ahold, adec, asus, arel};
    // as on the Syntakt: HOLD only in AHD (ADSR has no hold stage, measured),
    // SUS and REL only in ADSR
    ampModeAtt = std::make_unique<juce::ParameterAttachment>(
        *st.getParameter(paramId(t, "amode")),
        [ahold, asus, arel](float v) {
            const bool adsr = v >= 0.5f;
            for (auto [c, on] : {std::pair{ahold, !adsr}, std::pair{asus, adsr}, std::pair{arel, adsr}})
            {
                c->setEnabled(on);
                c->setAlpha(on ? 1.0f : 0.35f);
            }
        },
        nullptr);
    ampModeAtt->sendInitialUpdate();
    for (auto* c : ampControls)
        c->setVisible(false);
    ampPage.onClick = [this] { showAmpView(!ampView); };
    ampPage.getProperties().set("helpId", paramId(t, "amp"));
    ampPage.setEnabled(proc.ampAvailable());   // needs the Syntakt's DSP image in the engine state
    addAndMakeVisible(ampPage);
    // a type picked here starts from its own defaults (the DN2's: open,
    // flat or dry), as on the hardware; a preset brings its own settings
    ftype->onPicked = [&st, t](int type) {
        const auto& info = kFilterTypes[juce::jlimit(0, 6, type)];
        setPlain(st, paramId(t, "fcut"), (float)info.defCut);
        setPlain(st, paramId(t, "freso"), (float)info.defRes);
        setPlain(st, paramId(t, "fextra"), (float)info.defExtra);
    };
    filterTypeAtt = std::make_unique<juce::ParameterAttachment>(
        *st.getParameter(paramId(t, "ftype")),
        [this](float v) {
            const int type = juce::jlimit(0, 6, juce::roundToInt(v));
            const auto& info = kFilterTypes[type];
            filterCut->setLabel(info.cut);
            filterRes->setLabel(info.res);
            filterExtra->setLabel(*info.extra ? info.extra : "-");
            filterExtra->setEnabled(*info.extra != 0);
            // values as the type means them
            if (type == 0)
                filterCut->setValueText([](double x) {
                    const int v = juce::roundToInt(x);
                    return v == 64 ? juce::String("OFF") : v < 64 ? "L" + juce::String(64 - v) : "H" + juce::String(v - 64);
                });
            else if (type == 5 || type == 6)   // a comb's pitch: a note name
                filterCut->setValueText([](double x) {
                    return juce::MidiMessage::getMidiNoteName(juce::roundToInt(x) - 12, true, true, 4);
                });
            else
                filterCut->setValueText([](double x) {
                    const double hz = 5.0 * std::exp(0.065606 * x);
                    return hz < 1000.0 ? juce::String(juce::roundToInt(hz)) + "HZ"
                                       : juce::String(hz / 1000.0, hz < 10000.0 ? 2 : 1) + "K";
                });
            if (type == 3)
                filterRes->setValueText([](double x) {
                    const double db = (x - 64.0) * 3.0 / 16.0;
                    return (db > 0 ? "+" : "") + juce::String(db, 1) + "DB";
                });
            else
                filterRes->setValueText([](double x) { return juce::String(juce::roundToInt(x)); });
            if (type == 4)   // Legacy
                filterExtra->setValueText([](double x) { return juce::String(x < 63.5 ? "LP" : "HP"); });
            else
                filterExtra->setValueText([](double x) { return juce::String(juce::roundToInt(x)); });
            filterScope.repaint();
        },
        nullptr);
    filterTypeAtt->sendInitialUpdate();
    addAndMakeVisible(filterScope);
    // legato and portamento; the rest of the Syntakt's setup in the Track menu
    addAndMakeVisible(glide);
    glide.addRow({make(st, t, "port", "Port", S::Switch), make(st, t, "legato", "Legato", S::Switch)});
    glide.addRow({make(st, t, "ptim", "Time", S::Knob), make(st, t, "pamount", "Amount", S::Knob)});
    glide.minColumns = 2;
    // the SOUND SETUP panel (velocity and key modulation), over the page
    addChildComponent(modPanel);
    modPanel.onClose = [this] { showSetup(false); };
    setupButton.onClick = [this] { showSetup(!modPanel.isVisible()); };
    setupButton.getProperties().set("helpId", paramId(t, "soundsetup"));
    addAndMakeVisible(setupButton);
    for (auto [section, which] : {std::pair{&sound, RandomSection::Sound}, std::pair{&lfo, RandomSection::Lfo},
                                  std::pair{&filter, RandomSection::Filter}})
        section->enableDice([this, &st, t, which = which](bool variation) {
            if (beforeChange)
                beforeChange(variation ? "Vary" : "Randomise");
            randomise(st, t, which, rng, variation);
        });
}

void TrackPage::resized()
{
    auto r = getLocalBounds();
    // the filter page along the bottom: its knobs, and the response beside
    // and the glide's to the right
    auto bottom = r.removeFromBottom(204).withTrimmedTop(6);
    const int w = bottom.getWidth();
    glide.setBounds(bottom.removeFromRight(w * 24 / 100).withTrimmedLeft(6));
    filter.setBounds(bottom.removeFromLeft(w * 50 / 100).withTrimmedRight(6));
    filterScope.setBounds(bottom.withTrimmedLeft(6).withTrimmedRight(6).withTrimmedTop(8));
    sound.setBounds(r.removeFromLeft(r.getWidth() * 62 / 100).withTrimmedRight(6));
    lfo.setBounds(r.withTrimmedLeft(6));
    // SETUP in the SOUND header, left of its dice
    setupButton.setBounds(sound.getRight() - 36 - 76, sound.getY() - 1, 70, 20);
    setupButton.toFront(false);
    twinPage.setBounds(setupButton.getX() - 78, sound.getY() - 1, 72, 20);   // SP TWINSHOT only
    ampPage.setBounds(filter.getRight() - 36 - 76, filter.getY() - 1, 70, 20);   // FILTER / AMP, left of the dice
    lfoPage.setBounds(lfo.getRight() - 36 - 76, lfo.getY() - 1, 70, 20);         // LFO 1 / LFO 2
    lfoPage.toFront(false);
    ampPage.toFront(false);
    twinPage.toFront(false);
    modPanel.setBounds(getLocalBounds());
    if (modPanel.isVisible())   // over the page, and its button over it
    {
        modPanel.toFront(false);
        setupButton.toFront(false);
    }
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
    auto choice = [&](const char* id) {
        controls.push_back(std::make_unique<ParamControl>(st, paramId(kTracks, id), "Type", S::Choice));
        return controls.back().get();
    };
    dTime = mk("delaytime", "Time");
    dFb = mk("delayfeedback", "Fdbk");
    dX = mk("delayx", "Random");
    rSize = mk("reverbsize", "Size");
    rTone = mk("reverbtone", "Tone");
    rX = mk("reverbx", "Space");
    auto* dType = choice("delaytype");
    auto* rType = choice("reverbtype");
    dType->preferredWidth = rType->preferredWidth = 180;
    delay.addRow({dType});
    delay.addRow({dTime, dFb, dX});
    reverb.addRow({rType});
    reverb.addRow({rSize, rTone, rX});
    delay.minColumns = reverb.minColumns = 3;
    // each engine's knobs (FxEngines.cpp): labels, and values as they mean them
    delayTypeAtt = std::make_unique<juce::ParameterAttachment>(
        *st.getParameter(paramId(kTracks, "delaytype")),
        [this](float v) {
            const int type = juce::roundToInt(v);
            dTime->setLabel(type == 1 ? "Wand" : "Time");
            dX->setLabel(type == 1 ? "Random" : type == 2 ? "Tone" : "-");
            dX->setEnabled(type != 0);
            if (type == 1)   // the Wand: down a clean echo, up melting into reverb
                dTime->setValueText([](double x) {
                    const int w = juce::roundToInt(x) - 64;
                    return w == 0 ? juce::String("OFF") : (w < 0 ? "DN " : "UP ") + juce::String(std::abs(w));
                });
            else   // a note length, as the Cycles' own
                dTime->setValueText([](double x) {
                    const int v = juce::roundToInt(x);
                    static const std::pair<int, const char*> names[] = {
                        {2, "1/64"}, {4, "1/32"}, {6, "1/32."}, {8, "1/16"}, {12, "1/16."}, {16, "1/8"},
                        {24, "1/8."}, {32, "1/4"}, {48, "1/4."}, {64, "1/2"}, {96, "1/2."}, {127, "1 BAR"}};
                    for (auto [at, name] : names)
                        if (v == at)
                            return juce::String(name);
                    return juce::String(v);
                });
        },
        nullptr);
    delayTypeAtt->sendInitialUpdate();
    reverbTypeAtt = std::make_unique<juce::ParameterAttachment>(
        *st.getParameter(paramId(kTracks, "reverbtype")),
        [this](float v) {
            const int type = juce::roundToInt(v);
            static const char* sizes[] = {"Size", "Decay", "Time", "Decay"};
            static const char* tones[] = {"Tone", "Hi Cut", "Lo-Pass", "Model"};
            static const char* extras[] = {"-", "Space", "Diffuse", "Level"};
            rSize->setLabel(sizes[type & 3]);
            rTone->setLabel(tones[type & 3]);
            rX->setLabel(extras[type & 3]);
            rX->setEnabled(type != 0);
            if (type == 3)   // PSX: the SPU's presets
                rTone->setValueText([](double x) {
                    static const char* models[] = {"ROOM", "STUDIO S", "STUDIO M", "STUDIO L", "HALL", "SPACE ECHO"};
                    return juce::String(models[juce::jlimit(0, 5, juce::roundToInt(x) * 6 / 128)]);
                });
            else
                rTone->setValueText([](double x) { return juce::String(juce::roundToInt(x)); });
        },
        nullptr);
    reverbTypeAtt->sendInitialUpdate();
    input.minColumns = 3;
    if (st.getParameter("fxdelay") != nullptr)   // CyclesFX
    {
        hasInput = true;
        auto mkGlobal = [&](const char* id, const juce::String& label) {
            controls.push_back(std::make_unique<ParamControl>(st, id, label, S::Knob));
            return controls.back().get();
        };
        input.addRow({mkGlobal("fxdelay", "Delay"), mkGlobal("fxreverb", "Reverb"), mkGlobal("fxmix", "Mix")});
        addAndMakeVisible(input);
    }
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
                "Delay and reverb: Cycles is the Model:Cycles' own. The others are open-source engines: CHOMPI "
                "TEMPO's dual delay and reverb, a tape echo, CloudSeed, the PlayStation's SPU reverb.",
                r.withTrimmedLeft(14).withTrimmedTop(2), kText);
}

void FxPage::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop(236);
    if (hasInput)   // the input's sends and Mix first, then the two effects
        input.setBounds(top.removeFromLeft(top.getWidth() * 36 / 100).withTrimmedRight(6));
    delay.setBounds(top.removeFromLeft(top.getWidth() / 2).withTrimmedRight(6));
    reverb.setBounds(top.withTrimmedLeft(6));
    noteArea = r.withTrimmedTop(24).removeFromTop(48).reduced(12, 0);
}

MixerPage::MixerPage(CyclesEmuProcessor& p) : proc(p)
{
    using S = ParamControl::Style;
    auto& st = p.state();
    for (int t = 0; t < kTracks; ++t)
        for (auto [id, label, style] : {std::tuple{"level", "Level", S::Knob}, std::tuple{"pan", "Pan", S::Knob},
                                        std::tuple{"delaysend", "Delay", S::Knob}, std::tuple{"reverbsend", "Reverb", S::Knob},
                                        std::tuple{"mute", "Mute", S::Switch}, std::tuple{"solo", "Solo", S::Switch}})
        {
            controls.push_back(std::make_unique<ParamControl>(st, paramId(t, id), label, style));
            setAccent(*controls.back(), kAccents[t]);
            addAndMakeVisible(*controls.back());
        }
    startTimerHz(24);
}

juce::Rectangle<int> MixerPage::strip(int t) const
{
    const int w = getWidth() / kTracks;
    return {t * w, 0, w, getHeight()};
}

juce::Rectangle<int> MixerPage::meter(int t) const
{
    return strip(t).reduced(10, 0).withTrimmedTop(26).withHeight(96);
}

void MixerPage::resized()
{
    for (int t = 0; t < kTracks; ++t)
    {
        auto r = strip(t).reduced(4, 0).withTrimmedTop(26 + 96 + 6);
        auto toggles = r.removeFromBottom(30);
        const int kh = r.getHeight() / 4;
        for (int k = 0; k < 4; ++k)
            controls[(size_t)(t * 6 + k)]->setBounds(r.removeFromTop(kh));
        controls[(size_t)(t * 6 + 4)]->setBounds(toggles.removeFromLeft(toggles.getWidth() / 2));
        controls[(size_t)(t * 6 + 5)]->setBounds(toggles);
    }
}

void MixerPage::timerCallback()
{
    if (!isShowing())
        return;
    for (int t = 0; t < kTracks; ++t)   // fast up, slow down, like the Codec's bars
    {
        const float p = proc.takeTrackPeak(t);
        shown[t] = p > shown[t] ? p : shown[t] * 0.85f;
    }
    repaint();
}

void MixerPage::paint(juce::Graphics& g)
{
    for (int t = 0; t < kTracks; ++t)
    {
        auto s = strip(t);
        // the strip's name, with its colour chip; a groove between strips
        auto head = s.withHeight(20).reduced(10, 0);
        g.setColour(kAccents[t]);
        g.fillRect(head.getX(), head.getY() + 5, 8, 8);
        drawText(g, "Trk " + juce::String(t + 1), head.withTrimmedLeft(14), juce::Justification::centredLeft, kText);   // (eight strips: short)
        if (t > 0)
        {
            g.setColour(kShadow);
            g.fillRect(s.getX(), s.getY() + 4, 1, s.getHeight() - 8);
            g.setColour(kLight);
            g.fillRect(s.getX() + 1, s.getY() + 4, 1, s.getHeight() - 8);
        }
        // the meter: a Codec screen of segments, -48..0 dB, bottom up
        auto in = codecScreen(g, meter(t));
        const bool audible = proc.trackAudible(t);
        const float db = juce::Decibels::gainToDecibels(shown[t], -48.0f);
        const int segs = 12, lit = juce::roundToInt((db + 48.0f) / 48.0f * segs);
        auto bars = in.reduced(8, 6);
        const int sh = bars.getHeight() / segs;
        for (int i = 0; i < segs; ++i)
        {
            const bool on = i < lit;
            g.setColour(on ? (audible ? kPhosphor : kPhosphorDim) : kPhosphorFaint);
            g.fillRect(bars.getX(), bars.getBottom() - (i + 1) * sh + 1, bars.getWidth(), sh - 2);
        }
        if (!audible)   // silenced: say why the meter is dim
            drawText(g, "Off", in.withTrimmedTop(2).withHeight(14), juce::Justification::centred, kPhosphor);
        scanlines(g, in);
    }
}

juce::Rectangle<int> MenuStrip::itemArea(int i) const
{
    int x = 4;
    for (int k = 0; k < i; ++k)
        x += textWidth(menus[(size_t)k].name) + 20;
    return {x, 0, textWidth(menus[(size_t)i].name) + 20, getHeight()};
}

void MenuStrip::paint(juce::Graphics& g)
{
    g.fillAll(kFace);
    for (int i = 0; i < (int)menus.size(); ++i)
    {
        auto r = itemArea(i);
        if (i == open)
        {
            g.setColour(kSelect);
            g.fillRect(r.reduced(0, 1));
        }
        // Win95 underlines the access letter
        const auto c = i == open ? kTitleText : kText;
        drawText(g, menus[(size_t)i].name, r, juce::Justification::centred, c);
        g.setColour(c);
        g.fillRect(r.getCentreX() - textWidth(menus[(size_t)i].name) / 2, r.getCentreY() + 8, 10, 1);
    }
    g.setColour(kShadow);
    g.fillRect(0, getHeight() - 2, getWidth(), 1);
    g.setColour(kLight);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void MenuStrip::mouseDown(const juce::MouseEvent& e)
{
    for (int i = 0; i < (int)menus.size(); ++i)
        if (itemArea(i).contains(e.getPosition()))
        {
            open = i;
            repaint();
            auto area = localAreaToGlobal(itemArea(i));
            menus[(size_t)i].build().showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(area),
                                                   [safe = juce::Component::SafePointer<MenuStrip>(this)](int) {
                                                       if (safe)
                                                       {
                                                           safe->open = -1;
                                                           safe->repaint();
                                                       }
                                                   });
            return;
        }
}

// SP TWINSHOT's samples: a folder of up to 64 WAVs (in name order), from the
// Kit menu (the Voice menu in CyclesVoice)
void CyclesEmuEditor::chooseTwinFolder()
{
    chooser = std::make_unique<juce::FileChooser>("Twinshot samples: a folder of WAVs or AIFFs (subfolders too)",
                                                  proc.twinSamples().folder);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                         [this](const juce::FileChooser& fc) {
                             const auto dir = fc.getResult();
                             if (dir.isDirectory())
                                 twinLoaded(proc.loadTwinFolder(dir));
                         });
}

void CyclesEmuEditor::twinLoaded(int n)
{
    const auto& t = proc.twinSamples();
    status.setText(n < 0 ? juce::String("Twinshot: still loading the last folder")
                         : "Twinshot: " + juce::String(n) + (t.found() > n ? " of " + juce::String(t.found()) : juce::String())
                               + " samples from " + t.folder.getFileName()
                               + (t.found() > kTwinSlots && proc.twinPick().random ? " (a random pick)" : ""),
                   juce::dontSendNotification);
    juce::Timer::callAfterDelay(200, [safe = juce::Component::SafePointer<CyclesEmuEditor>(this)] {
        if (safe != nullptr)
            for (auto* p : safe->pages)
                if (auto* tp = dynamic_cast<TrackPage*>(p))
                    tp->refreshTwinNames();
    });
}

// SP TWINSHOT's samples: the folder (and its subfolders), and when it holds
// more than the 64 slots, which 64
juce::PopupMenu CyclesEmuEditor::twinMenu()
{
    juce::PopupMenu m;
    const auto& t = proc.twinSamples();
    const bool random = proc.twinPick().random, more = t.found() > kTwinSlots;
    m.addItem("Choose Folder...", [this] { chooseTwinFolder(); });
    m.addSeparator();
    m.addItem(t.folder.isDirectory() ? juce::String(t.count()) + (more ? " of " + juce::String(t.found()) : juce::String())
                                           + " samples: " + t.folder.getFileName()
                                     : juce::String("No folder chosen"),
              false, false, nullptr);
    m.addItem("First 64 By Name", true, !random, [this] { twinLoaded(proc.setTwinPick(false, false)); });
    m.addItem("Random 64", true, random, [this] { twinLoaded(proc.setTwinPick(true, false)); });
    m.addItem("Pick Another Random 64", more, false, [this] { twinLoaded(proc.setTwinPick(true, true)); });
    return m;
}

juce::PopupMenu CyclesEmuEditor::kitMenu()
{
    juce::PopupMenu m;
    m.addSubMenu("Twinshot Samples", twinMenu());
    m.addSeparator();
    m.addItem(undoWhat.isEmpty() ? juce::String("Undo") : "Undo " + undoWhat, undoKit.isValid(), false, [this] {
        auto redo = kitToTree(proc.state());
        treeToKit(proc.state(), undoKit);
        undoKit = redo;   // undo again: redo
        undoWhat = "Undo";
        status.setText("Undone", juce::dontSendNotification);
    });
    m.addSeparator();
    m.addItem("Load Kit...", [this] { chooseFile(true, false); });
    m.addItem("Save Kit As...", [this] { chooseFile(true, true); });
    m.addSeparator();
    m.addItem("Reset Kit (new project's kit)", [this] {
        remember("Reset Kit");
        proc.resetKit();
        status.setText("Kit reset", juce::dontSendNotification);
    });
    m.addItem("Randomise All Tracks (Sound)", [this] {
        remember("Randomise All");
        for (int t = 0; t < kTracks; ++t)
            randomise(proc.state(), t, RandomSection::Sound, rng, false);
    });
    return m;
}

juce::PopupMenu CyclesEmuEditor::trackMenu()
{
    juce::PopupMenu m;
    const bool track = current < proc.trackPages();
    // CyclesVoice: the one sound; and the undo the Kit menu holds otherwise
    const juce::String what = proc.isVoice() ? juce::String("Sound") : "Track " + juce::String(current + 1);
    if (proc.isVoice())
    {
        m.addItem(undoWhat.isEmpty() ? juce::String("Undo") : "Undo " + undoWhat, undoKit.isValid(), false, [this] {
            auto redo = kitToTree(proc.state());
            treeToKit(proc.state(), undoKit);
            undoKit = redo;
            undoWhat = "Undo";
        });
        m.addSeparator();
        m.addSubMenu("Twinshot Samples", twinMenu());
        m.addSeparator();
    }
    m.addItem("Load " + what + "...", track, false, [this] { chooseFile(false, false); });
    m.addItem("Save " + what + " As...", track, false, [this] { chooseFile(false, true); });
    m.addSeparator();
    m.addItem("Copy " + what, track, false, [this] { trackClipboard = trackToTree(proc.state(), current); });
    m.addItem("Paste to " + what, track && trackClipboard.isValid(), false, [this] {
        remember("Paste");
        treeToTrack(proc.state(), current, trackClipboard);
    });
    m.addItem("Reset " + what + " (init sound)", track && proc.canResetTrack(), false, [this] {
        remember("Reset Track");
        proc.resetTrack(current);
        status.setText("Track " + juce::String(current + 1) + " reset", juce::dontSendNotification);
    });
    if (track)   // velocity and key modulation, the panel over the page
        if (auto* tp = dynamic_cast<TrackPage*>(pages[current]))
            m.addItem("Sound Setup (Velocity, Key Tracking)...", [tp] { tp->showSetup(true); });
    // the Syntakt's portamento setup, and legato's note priority
    if (track)
    {
        juce::PopupMenu glideMenu;
        auto choice = [&](const char* id, const juce::String& title, juce::StringArray items) {
            glideMenu.addSectionHeader(title);
            auto* p = proc.state().getParameter(paramId(current, id));
            const int now = juce::roundToInt(p->convertFrom0to1(p->getValue()));
            for (int i = 0; i < items.size(); ++i)
                glideMenu.addItem(items[i], true, now == i, [this, id, i] {
                    setPlain(proc.state(), paramId(current, id), (float)i);
                });
        };
        choice("pslope", "Slope", {"Constant Rate", "Constant Time"});
        choice("pstyle", "Style", {"Glide", "Glissando"});
        choice("plegonly", "Legato Only", {"Off", "On"});
        choice("pgating", "Gating", {"Off", "On"});
        choice("notepri", "Note Priority (Legato)", {"Last", "Low", "High"});
        m.addSeparator();
        m.addSubMenu("Glide Setup", glideMenu);
    }
    m.addSeparator();
    const char* names[] = {"Sound", "LFO", "Filter"};
    const RandomSection which[] = {RandomSection::Sound, RandomSection::Lfo, RandomSection::Filter};
    for (int i = 0; i < 3; ++i)
        m.addItem(juce::String("Randomise ") + names[i], track, false, [this, w = which[i]] { randomiseTrack(w, false); });
    for (int i = 0; i < 3; ++i)
        m.addItem(juce::String("Vary ") + names[i], track, false, [this, w = which[i]] { randomiseTrack(w, true); });
    return m;
}

// The colour scheme is a preference shared by every instance, kept in
// ~/Library/Application Support/CyclesEmu/CyclesEmu.settings.
// (settings(): Settings.h)

juce::PopupMenu CyclesEmuEditor::viewMenu()
{
    juce::PopupMenu m;
    for (int i = 0; i < kNumThemes; ++i)
        m.addItem(juce::String("Colour: ") + kThemeNames[i], true, currentTheme() == i, [this, i] { setTheme(i); });
    m.addSeparator();
    m.addItem("Fanfare On Kit Save", true, fanfareOn(), [] { setFanfareOn(!fanfareOn()); });
    return m;
}

void CyclesEmuEditor::setTheme(int index)
{
    applyTheme(index);
    settings().setValue("theme", index);
    settings().saveIfNeeded();
    repaint();   // every child repaints with it
}

juce::PopupMenu CyclesEmuEditor::helpMenu()
{
    juce::PopupMenu m;
    m.addItem("Help Mode (?)", true, helpMode, [this] { setHelpMode(!helpMode); });
    m.addItem("Show Presets Folder", [] { presetFolder(true).getParentDirectory().revealToUser(); });
    return m;
}

void CyclesEmuEditor::randomiseTrack(RandomSection s, bool variation)
{
    if (current < proc.trackPages())
    {
        remember(variation ? "Vary" : "Randomise");
        randomise(proc.state(), current, s, rng, variation);
    }
}

void CyclesEmuEditor::remember(const juce::String& what)
{
    undoKit = kitToTree(proc.state());
    undoWhat = what;
}

void CyclesEmuEditor::chooseFile(bool kits, bool saving)
{
    const juce::String ext = kits ? "*.cykit" : "*.cytrack";
    const int track = current;
    chooser = std::make_unique<juce::FileChooser>(saving ? (kits ? "Save Kit" : "Save Track") : (kits ? "Load Kit" : "Load Track"),
                                                  presetFolder(kits), ext);
    auto flags = juce::FileBrowserComponent::canSelectFiles
                 | (saving ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                           : juce::FileBrowserComponent::openMode);
    chooser->launchAsync(flags, [this, kits, saving, track](const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f == juce::File())
            return;
        auto& st = proc.state();
        if (saving)
        {
            f = f.withFileExtension(kits ? "cykit" : "cytrack");
            const bool ok = writeTree(kits ? kitToTree(st) : trackToTree(st, track), f);
            status.setText(ok ? "Saved " + f.getFileName() : "Could not save " + f.getFileName(),
                           juce::dontSendNotification);
            if (ok && kits)
                playFanfare();
            return;
        }
        auto tree = readTree(f);
        if (kits ? !tree.hasType("CyclesKit") : !tree.hasType("CyclesTrack"))
        {
            status.setText("Not a " + juce::String(kits ? "kit" : "track") + ": " + f.getFileName(),
                           juce::dontSendNotification);
            return;
        }
        remember(kits ? "Load Kit" : "Load Track");
        kits ? treeToKit(st, tree) : treeToTrack(st, track, tree);
        status.setText("Loaded " + f.getFileName(), juce::dontSendNotification);
    });
}

CyclesEmuEditor::CyclesEmuEditor(CyclesEmuProcessor& p) : AudioProcessorEditor(p), proc(p)
{
    applyTheme(settings().getIntValue("theme", 0));
    setLookAndFeel(&look);
    setAccent(*this, kNavy);
    auto& st = p.state();
    using S = ParamControl::Style;
    auto g = [&](const char* id, const juce::String& label, S s) {
        globals.push_back(std::make_unique<ParamControl>(st, id, label, s));
        addAndMakeVisible(*globals.back());
    };
    if (proc.isVoice())
        g("voices", "Voices", S::Knob);
    else if (proc.isFx())
    {
    }
    else
    {
        g("midimode", "MIDI Mode", S::Choice);
        g("keystrack", "Key Trk", S::Knob);
    }
    g("outgain", "Output", S::Knob);
    addAndMakeVisible(codecTempo);   // tempo and host tempo
    if (proc.isFx())   // the effect: nothing to load per track
        menuBar.menus = {{"View", [this] { return viewMenu(); }}, {"Help", [this] { return helpMenu(); }}};
    else if (proc.isVoice())   // one sound: no kit, and the sound's own menu
        menuBar.menus = {{"Voice", [this] { return trackMenu(); }},
                         {"View", [this] { return viewMenu(); }},
                         {"Help", [this] { return helpMenu(); }}};
    else
        menuBar.menus = {{"Kit", [this] { return kitMenu(); }},
                         {"Track", [this] { return trackMenu(); }},
                         {"View", [this] { return viewMenu(); }},
                         {"Help", [this] { return helpMenu(); }}};
    addAndMakeVisible(menuBar);
    status.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(status);

    // the tabs: a page per track (one in CyclesVoice), the mixer (CyclesEmu),
    // and the FX
    const int nT = proc.trackPages();
    auto addPage = [&](const juce::String& name, juce::Colour accent, juce::Component* page) {
        const int index = pages.size();
        auto* b = tabs.add(new juce::TextButton(name));
        b->setClickingTogglesState(true);
        b->setRadioGroupId(1);
        b->getProperties().set("tab", true);
        setAccent(*b, accent);
        b->onClick = [this, index] { showPage(index); };
        pages.add(page);
        setAccent(*page, accent);
        addChildComponent(page);
    };
    for (int i = 0; i < nT; ++i)
    {
        auto* tp = new TrackPage(p, i);
        tp->beforeChange = [this](const juce::String& what) { remember(what); };
        addPage(proc.isVoice() ? juce::String("Voice") : "Trk " + juce::String(i + 1), kAccents[i], tp);   // eight fit
    }
    if (!proc.isVoice() && !proc.isFx())
        addPage("Mix", kAccentFx, new MixerPage(p));
    addPage("FX", kAccentFx, new FxPage(st));
    // tabs above the panel's edge, so the selected one opens into it
    for (auto* b : tabs)
        addAndMakeVisible(b);
    helpButton.setClickingTogglesState(true);
    helpButton.getProperties().set("caption", true);
    helpButton.onClick = [this] { setHelpMode(helpButton.getToggleState()); };
    addAndMakeVisible(helpButton);
    addChildComponent(bubble);
    // hover anywhere in the editor reaches mouseMove, for the help mode
    addMouseListener(this, true);
    // Sliders built their value readouts before they met our look: have
    // them rebuild them now, as Look::createSliderTextBox's PixelEntry.
    sendLookAndFeelChange();
    if (proc.isFx())   // one page: no tabs
        for (auto* b : tabs)
            b->setVisible(false);
    setSize(kW, proc.isFx() ? kFxH : kH);
    showPage(0);
    for (int t = 0; t < proc.trackPages(); ++t)
        seenTrigs[t] = proc.trigCount(t);
    startTimerHz(30);   // the tab lights; the status line only changes when its text does
}

CyclesEmuEditor::~CyclesEmuEditor() { setLookAndFeel(nullptr); }

void CyclesEmuEditor::setTwinPage2(bool p2)
{
    if (auto* tp = dynamic_cast<TrackPage*>(pages[current]))
        tp->setTwinPage2(p2);
}

void CyclesEmuEditor::setAmpView(bool on)
{
    if (auto* tp = dynamic_cast<TrackPage*>(pages[current]))
        tp->setAmpView(on);
}

void CyclesEmuEditor::setLfo2View(bool on)
{
    if (auto* tp = dynamic_cast<TrackPage*>(pages[current]))
        tp->setLfo2View(on);
}

void CyclesEmuEditor::showSoundSetup(bool on)
{
    if (auto* tp = dynamic_cast<TrackPage*>(pages[current]))
        tp->showSetup(on);
}

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
    drawText(g, proc.isFx() ? "CyclesFX.exe" : proc.isVoice() ? "CyclesVoice.exe" : "CyclesEmu.exe",
             kTitle.getX() + 24, kTitle.getY() + 4, kTitleText);
    auto caps = kTitle.withTrimmedRight(3).withSizeKeepingCentre(kTitle.getWidth() - 6, 16);
    const char* glyph[] = {"x", "[", "_"};   // decorative; the "?" left of them is helpButton
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
        int x = logo.getX() + i * 6 * 4;
        drawText(g, word.substring(i, i + 1), x + 3, logo.getY() + 3, kDark, 4);
        drawText(g, word.substring(i, i + 1), x, logo.getY(), kAccents[i % 4], 4);
    }
    drawText(g, proc.isFx() ? "FX  v1.0" : proc.isVoice() ? "Voice  v1.0" : "Emu  v1.0", logo.getX() + 2,
             logo.getY() + 40, kText);
    drawText(g, "Model:Cycles", logo.getX() + 2, logo.getY() + 58, kDimText);

    // the page panel, and the status bar
    g.setColour(kFace);
    g.fillRect(panelArea());
    raised(g, panelArea());
    auto sb = statusArea();
    auto right = sb.removeFromRight(200);
    sunken(g, sb.withTrimmedRight(2), false);
    sunken(g, right, false);
    const bool onTrack = current < proc.trackPages();
    juce::String where = !onTrack ? juce::String(dynamic_cast<MixerPage*>(pages[current]) ? "Mixer" : "Send FX")
                         : proc.isVoice() ? juce::String(juce::roundToInt(proc.state().getRawParameterValue("voices")->load())) + " voices"
                                          : "Track " + juce::String(current + 1) + " of " + juce::String(kTracks);
    g.setColour(onTrack ? kAccents[current] : kAccentFx);
    g.fillRect(right.getX() + 8, right.getCentreY() - 4, 8, 8);
    drawText(g, where, right.withTrimmedLeft(24), juce::Justification::centredLeft, kText);
}

// the page panel: below the tabs, or (CyclesFX, no tabs) right below the
// system strip; the status bar along the bottom
juce::Rectangle<int> CyclesEmuEditor::panelArea() const
{
    const int top = proc.isFx() ? 182 : 202;
    return {10, top, kW - 20, statusArea().getY() - 4 - top};
}

juce::Rectangle<int> CyclesEmuEditor::statusArea() const { return {4, getHeight() - 28, kW - 8, 24}; }

void CyclesEmuEditor::resized()
{
    auto top = kToolbar.withTrimmedTop(20).reduced(12, 6);
    top.removeFromLeft(kLogoW);
    if (proc.isFx())
    {
        globals[0]->setBounds(top.removeFromRight(92).reduced(4, 0));   // output
        top.removeFromLeft(230);
    }
    else if (proc.isVoice())
    {
        top.removeFromLeft(120);
        globals[0]->setBounds(top.removeFromLeft(110).reduced(4, 0));   // voices
        globals[1]->setBounds(top.removeFromRight(92).reduced(4, 0));   // output
        top.removeFromLeft(110);
    }
    else
    {
        globals[0]->setBounds(top.removeFromLeft(252).reduced(4, 0));   // MIDI mode
        globals[1]->setBounds(top.removeFromLeft(100).reduced(4, 0));   // key track
        globals[2]->setBounds(top.removeFromRight(92).reduced(4, 0));   // output
    }
    codecTempo.setBounds(top.reduced(6, 0).withTrimmedTop(-6));
    status.setBounds(statusArea().withTrimmedRight(202).reduced(8, 2));
    menuBar.setBounds(kMenu);
    // left of the three caption boxes (3 x 18 + 2 px gap, 3 px margin)
    helpButton.setBounds(kTitle.getRight() - 3 - 56 - 22, kTitle.getY() + 3, 18, 16);
    auto bar = kTabs;
    const int tw = juce::jmin(120, bar.getWidth() / juce::jmax(1, tabs.size()));
    for (auto* b : tabs)
        b->setBounds(bar.removeFromLeft(tw).withTrimmedRight(-1).withHeight(26));
    for (auto* p : pages)
        p->setBounds(panelArea().reduced(10, 12));
}

void CyclesEmuEditor::setHelpMode(bool on)
{
    helpMode = on;
    helpButton.setToggleState(on, juce::dontSendNotification);
    helpTarget = nullptr;
    bubble.setVisible(false);
    timerCallback();
}

void CyclesEmuEditor::showHelpFor(const juce::String& id)
{
    setHelpMode(true);
    // the visible control with that id (each track page has its own)
    std::function<juce::Component*(juce::Component&)> find = [&](juce::Component& c) -> juce::Component* {
        if (!c.isVisible())
            return nullptr;
        if (c.getProperties()["helpId"].toString() == id)
            return &c;
        for (auto* ch : c.getChildren())
            if (auto* f = find(*ch))
                return f;
        return nullptr;
    };
    juce::Component* pc = nullptr;
    for (auto* ch : getChildren())   // (the editor itself is not visible until a host shows it)
        if (pc == nullptr)
            pc = find(*ch);
    updateHelp(pc);
}

void CyclesEmuEditor::mouseMove(const juce::MouseEvent& e) { updateHelp(e.eventComponent); }

void CyclesEmuEditor::mouseExit(const juce::MouseEvent& e)
{
    // leaving the editor altogether (moves between children arrive as enters)
    if (!getLocalBounds().contains(e.getEventRelativeTo(this).getPosition()))
        updateHelp(nullptr);
}

void CyclesEmuEditor::updateHelp(juce::Component* under)
{
    if (!helpMode)
        return;
    juce::Component* pc = nullptr;   // the nearest component with a help id
    for (auto* c = under; c != nullptr && c != this; c = c->getParentComponent())
        if (c->getProperties().contains("helpId"))
        {
            pc = c;
            break;
        }
    if (pc == helpTarget)
        return;
    helpTarget = pc;
    if (pc == nullptr)
    {
        bubble.setVisible(false);
        return;
    }
    const juce::String id = pc->getProperties()["helpId"];
    // the macros' text depends on the track's machine
    int machine = 0;
    if (id.length() > 3 && id[0] == 't' && id[2] == '_')
        if (auto* m = dynamic_cast<juce::AudioParameterChoice*>(proc.state().getParameter(id.substring(0, 3) + "machine")))
            machine = m->getIndex();
    bubble.show(helpFor(id, machine), getLocalArea(pc, pc->getLocalBounds()), getLocalBounds().reduced(6));
    bubble.toFront(false);
}

void HelpBubble::show(const HelpText& t, juce::Rectangle<int> target, juce::Rectangle<int> within)
{
    text = t;
    const int w = 330, pad = 8;
    int h = pad * 2 + textHeight() + 8 + wrappedHeight(text.body, w - pad * 2);
    // below the control if it fits, else above; kept inside the window
    int y = target.getBottom() + 4;
    if (y + h > within.getBottom())
        y = target.getY() - 4 - h;
    int x = juce::jlimit(within.getX(), within.getRight() - w, target.getCentreX() - w / 2);
    setBounds(x, juce::jmax(within.getY(), y), w, h);
    setVisible(text.title.isNotEmpty());
    repaint();
}

void HelpBubble::paint(juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour(juce::Colour(0xffffffe1));   // Win95 tooltip yellow
    g.fillRect(r);
    g.setColour(kDark);
    g.drawRect(r, 1);
    auto in = r.reduced(8);
    auto title = in.removeFromTop(textHeight());
    const juce::Colour ink(0xff000080), body(0xff000000);   // the tooltip keeps its own colours
    drawText(g, text.title, title.getX() + 1, title.getY(), ink);   // drawn twice: bold
    drawText(g, text.title, title.getX(), title.getY(), ink);
    in.removeFromTop(8);
    drawWrapped(g, text.body, in, body);
}

// A track's tab chip lights for 150 ms when it is played. The page stays
// where it is (following the track played made editing another track
// impossible while a pattern ran).
void CyclesEmuEditor::updateTrigLights()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    for (int t = 0; t < proc.trackPages(); ++t)
    {
        const uint32_t n = proc.trigCount(t);
        if (n != seenTrigs[t])
            seenTrigs[t] = n, litUntil[t] = now + 150.0;
        const bool lit = now < litUntil[t];
        if ((bool)tabs[t]->getProperties()["lit"] != lit)
        {
            tabs[t]->getProperties().set("lit", lit);
            tabs[t]->repaint();
        }
    }
}

void CyclesEmuEditor::timerCallback()
{
    updateTrigLights();
    if (helpMode)
    {
        status.setText("Help: hover over a control. Click ? again to leave.", juce::dontSendNotification);
        return;
    }
    if (proc.engineStatus().isNotEmpty())
        status.setText(proc.engineStatus(), juce::dontSendNotification);
    else
        status.setText("Ready.  Engine tempo " + juce::String(proc.currentBpm(), 1) + " BPM",
                       juce::dontSendNotification);
}
