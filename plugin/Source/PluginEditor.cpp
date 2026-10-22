#include "PluginEditor.h"
#include "LfoShape.h"

using namespace cyclesemu;
using namespace retro;

namespace
{
constexpr int kW = 960, kH = 780;
// the window's furniture
const juce::Rectangle<int> kTitle(4, 4, kW - 8, 22), kMenu(4, 27, kW - 8, 20), kToolbar(10, 52, kW - 20, 124),
    kTabs(14, 180, kW - 28, 24), kPanel(10, 202, kW - 20, 546), kStatus(4, kH - 28, kW - 8, 24);
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
        case Style::Icons:
        {
            addAndMakeVisible(caption);
            caption.setJustificationType(juce::Justification::centredLeft);
            auto names = p->getAllValueStrings();
            for (int i = 0; i < names.size(); ++i)
            {
                icons.push_back(std::make_unique<IconButton>(iconSet, i));
                icons.back()->onClick = [this, i] { iconAtt->setValueAsCompleteGesture((float)i); };
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
    auto r = getLocalBounds().reduced(12, 8);
    r.removeFromTop(16);
    if (rows.empty())
        return;
    size_t cols = 4;
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

LfoScope::LfoScope(CyclesEmuProcessor& p, int t) : proc(p), track(t)
{
    const char* ids[] = {"lfowave", "lforeset", "lfosync", "lfomult", "lfodest",
                         "lfospeed", "lfodepth", "lfofade", "lfophase"};
    for (int i = 0; i < 9; ++i)
        raw[i] = p.state().getRawParameterValue(paramId(t, ids[i]));
    setInterceptsMouseClicks(false, false);
    startTimerHz(30);
}

LfoScope::State LfoScope::read() const
{
    State s;
    int* f[] = {&s.wave, &s.trig, &s.sync, &s.mult, &s.dest, &s.speed, &s.depth, &s.fade, &s.phase};
    for (int i = 0; i < 9; ++i)
        *f[i] = juce::roundToInt(raw[i]->load());
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
    else if (s.trig == 3)
        run = juce::jmin(t, 1.0);      // One
    else if (s.trig == 4)
        run = juce::jmin(t, 0.5);      // Half
    double dir = s.speed >= 64 ? 1.0 : -1.0;
    float v = lfoWave(s.wave, s.phase / 128.0 + dir * run) * (float)(s.depth - 64) / 64.0f;
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
        const auto tag = "> " + kLfoDestNames[s.dest];
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
    const char* ids[] = {"fcut", "freso", "fenv", "fdec"};
    for (int i = 0; i < 4; ++i)
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
    for (int i = 0; i < 4; ++i)
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
    const int env = juce::roundToInt(raw[2]->load()) - 64, dec = juce::roundToInt(raw[3]->load());
    const bool off = cut == 64, lp = cut < 64;
    const double fc = lp ? 20000.0 * std::pow(5.0 / 20000.0, (63 - cut) / 63.0)
                         : 5.0 * std::pow(19000.0 / 5.0, (cut - 65) / 62.0);
    const double q = 0.5 * std::pow(100.0, res / 127.0);

    auto r = getLocalBounds();
    auto in = codecScreen(g, r);
    auto head = in.reduced(6, 3).removeFromTop(14);
    auto num = [](double hz) {
        return hz >= 1000.0 ? juce::String(hz / 1000.0, hz >= 10000.0 ? 1 : 2) + "kHz" : juce::String((int)hz) + "Hz";
    };
    drawText(g, off ? juce::String("- OFF -") : "- " + juce::String(lp ? "LP" : "HP") + " " + num(fc) + " -",
             head, juce::Justification::centredLeft, kPhosphor);
    drawText(g, env == 0 ? juce::String("ENV 0") : "ENV " + juce::String(env > 0 ? "+" : "") + juce::String(env) +
                                                       " D" + juce::String(dec),
             head, juce::Justification::centredRight, env == 0 ? kPhosphorDim : kPhosphor);

    auto p = in.reduced(8, 4).withTrimmedTop(16);
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
        if (!off)
        {
            // the 2-pole state-variable filter's magnitude
            const double u = hz / fc, den = std::sqrt((1 - u * u) * (1 - u * u) + (u / q) * (u / q));
            db = 20.0 * std::log10((lp ? 1.0 : u * u) / den);
        }
        const int y = yOf(db) & ~1;
        g.fillRect(p.getX() + x, prev < 0 ? y : juce::jmin(prev, y), 2, prev < 0 ? 2 : std::abs(prev - y) + 2);
        prev = y;
    }
    if (!off)   // the cutoff's mark
    {
        g.setColour(kPhosphorDim);
        g.fillRect(xOf(juce::jlimit(20.0, 20000.0, fc)), p.getY(), 1, p.getHeight());
    }
    scanlines(g, in);
}

TrackPage::TrackPage(CyclesEmuProcessor& proc, int t) : scope(proc, t), filterScope(proc, t)
{
    using S = ParamControl::Style;
    auto& st = proc.state();
    addAndMakeVisible(sound);
    addAndMakeVisible(lfo);
    auto* punch = make(st, t, "punch", "Punch", S::Toggle);
    sound.addRow({make(st, t, "machine", "Machine", S::Icons, IconSet::Machine), punch,
                  make(st, t, "gate", "Gate", S::Toggle), make(st, t, "mute", "Mute", S::Toggle)});
    auto* color = make(st, t, "color", "Color", S::Knob);
    auto* shape = make(st, t, "shape", "Shape", S::Knob);
    auto* sweep = make(st, t, "sweep", "Sweep", S::Knob);
    auto* contour = make(st, t, "contour", "Contour", S::Knob);
    sound.addRow({make(st, t, "pitch", "Pitch", S::Knob), make(st, t, "decay", "Decay", S::Knob), color, shape,
                  sweep, contour});
    macroKnobs[0] = color, macroKnobs[1] = shape, macroKnobs[2] = sweep, macroKnobs[3] = contour,
    macroKnobs[4] = punch;
    // Modded-Cycles' guide: what each Syntakt engine maps the knobs to
    // (full names in Help.cpp), shortened to fit the pixel font
    machineAtt = std::make_unique<juce::ParameterAttachment>(
        *st.getParameter(paramId(t, "machine")),
        [this](float v) {
            static const char* labels[6][5] = {
                {"Color", "Shape", "Sweep", "Contour", "Punch"},   // the six originals
                {"Inharm", "Frq Cpx", "P.Sweep", "Mod Env", "Punch"},
                {"Body Ch", "Balance", "Spc Crn", "Body Ev", "Punch"},
                {"Form", "Impact", "Bright", "Prt Dec", "Punch"},
                {"Detune", "Balance", "Rate Rx", "Wave", "Bits"},
                {"Nse Mod", "Det Anm", "Detune", "Osc Mix", "Sub"},
            };
            int m = juce::roundToInt(v), row = m < 6 ? 0 : m - 5;
            for (int i = 0; i < 5; ++i)
                macroKnobs[i]->setLabel(labels[row][i]);
        },
        nullptr);
    machineAtt->sendInitialUpdate();
    sound.addRow({make(st, t, "voldist", "Vol+Dst", S::Knob), make(st, t, "pan", "Pan", S::Knob),
                  make(st, t, "delaysend", "Delay", S::Knob), make(st, t, "reverbsend", "Reverb", S::Knob),
                  make(st, t, "level", "Level", S::Knob)});
    lfo.addRow({make(st, t, "lfowave", "Wave", S::Icons, IconSet::Wave),
                make(st, t, "lforeset", "Trig", S::Icons, IconSet::Trig)});
    auto* dest = make(st, t, "lfodest", "Destination", S::Choice);
    auto* mult = make(st, t, "lfomult", "Mult", S::Choice);
    mult->preferredWidth = 84;
    lfo.addRow({make(st, t, "lfosync", "", S::Icons, IconSet::Clock), mult, dest});
    lfo.addCustomRow(&scope, 84);
    lfo.addRow({make(st, t, "lfospeed", "Speed", S::Knob), make(st, t, "lfodepth", "Depth", S::Knob),
                make(st, t, "lfofade", "Fade", S::Knob), make(st, t, "lfophase", "Phase", S::Knob)});
    addAndMakeVisible(filter);
    filter.addRow({make(st, t, "fcut", "Cutoff", S::Knob), make(st, t, "freso", "Reso", S::Knob),
                   make(st, t, "fenv", "Env", S::Knob), make(st, t, "fdec", "Decay", S::Knob)});
    addAndMakeVisible(filterScope);
    for (auto [section, which] : {std::pair{&sound, RandomSection::Sound}, std::pair{&lfo, RandomSection::Lfo},
                                  std::pair{&filter, RandomSection::Filter}})
        section->enableDice([this, &st, t, which = which](bool variation) { randomise(st, t, which, rng, variation); });
}

void TrackPage::resized()
{
    auto r = getLocalBounds();
    // the filter page along the bottom: its knobs, and the response beside
    auto bottom = r.removeFromBottom(162).withTrimmedTop(6);
    filter.setBounds(bottom.removeFromLeft(bottom.getWidth() * 62 / 100).withTrimmedRight(6));
    filterScope.setBounds(bottom.withTrimmedLeft(6).withTrimmedTop(8));
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
            g.setColour(kNavy);
            g.fillRect(r.reduced(0, 1));
        }
        // Win95 underlines the access letter
        const auto c = i == open ? kLight : kText;
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

juce::PopupMenu CyclesEmuEditor::kitMenu()
{
    juce::PopupMenu m;
    m.addItem("Load Kit...", [this] { chooseFile(true, false); });
    m.addItem("Save Kit As...", [this] { chooseFile(true, true); });
    m.addSeparator();
    m.addItem("Randomise All Tracks (Sound)", [this] {
        for (int t = 0; t < kTracks; ++t)
            randomise(proc.state(), t, RandomSection::Sound, rng, false);
    });
    return m;
}

juce::PopupMenu CyclesEmuEditor::trackMenu()
{
    juce::PopupMenu m;
    const bool track = current < kTracks;
    const juce::String n = track ? juce::String(current + 1) : juce::String();
    m.addItem("Load Track " + n + "...", track, false, [this] { chooseFile(false, false); });
    m.addItem("Save Track " + n + " As...", track, false, [this] { chooseFile(false, true); });
    m.addSeparator();
    m.addItem("Copy Track " + n, track, false, [this] { trackClipboard = trackToTree(proc.state(), current); });
    m.addItem("Paste to Track " + n, track && trackClipboard.isValid(), false,
              [this] { treeToTrack(proc.state(), current, trackClipboard); });
    m.addSeparator();
    const char* names[] = {"Sound", "LFO", "Filter"};
    const RandomSection which[] = {RandomSection::Sound, RandomSection::Lfo, RandomSection::Filter};
    for (int i = 0; i < 3; ++i)
        m.addItem(juce::String("Randomise ") + names[i], track, false, [this, w = which[i]] { randomiseTrack(w, false); });
    for (int i = 0; i < 3; ++i)
        m.addItem(juce::String("Vary ") + names[i], track, false, [this, w = which[i]] { randomiseTrack(w, true); });
    return m;
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
    if (current < kTracks)
        randomise(proc.state(), current, s, rng, variation);
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
            return;
        }
        auto tree = readTree(f);
        if (kits ? !tree.hasType("CyclesKit") : !tree.hasType("CyclesTrack"))
        {
            status.setText("Not a " + juce::String(kits ? "kit" : "track") + ": " + f.getFileName(),
                           juce::dontSendNotification);
            return;
        }
        kits ? treeToKit(st, tree) : treeToTrack(st, track, tree);
        status.setText("Loaded " + f.getFileName(), juce::dontSendNotification);
    });
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
    g("outgain", "Output", S::Knob);
    addAndMakeVisible(codecTempo);   // tempo and host tempo
    menuBar.menus = {{"Kit", [this] { return kitMenu(); }},
                     {"Track", [this] { return trackMenu(); }},
                     {"Help", [this] { return helpMenu(); }}};
    addAndMakeVisible(menuBar);
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
            pages.add(new TrackPage(p, i));
        else
            pages.add(new FxPage(st));
        setAccent(*pages.getLast(), kAccents[i]);
        addChildComponent(pages.getLast());
    }
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
    drawText(g, "Emu  v1.0", logo.getX() + 2, logo.getY() + 40, kText);
    drawText(g, "Model:Cycles", logo.getX() + 2, logo.getY() + 58, kDimText);

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
    globals[0]->setBounds(top.removeFromLeft(252).reduced(4, 0));   // MIDI mode
    globals[1]->setBounds(top.removeFromLeft(100).reduced(4, 0));   // key track
    globals[2]->setBounds(top.removeFromRight(92).reduced(4, 0));   // output
    codecTempo.setBounds(top.reduced(6, 0).withTrimmedTop(-6));
    status.setBounds(kStatus.withTrimmedRight(202).reduced(8, 2));
    menuBar.setBounds(kMenu);
    // left of the three caption boxes (3 x 18 + 2 px gap, 3 px margin)
    helpButton.setBounds(kTitle.getRight() - 3 - 56 - 22, kTitle.getY() + 3, 18, 16);
    auto bar = kTabs;
    for (auto* b : tabs)
        b->setBounds(bar.removeFromLeft(120).withTrimmedRight(-1).withHeight(26));
    for (auto* p : pages)
        p->setBounds(kPanel.reduced(10, 12));
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
    drawText(g, text.title, title.getX() + 1, title.getY(), kNavy);   // drawn twice: bold
    drawText(g, text.title, title.getX(), title.getY(), kNavy);
    in.removeFromTop(8);
    drawWrapped(g, text.body, in, kText);
}

void CyclesEmuEditor::timerCallback()
{
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
