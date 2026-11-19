#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>

namespace col
{
inline const juce::Colour bg        { 0xff080808 };
inline const juce::Colour panel     { 0xff1a1a1b };
inline const juce::Colour panelDark { 0xff0f0f10 };
inline const juce::Colour line      { 0xff2b2b2d };
inline const juce::Colour text      { 0xffc4c4c4 };
inline const juce::Colour dim       { 0xff6f6f72 };
inline const juce::Colour mustard   { 0xffe3a800 };
inline const juce::Colour mustardHi { 0xfff7c82a };
inline const juce::Colour mustardLo { 0xff8f6b00 };
}

enum class Fmt { Plain, Percent, Db, Hz, Sec, Gate };

inline void applyFormat (juce::Slider& s, Fmt f)
{
    s.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
    s.textFromValueFunction = [f] (double v) -> juce::String
    {
        switch (f)
        {
            case Fmt::Percent: return juce::String (juce::roundToInt (v * 100.0)) + "%";
            case Fmt::Db:      return juce::String (v, 1) + " dB";
            case Fmt::Hz:      return v >= 1000.0 ? juce::String (v / 1000.0, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
            case Fmt::Sec:     return juce::String (v, 1) + " s";
            case Fmt::Gate:    return v <= -89.5 ? juce::String ("OFF") : juce::String (v, 0) + " dB";
            default:           return juce::String (v, 2);
        }
    };
}

//==============================================================================
class MustardLNF : public juce::LookAndFeel_V4
{
public:
    MustardLNF()
    {
        setColour (juce::Slider::rotarySliderFillColourId, col::mustard);
        setColour (juce::Slider::thumbColourId, col::mustard);
        setColour (juce::Slider::textBoxTextColourId, col::text);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, col::text);
        setColour (juce::TextButton::buttonColourId, col::panelDark);
        setColour (juce::TextButton::buttonOnColourId, col::mustardLo);
        setColour (juce::TextButton::textColourOffId, col::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ComboBox::backgroundColourId, col::panelDark);
        setColour (juce::ComboBox::textColourId, col::mustardHi);
        setColour (juce::ComboBox::outlineColourId, col::line);
        setColour (juce::ComboBox::arrowColourId, col::mustard);
        setColour (juce::PopupMenu::backgroundColourId, col::panelDark);
        setColour (juce::PopupMenu::textColourId, col::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::mustardLo);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::BubbleComponent::backgroundColourId, juce::Colours::black);
        setColour (juce::BubbleComponent::outlineColourId, col::mustard);
        setColour (juce::TooltipWindow::textColourId, col::mustardHi);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startA, float endA, juce::Slider& s) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float angle = startA + pos * (endA - startA);
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
        const float th = juce::jmax (2.0f, r * 0.07f);

        juce::Path track;
        track.addCentredArc (c.x, c.y, r, r, 0.0f, startA, endA, true);
        g.setColour (juce::Colour (0xff2c2c2e));
        g.strokePath (track, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path val;
        val.addCentredArc (c.x, c.y, r, r, 0.0f, startA, angle, true);
        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (val, juce::PathStrokeType (th + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (val, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float br = r - th - 4.0f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d3d40), c.x, c.y - br, juce::Colour (0xff131315), c.x, c.y + br, false));
        g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
        g.setColour (juce::Colour (0xff050505));
        g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.5f);

        const float dr = br - juce::jmax (6.0f, br * 0.22f);
        const float dx = c.x + std::sin (angle) * dr, dy = c.y - std::cos (angle) * dr;
        const float ds = juce::jmax (4.0f, br * 0.14f);
        g.setColour (accent);
        g.fillEllipse (dx - ds, dy - ds, ds * 2.0f, ds * 2.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           const juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        const auto accent = s.findColour (juce::Slider::thumbColourId);
        const bool vertical = (style == juce::Slider::LinearVertical);
        const bool isGate = (bool) s.getProperties()["gate"];

        if (vertical && isGate)
        {
            g.setColour (accent);
            g.fillRoundedRectangle ((float) x, pos - 2.5f, (float) w, 5.0f, 2.0f);
            return;
        }

        if (vertical)
        {
            const float cx = x + w * 0.5f;
            g.setColour (juce::Colour (0xff070708));
            g.fillRoundedRectangle (cx - 3.0f, (float) y, 6.0f, (float) h, 3.0f);
            g.setColour (col::line);
            g.drawRoundedRectangle (cx - 3.0f, (float) y, 6.0f, (float) h, 3.0f, 1.0f);
            drawThumb (g, cx, pos, accent);
        }
        else
        {
            const float cy = y + h * 0.5f;
            g.setColour (juce::Colour (0xff070708));
            g.fillRoundedRectangle ((float) x, cy - 3.0f, (float) w, 6.0f, 3.0f);
            g.setColour (accent.withAlpha (0.55f));
            g.fillRoundedRectangle ((float) x, cy - 3.0f, pos - (float) x, 6.0f, 3.0f);
            drawThumb (g, pos, cy, accent);
        }
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (13.0f, juce::Font::bold)); }

