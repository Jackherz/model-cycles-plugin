#include "Retro.h"
#include <cmath>
#include <map>

namespace retro
{
namespace
{
// Each glyph: 7 rows of 5 columns, top row first.
struct Glyph
{
    const char* rows[7];
};

const std::map<juce::juce_wchar, Glyph>& glyphs()
{
    static const std::map<juce::juce_wchar, Glyph> g = {
        {'A', {{"01110", "10001", "10001", "11111", "10001", "10001", "10001"}}},
        {'B', {{"11110", "10001", "10001", "11110", "10001", "10001", "11110"}}},
        {'C', {{"01110", "10001", "10000", "10000", "10000", "10001", "01110"}}},
        {'D', {{"11110", "10001", "10001", "10001", "10001", "10001", "11110"}}},
        {'E', {{"11111", "10000", "10000", "11110", "10000", "10000", "11111"}}},
        {'F', {{"11111", "10000", "10000", "11110", "10000", "10000", "10000"}}},
        {'G', {{"01110", "10001", "10000", "10111", "10001", "10001", "01111"}}},
        {'H', {{"10001", "10001", "10001", "11111", "10001", "10001", "10001"}}},
        {'I', {{"01110", "00100", "00100", "00100", "00100", "00100", "01110"}}},
        {'J', {{"00111", "00010", "00010", "00010", "00010", "10010", "01100"}}},
        {'K', {{"10001", "10010", "10100", "11000", "10100", "10010", "10001"}}},
        {'L', {{"10000", "10000", "10000", "10000", "10000", "10000", "11111"}}},
        {'M', {{"10001", "11011", "10101", "10101", "10001", "10001", "10001"}}},
        {'N', {{"10001", "10001", "11001", "10101", "10011", "10001", "10001"}}},
        {'O', {{"01110", "10001", "10001", "10001", "10001", "10001", "01110"}}},
        {'P', {{"11110", "10001", "10001", "11110", "10000", "10000", "10000"}}},
        {'Q', {{"01110", "10001", "10001", "10001", "10101", "10010", "01101"}}},
        {'R', {{"11110", "10001", "10001", "11110", "10100", "10010", "10001"}}},
        {'S', {{"01111", "10000", "10000", "01110", "00001", "00001", "11110"}}},
        {'T', {{"11111", "00100", "00100", "00100", "00100", "00100", "00100"}}},
        {'U', {{"10001", "10001", "10001", "10001", "10001", "10001", "01110"}}},
        {'V', {{"10001", "10001", "10001", "10001", "10001", "01010", "00100"}}},
        {'W', {{"10001", "10001", "10001", "10101", "10101", "10101", "01010"}}},
        {'X', {{"10001", "10001", "01010", "00100", "01010", "10001", "10001"}}},
        {'Y', {{"10001", "10001", "10001", "01010", "00100", "00100", "00100"}}},
        {'Z', {{"11111", "00001", "00010", "00100", "01000", "10000", "11111"}}},
        {'0', {{"01110", "10001", "10011", "10101", "11001", "10001", "01110"}}},
        {'1', {{"00100", "01100", "00100", "00100", "00100", "00100", "01110"}}},
        {'2', {{"01110", "10001", "00001", "00010", "00100", "01000", "11111"}}},
        {'3', {{"11111", "00010", "00100", "00010", "00001", "10001", "01110"}}},
        {'4', {{"00010", "00110", "01010", "10010", "11111", "00010", "00010"}}},
        {'5', {{"11111", "10000", "11110", "00001", "00001", "10001", "01110"}}},
        {'6', {{"00110", "01000", "10000", "11110", "10001", "10001", "01110"}}},
        {'7', {{"11111", "00001", "00010", "00100", "01000", "01000", "01000"}}},
        {'8', {{"01110", "10001", "10001", "01110", "10001", "10001", "01110"}}},
        {'9', {{"01110", "10001", "10001", "01111", "00001", "00010", "01100"}}},
        {' ', {{"00000", "00000", "00000", "00000", "00000", "00000", "00000"}}},
        {'!', {{"00100", "00100", "00100", "00100", "00100", "00000", "00100"}}},
        {'+', {{"00000", "00100", "00100", "11111", "00100", "00100", "00000"}}},
        {'-', {{"00000", "00000", "00000", "11111", "00000", "00000", "00000"}}},
        {'.', {{"00000", "00000", "00000", "00000", "00000", "01100", "01100"}}},
        {',', {{"00000", "00000", "00000", "00000", "01100", "00100", "01000"}}},
        {':', {{"00000", "01100", "01100", "00000", "01100", "01100", "00000"}}},
        {'/', {{"00001", "00010", "00010", "00100", "01000", "01000", "10000"}}},
        {'(', {{"00010", "00100", "01000", "01000", "01000", "00100", "00010"}}},
        {')', {{"01000", "00100", "00010", "00010", "00010", "00100", "01000"}}},
        {'=', {{"00000", "00000", "11111", "00000", "11111", "00000", "00000"}}},
        {'%', {{"11000", "11001", "00010", "00100", "01000", "10011", "00011"}}},
        {'_', {{"00000", "00000", "00000", "00000", "00000", "00000", "11111"}}},
        {'#', {{"01010", "01010", "11111", "01010", "11111", "01010", "01010"}}},
        {'>', {{"01000", "00100", "00010", "00001", "00010", "00100", "01000"}}},
        {'<', {{"00010", "00100", "01000", "10000", "01000", "00100", "00010"}}},
        {'?', {{"01110", "10001", "00001", "00010", "00100", "00000", "00100"}}},
        {'\'', {{"00100", "00100", "01000", "00000", "00000", "00000", "00000"}}},
        {'[', {{"01110", "01000", "01000", "01000", "01000", "01000", "01110"}}},
        {']', {{"01110", "00010", "00010", "00010", "00010", "00010", "01110"}}},
        {'|', {{"00100", "00100", "00100", "00100", "00100", "00100", "00100"}}},
        {'*', {{"00000", "00100", "10101", "01110", "10101", "00100", "00000"}}},
    };
    return g;
}

const Glyph& glyphFor(juce::juce_wchar c)
{
    auto& g = glyphs();
    auto it = g.find(juce::CharacterFunctions::toUpperCase(c));
    return it != g.end() ? it->second : g.at('?');
}

// 4x4 Bayer thresholds, 0..15
const int kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
} // namespace

int textWidth(const juce::String& s, int scale)
{
    return s.isEmpty() ? 0 : s.length() * 6 * scale - scale;
}

void drawText(juce::Graphics& g, const juce::String& s, int x, int y, juce::Colour c, int scale)
{
    g.setColour(c);
    for (auto ch : s)
    {
        const auto& gl = glyphFor(ch);
        for (int r = 0; r < 7; ++r)
            for (int col = 0; col < 5; ++col)
                if (gl.rows[r][col] == '1')
                    g.fillRect(x + col * scale, y + r * scale, scale, scale);
        x += 6 * scale;
    }
}

void drawText(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> area, juce::Justification j,
              juce::Colour c, int scale)
{
    juce::String t = s;
    while (t.isNotEmpty() && textWidth(t, scale) > area.getWidth())
        t = t.dropLastCharacters(1);
    int w = textWidth(t, scale), h = textHeight(scale);
    auto r = j.appliedToRectangle(juce::Rectangle<int>(0, 0, w, h), area);
    drawText(g, t, r.getX(), r.getY(), c, scale);
}

namespace
{
juce::StringArray wrapLines(const juce::String& s, int width, int scale)
{
    juce::StringArray lines;
    juce::String line;
    for (auto& w : juce::StringArray::fromTokens(s, " ", ""))
    {
        auto next = line.isEmpty() ? w : line + " " + w;
        if (line.isNotEmpty() && textWidth(next, scale) > width)
        {
            lines.add(line);
            line = w;
        }
        else
            line = next;
    }
    if (line.isNotEmpty())
        lines.add(line);
    return lines;
}
int lineStep(int scale) { return textHeight(scale) + 3 * scale; }
} // namespace

void drawWrapped(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> area, juce::Colour c, int scale)
{
    int y = area.getY();
    for (auto& line : wrapLines(s, area.getWidth(), scale))
    {
        drawText(g, line, area.getX(), y, c, scale);
        y += lineStep(scale);
    }
}

int wrappedHeight(const juce::String& s, int width, int scale)
{
    int n = wrapLines(s, width, scale).size();
    return n == 0 ? 0 : n * lineStep(scale) - 3 * scale;
}

juce::Colour accentFor(const juce::Component& c)
{
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (p->getProperties().contains("accent"))
            return juce::Colour((juce::uint32)(int)p->getProperties()["accent"]);
    return kNavy;
}

void raised(juce::Graphics& g, juce::Rectangle<int> r, bool thick)
{
    g.setColour(kLight);
    g.fillRect(r.getX(), r.getY(), r.getWidth() - 1, 1);
    g.fillRect(r.getX(), r.getY(), 1, r.getHeight() - 1);
    g.setColour(kDark);
    g.fillRect(r.getX(), r.getBottom() - 1, r.getWidth(), 1);
    g.fillRect(r.getRight() - 1, r.getY(), 1, r.getHeight());
    if (thick)
    {
        g.setColour(kHilite);
        g.fillRect(r.getX() + 1, r.getY() + 1, r.getWidth() - 3, 1);
        g.fillRect(r.getX() + 1, r.getY() + 1, 1, r.getHeight() - 3);
        g.setColour(kShadow);
        g.fillRect(r.getX() + 1, r.getBottom() - 2, r.getWidth() - 2, 1);
        g.fillRect(r.getRight() - 2, r.getY() + 1, 1, r.getHeight() - 2);
    }
}

void sunken(juce::Graphics& g, juce::Rectangle<int> r, bool thick)
{
    g.setColour(kShadow);
    g.fillRect(r.getX(), r.getY(), r.getWidth() - 1, 1);
    g.fillRect(r.getX(), r.getY(), 1, r.getHeight() - 1);
    g.setColour(kLight);
    g.fillRect(r.getX(), r.getBottom() - 1, r.getWidth(), 1);
    g.fillRect(r.getRight() - 1, r.getY(), 1, r.getHeight());
    if (thick)
    {
        g.setColour(kDark);
        g.fillRect(r.getX() + 1, r.getY() + 1, r.getWidth() - 3, 1);
        g.fillRect(r.getX() + 1, r.getY() + 1, 1, r.getHeight() - 3);
        g.setColour(kHilite);
        g.fillRect(r.getX() + 1, r.getBottom() - 2, r.getWidth() - 2, 1);
        g.fillRect(r.getRight() - 2, r.getY() + 1, 1, r.getHeight() - 2);
    }
}

void groupBox(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour accent)
{
    auto box = r.withTrimmedTop(7);
    // etched: a shadow line and a light line one pixel in
    g.setColour(kShadow);
    g.drawRect(box.withTrimmedRight(1).withTrimmedBottom(1), 1);
    g.setColour(kLight);
    g.drawRect(box.withTrimmedLeft(1).withTrimmedTop(1), 1);
    int tw = textWidth(title) + 16;
    g.setColour(kFace);
    g.fillRect(r.getX() + 10, r.getY(), tw + 14, 16);
    g.setColour(accent);
    g.fillRect(r.getX() + 14, r.getY() + 3, 8, 8);   // a little colour chip, N64 style
    drawText(g, title, r.getX() + 28, r.getY() + 1, kText);
}

void ditherGradient(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour a, juce::Colour b, int cell)
{
    // 8 colour levels, dithered between neighbours
    const int levels = 8;
    for (int x = r.getX(); x < r.getRight(); x += cell)
    {
        float t = (float)(x - r.getX()) / (float)juce::jmax(1, r.getWidth() - 1);
        float lv = t * (levels - 1);
        int lo = (int)std::floor(lv);
        float frac = lv - (float)lo;
        juce::Colour c0 = a.interpolatedWith(b, (float)lo / (levels - 1));
        juce::Colour c1 = a.interpolatedWith(b, (float)juce::jmin(lo + 1, levels - 1) / (levels - 1));
        for (int y = r.getY(); y < r.getBottom(); y += cell)
        {
            int bx = ((x - r.getX()) / cell) & 3, by = ((y - r.getY()) / cell) & 3;
            g.setColour(frac * 16.0f > (float)kBayer[by][bx] ? c1 : c0);
            g.fillRect(x, y, juce::jmin(cell, r.getRight() - x), juce::jmin(cell, r.getBottom() - y));
        }
    }
}

Look::Look()
{
    setColour(juce::ResizableWindow::backgroundColourId, kFace);
    setColour(juce::PopupMenu::backgroundColourId, kFace);
    setColour(juce::Label::textColourId, kText);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                            juce::Slider& s)
{
    // A low-poly knob: an octagon body with flat-shaded facets (lit from the
    // top left) inside a ring of 15 pixel ticks lit up to the value.
    auto area = juce::Rectangle<int>(x, y, w, h);
    int size = juce::jmin(w, h) & ~1;
    auto c = area.withSizeKeepingCentre(size, size).toFloat();
    float cx = c.getCentreX(), cy = c.getCentreY(), R = size * 0.5f;
    bool bipolar = s.getMinimum() == 0 && s.getMaximum() == 127 && s.getTextFromValue(64.0).trim() == "0";
    const int ticks = 15;
    float centre = (start + end) * 0.5f, value = start + pos * (end - start);
    for (int i = 0; i < ticks; ++i)
    {
        float a = start + (end - start) * (float)i / (float)(ticks - 1);
        bool on = bipolar ? (a >= juce::jmin(centre, value) - 1e-4f && a <= juce::jmax(centre, value) + 1e-4f)
                          : a <= value + 1e-4f;
        float tx = cx + (R - 3.0f) * std::sin(a), ty = cy - (R - 3.0f) * std::cos(a);
        g.setColour(on ? accentFor(s) : kShadow);
        g.fillRect(juce::Rectangle<float>(tx - 2.0f, ty - 2.0f, 4.0f, 4.0f).toNearestInt());
    }
    float r = R - 9.0f;
    juce::Path oct;
    for (int i = 0; i < 8; ++i)
    {
        float a = juce::MathConstants<float>::pi / 8.0f + (float)i * juce::MathConstants<float>::pi / 4.0f;
        juce::Point<float> p(cx + r * std::sin(a), cy - r * std::cos(a));
        i == 0 ? oct.startNewSubPath(p) : oct.lineTo(p);
    }
    oct.closeSubPath();
    g.setColour(kShadow);
    g.fillPath(oct, juce::AffineTransform::translation(2.0f, 2.0f));   // hard drop shadow
    g.setColour(juce::Colour(0xff9a9aa2));
    g.fillPath(oct);
    // facets: each of the eight triangles shaded by its angle to the light
    for (int i = 0; i < 8; ++i)
    {
        float a0 = juce::MathConstants<float>::pi / 8.0f + (float)i * juce::MathConstants<float>::pi / 4.0f;
        float a1 = a0 + juce::MathConstants<float>::pi / 4.0f;
        float mid = (a0 + a1) * 0.5f;
        float light = 0.5f + 0.5f * std::cos(mid + juce::MathConstants<float>::pi / 4.0f);   // light from the top left
        juce::Path tri;
        tri.startNewSubPath(cx, cy);
        tri.lineTo(cx + r * std::sin(a0), cy - r * std::cos(a0));
        tri.lineTo(cx + r * std::sin(a1), cy - r * std::cos(a1));
        tri.closeSubPath();
        g.setColour(juce::Colour(0xff6a6a72).interpolatedWith(juce::Colour(0xffe4e4ea), light));
        g.fillPath(tri);
    }
    g.setColour(juce::Colour(0xffb4b4bc));
    g.fillEllipse(c.withSizeKeepingCentre(r * 1.0f, r * 1.0f));
    // pointer: a chunky pixel line
    for (float d = r * 0.2f; d < r - 1.0f; d += 2.0f)
    {
        float px = cx + d * std::sin(value), py = cy - d * std::cos(value);
        g.setColour(kDark);
        g.fillRect(juce::Rectangle<float>(px - 1.5f, py - 1.5f, 3.0f, 3.0f).toNearestInt());
    }
}

juce::Label* Look::createSliderTextBox(juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return l;
}

juce::Slider::SliderLayout Look::getSliderLayout(juce::Slider& s)
{
    auto layout = LookAndFeel_V4::getSliderLayout(s);
    auto b = s.getLocalBounds();
    layout.textBoxBounds = b.removeFromBottom(20).withSizeKeepingCentre(juce::jmin(84, b.getWidth()), 20);
    layout.sliderBounds = b.reduced(2);
    return layout;
}

void Look::drawLabel(juce::Graphics& g, juce::Label& l)
{
    if (l.isBeingEdited())
        return;
    auto* parent = l.getParentComponent();
    auto r = l.getLocalBounds();
    if (dynamic_cast<juce::Slider*>(parent) != nullptr)
    {
        // value readout: a little black LCD, green pixel text
        sunken(g, r, false);
        g.setColour(kLcd);
        g.fillRect(r.reduced(1));
        drawText(g, l.getText(), r.reduced(3, 0), juce::Justification::centred, kLcdText);
        return;
    }
    if (dynamic_cast<juce::ComboBox*>(parent) != nullptr)
    {
        drawText(g, l.getText(), r.reduced(4, 0), juce::Justification::centredLeft,
                 parent->isEnabled() ? kText : kShadow);
        return;
    }
    auto j = l.getJustificationType();
    drawText(g, l.getText(), r, j, l.findColour(juce::Label::textColourId));
}

void Look::drawComboBox(juce::Graphics& g, int w, int h, bool, int bx, int by, int bw, int bh, juce::ComboBox&)
{
    auto r = juce::Rectangle<int>(0, 0, w, h);
    g.setColour(kField);
    g.fillRect(r);
    sunken(g, r);
    auto btn = juce::Rectangle<int>(bx, by, bw, bh);
    g.setColour(kFace);
    g.fillRect(btn);
    raised(g, btn);
    // a pixel arrow
    int ax = btn.getCentreX() - 4, ay = btn.getCentreY() - 2;
    g.setColour(kDark);
    for (int i = 0; i < 4; ++i)
        g.fillRect(ax + i, ay + i, 8 - 2 * i, 1);
}

void Look::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(2, 2, box.getWidth() - box.getHeight() - 2, box.getHeight() - 4);
}

