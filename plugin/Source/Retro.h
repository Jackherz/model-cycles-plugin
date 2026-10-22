// The retro look: Windows 95 chrome, PS1/N64 pixel type, low-poly knobs,
// ordered dithering. Everything is drawn in code (a 5x7 bitmap font of our
// own, no font files).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace retro
{
// The colour scheme: Windows 95 greys by default. These are shared and set
// by applyTheme (one of five schemes, each derived from its background
// colour); drawing reads them at paint time.
inline juce::Colour kFace(0xffc0c0c0), kLight(0xffffffff), kShadow(0xff808080), kDark(0xff000000),
    kHilite(0xffdfdfdf), kNavy(0xff000080), kTitleEnd(0xff1084d0), kDesktop(0xff008080),
    kField(0xffffffff), kText(0xff000000), kDimText(0xff505050),
    kTitleText(0xffffffff), kSelect(0xff000080);   // text on the title bar / a selection
// The N64 logo's four colours and two more: the tracks' own, in every scheme.
const juce::Colour kAccents[] = {juce::Colour(0xffe2231a), juce::Colour(0xfff6b800), juce::Colour(0xff009e49),
                                 juce::Colour(0xff0057b7), juce::Colour(0xff7a3fbf), juce::Colour(0xffe2231a),
                                 juce::Colour(0xff4a4a4a)};   // tracks 1-6, FX

// Metal Gear Solid's Codec: phosphor green on near-black, in a gunmetal
// bevel, with grey label plates.
inline juce::Colour kCodecBg(0xff040d07), kPhosphor(0xff8dffb2), kPhosphorDim(0xff2f7a4c),
    kPhosphorFaint(0xff12321f), kGunmetal(0xff3a403c), kGunLight(0xff6c766f), kGunDark(0xff141816),
    kPlate(0xff9aa39c), kPlateText(0xff1c221e);
// the LCDs (readouts, selected icons, the LFO display) are Codec screens
inline juce::Colour kLcd = kCodecBg, kLcdText = kPhosphor;

// The five schemes: Grey (Win95's own), Desert, Lilac, Slate, Midnight.
constexpr int kNumThemes = 5;
extern const char* const kThemeNames[kNumThemes];
void applyTheme(int index);
int currentTheme();

constexpr int kPx = 2;   // one font pixel in screen pixels

// 5x7 bitmap type, upper case (lower case maps to it).
int textWidth(const juce::String& s, int scale = kPx);
inline int textHeight(int scale = kPx) { return 7 * scale; }
void drawText(juce::Graphics&, const juce::String& s, int x, int y, juce::Colour, int scale = kPx);
void drawText(juce::Graphics&, const juce::String& s, juce::Rectangle<int> area, juce::Justification,
              juce::Colour, int scale = kPx);

// Win95 3D edges: raised (buttons, panels) or sunken (fields, LCDs).
void raised(juce::Graphics&, juce::Rectangle<int>, bool thick = true);
void sunken(juce::Graphics&, juce::Rectangle<int>, bool thick = true);
// An etched group box with its title cut into the top edge.
void groupBox(juce::Graphics&, juce::Rectangle<int>, const juce::String& title, juce::Colour accent);
// Word-wrapped pixel text, top-left aligned.
void drawWrapped(juce::Graphics&, const juce::String& s, juce::Rectangle<int> area, juce::Colour, int scale = kPx);
int wrappedHeight(const juce::String& s, int width, int scale = kPx);
// WCAG contrast ratio of two colours (1..21).
float contrast(juce::Colour a, juce::Colour b);
// c darkened (on a light bg) or lightened (on a dark one) until its contrast
// with bg reaches minRatio: a track colour that still reads on any scheme.
juce::Colour readableOn(juce::Colour c, juce::Colour bg, float minRatio = 3.0f);
// The "accent" property of c or its nearest ancestor that has one.
juce::Colour accentFor(const juce::Component& c);
// Darken every other row of `in` (the Codec's scanlines).
void scanlines(juce::Graphics&, juce::Rectangle<int> in);
// A Codec screen: gunmetal bevel (`frame` px), near-black inside, a
// scanline every other row. Returns the inside.
juce::Rectangle<int> codecScreen(juce::Graphics&, juce::Rectangle<int>, int frame = 3);
// A grey label plate ("PTT", "MEMORY"); lit draws it phosphor on black.
void labelPlate(juce::Graphics&, juce::Rectangle<int>, const juce::String& text, bool lit = false);
// Seven-segment digits ("120.00"), blocky; digit height h, centred in area.
void sevenSeg(juce::Graphics&, const juce::String& text, juce::Rectangle<int> area, int h, juce::Colour on,
              juce::Colour off);
int sevenSegWidth(const juce::String& text, int h);
// Signal bars rising left to right, lit up to level (0..1).
void signalBars(juce::Graphics&, juce::Rectangle<int>, int bars, float level, juce::Colour on, juce::Colour off);
// A gradient from a to b, left to right, through a 4x4 Bayer dither in
// blocks of `cell` pixels (the PS1's look).
void ditherGradient(juce::Graphics&, juce::Rectangle<int>, juce::Colour a, juce::Colour b, int cell = kPx);

// A value readout you can type into as on the old computer itself: click
// it and it becomes a lit Codec prompt with the pixel font and a blinking
// block cursor. Return enters the value (through the slider's own text
// parsing), Escape cancels, clicking away enters. No system text box.
class PixelEntry : public juce::Label, private juce::Timer
{
public:
    PixelEntry();
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override {}
    void mouseDoubleClick(const juce::MouseEvent&) override {}
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(FocusChangeType) override {}
    void focusLost(FocusChangeType) override;
    bool entering() const { return active; }
    void begin();   // what a click does

private:
    void timerCallback() override;
    void finish(bool commit);

    bool active = false, cursorOn = true;
    juce::String typed;
};

class Look : public juce::LookAndFeel_V4
{
public:
    Look();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout(juce::Slider&) override;
    void drawLabel(juce::Graphics&, juce::Label&) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh,
                      juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool separator, bool active,
                           bool highlighted, bool ticked, bool hasSub, const juce::String& text,
                           const juce::String& shortcut, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize(const juce::String& text, bool separator, int standardHeight, int& w,
                                   int& h) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
};
} // namespace retro