private:
    static void drawThumb (juce::Graphics& g, float cx, float cy, juce::Colour accent)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d3d40), cx, cy - 8.0f, juce::Colour (0xff131315), cx, cy + 8.0f, false));
        g.fillEllipse (cx - 8.0f, cy - 8.0f, 16.0f, 16.0f);
        g.setColour (accent);
        g.drawEllipse (cx - 8.0f, cy - 8.0f, 16.0f, 16.0f, 1.6f);
    }
};

//==============================================================================
class PowerButton : public juce::ToggleButton
{
public:
    juce::Colour accent = col::mustard;

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (5.0f);
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 1.0f;
        const auto c = r.getCentre();
        auto colr = getToggleState() ? accent : col::dim;
        if (over) colr = colr.brighter (0.25f);

        juce::Path p;
        p.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
        if (getToggleState())
        {
            g.setColour (colr.withAlpha (0.25f));
            g.strokePath (p, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (colr);
        g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawLine (c.x, c.y - rad - 0.5f, c.x, c.y - 1.0f, 2.2f);
    }
};

//==============================================================================
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& labelText,
          Fmt fmt, bool textBox = false)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setTextBoxStyle (textBox ? juce::Slider::TextBoxBelow : juce::Slider::NoTextBox, true, 80, 16);
        applyFormat (slider, fmt);
        if (! textBox) slider.setPopupDisplayEnabled (true, true, nullptr);
        addAndMakeVisible (slider);

        attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, slider);
        if (auto* prm = apvts.getParameter (id))
            slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        slider.updateText();

        label.setText (labelText, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        label.setColour (juce::Label::textColourId, col::text);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromBottom (16));
        slider.setBounds (r);
    }

    juce::Slider slider;
    juce::Label label;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
};

//==============================================================================
class Fader : public juce::Component
{
public:
    Fader (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, const juce::String& labelText, Fmt fmt)
    {
        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        applyFormat (slider, fmt);
        slider.setPopupDisplayEnabled (true, true, nullptr);
        addAndMakeVisible (slider);
        attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, slider);
        if (auto* prm = apvts.getParameter (id))
            slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));

        label.setText (labelText, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        label.setColour (juce::Label::textColourId, col::text);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromBottom (18));
        slider.setBounds (r);
    }

    juce::Slider slider;
    juce::Label label;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
};

//==============================================================================
// Medidor segmentado. El getter devuelve un valor normalizado 0..1.
class LevelBar : public juce::Component, private juce::Timer
{
public:
    LevelBar (std::function<float()> getter, bool fromTop = false, bool horizontal = false)
        : get (std::move (getter)), top (fromTop), horiz (horizontal)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0a0a0b));
        g.fillRoundedRectangle (r, 3.0f);
        r = r.reduced (2.0f);

        const float seg = 4.0f, gap = 1.5f;
        const int num = juce::jmax (1, (int) ((horiz ? r.getWidth() : r.getHeight()) / (seg + gap)));
        const int lit = juce::roundToInt (shown * (float) num);

        for (int i = 0; i < num; ++i)
        {
            bool on;
            float frac;
            if (horiz) { on = i < lit; frac = (float) i / (float) num; }
            else if (top) { on = i < lit; frac = (float) i / (float) num; }
            else { on = i >= num - lit; frac = 1.0f - (float) i / (float) num; }

            juce::Colour c = on ? col::mustard : juce::Colour (0xff1c1c1e);
            if (on && ! top && frac > 0.88f) c = juce::Colour (0xffff6a3d);

            if (horiz) g.setColour (c), g.fillRect (r.getX() + (float) i * (seg + gap), r.getY(), seg, r.getHeight());
            else       g.setColour (c), g.fillRect (r.getX(), r.getY() + (float) i * (seg + gap), r.getWidth(), seg);
        }
    }

private:
    void timerCallback() override
    {
        const float v = juce::jlimit (0.0f, 1.0f, get());
        shown = v > shown ? v : shown * 0.9f + v * 0.1f;
        repaint();
    }

    std::function<float()> get;
    bool top, horiz;
    float shown = 0.0f;
};