void Look::drawPopupMenuBackground(juce::Graphics& g, int w, int h)
{
    g.fillAll(kFace);
    raised(g, {0, 0, w, h});
}

void Look::getIdealPopupMenuItemSize(const juce::String& text, bool separator, int, int& w, int& h)
{
    h = separator ? 8 : 22;
    w = textWidth(text) + 40;
}

void Look::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool separator, bool active,
                             bool highlighted, bool ticked, bool, const juce::String& text, const juce::String&,
                             const juce::Drawable*, const juce::Colour*)
{
    auto r = area.reduced(3, 0);
    if (separator)
    {
        g.setColour(kShadow);
        g.fillRect(r.getX(), r.getCentreY(), r.getWidth(), 1);
        g.setColour(kLight);
        g.fillRect(r.getX(), r.getCentreY() + 1, r.getWidth(), 1);
        return;
    }
    if (highlighted && active)
    {
        g.setColour(kNavy);
        g.fillRect(r);
    }
    auto c = !active ? kShadow : highlighted ? kLight : kText;
    if (ticked)
        drawText(g, "*", r.getX() + 4, r.getCentreY() - 7, c);
    drawText(g, text, r.withTrimmedLeft(20), juce::Justification::centredLeft, c);
}

void Look::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds();
    auto box = juce::Rectangle<int>(r.getX() + 2, r.getCentreY() - 8, 16, 16);
    g.setColour(kField);
    g.fillRect(box);
    sunken(g, box);
    if (b.getToggleState())
    {
        // a pixel tick
        static const char* tick[] = {"0000001", "0000011", "1000111", "1101110", "1111100", "0111000", "0010000"};
        g.setColour(kDark);
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x)
                if (tick[y][x] == '1')
                    g.fillRect(box.getX() + 4 + x, box.getY() + 4 + y, 1, 1);
    }
    drawText(g, b.getButtonText(), r.withTrimmedLeft(24), juce::Justification::centredLeft,
             b.isEnabled() ? kText : kShadow);
}

void Look::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool, bool down)
{
    auto r = b.getLocalBounds();
    bool on = b.getToggleState();
    if (b.getProperties()["tab"])
    {
        // a Win95 tab: the selected one stands taller and opens into the
        // panel below, the others sit back and lose their bottom row
        if (!on)
            r = r.withTrimmedTop(3).withTrimmedBottom(4);
        g.setColour(kFace);
        g.fillRect(r);
        g.setColour(kLight);
        g.fillRect(r.getX(), r.getY() + 2, 1, r.getHeight() - 2);
        g.fillRect(r.getX() + 2, r.getY(), r.getWidth() - 4, 1);
        g.fillRect(r.getX() + 1, r.getY() + 1, 1, 1);
        g.setColour(kHilite);
        g.fillRect(r.getX() + 1, r.getY() + 2, 1, r.getHeight() - 2);
        g.fillRect(r.getX() + 2, r.getY() + 1, r.getWidth() - 4, 1);
        g.setColour(kDark);
        g.fillRect(r.getRight() - 1, r.getY() + 2, 1, r.getHeight() - 2);
        g.fillRect(r.getRight() - 2, r.getY() + 1, 1, 1);
        g.setColour(kShadow);
        g.fillRect(r.getRight() - 2, r.getY() + 2, 1, r.getHeight() - 2);
        return;
    }
    g.setColour(on ? kHilite : kFace);
    g.fillRect(r);
    if (down || on)
        sunken(g, r, !b.getProperties()["caption"]);
    else
        raised(g, r, !b.getProperties()["caption"]);
}

void Look::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    auto r = b.getLocalBounds();
    bool tab = b.getProperties()["tab"], on = b.getToggleState();
    if (tab && !on)
        r = r.withTrimmedTop(3).withTrimmedBottom(4);
    else if (down || (on && !tab))
        r.translate(1, 1);
    auto text = b.getButtonText();
    int w = textWidth(text) + (tab ? 14 : 0);
    auto t = r.withSizeKeepingCentre(w, textHeight());
    if (tab)
    {
        // the track's colour chip, N64 style
        g.setColour(accentFor(b));
        g.fillRect(t.getX(), t.getY() + 2, 8, 8);
        if (on)
        {
            g.setColour(kDark);
            g.drawRect(t.getX() - 1, t.getY() + 1, 10, 10, 1);
        }
        t.removeFromLeft(14);
    }
    drawText(g, text, t.getX(), t.getY(), kText);
}
} // namespace retro
