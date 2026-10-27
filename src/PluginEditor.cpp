#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    juce::Image img (const char* data, int size) { return juce::ImageCache::getFromMemory (data, size); }
    constexpr float kHalfSweep = juce::MathConstants<float>::pi * 0.75f;   // +/-135 degrees
}

//==============================================================================
ImageKnob::ImageKnob (juce::Image im) : image (std::move (im))
{
    setSliderStyle (RotaryHorizontalVerticalDrag);
    setTextBoxStyle (NoTextBox, true, 0, 0);
    setMouseDragSensitivity (220);
    setPopupDisplayEnabled (true, false, nullptr);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ImageKnob::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float prop = (float) valueToProportionOfLength (getValue());
    const float angle = -kHalfSweep + 2.0f * kHalfSweep * prop;
    const float sc = b.getWidth() / (float) image.getWidth();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImageTransformed (image, juce::AffineTransform::scale (sc)
                                       .translated (b.getX(), b.getY())
                                       .rotated (angle, b.getCentreX(), b.getCentreY()));
}

//==============================================================================
StripSelector::StripSelector (juce::Image s, int frames) : strip (std::move (s)), numFrames (frames)
{
    setSliderStyle (RotaryHorizontalVerticalDrag);
    setTextBoxStyle (NoTextBox, true, 0, 0);
    setMouseDragSensitivity (120);
    setPopupDisplayEnabled (true, false, nullptr);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void StripSelector::paint (juce::Graphics& g)
{
    const int fw = strip.getWidth();
    const int frame = juce::jlimit (0, numFrames - 1, juce::roundToInt (valueToProportionOfLength (getValue()) * (numFrames - 1)));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (strip, 0, 0, getWidth(), getHeight(), 0, frame * fw, fw, fw);
}

//==============================================================================
Footswitch::Footswitch (juce::Image released, juce::Image pressed)
    : juce::Button ({}), up (std::move (released)), down (std::move (pressed))
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Footswitch::paintButton (juce::Graphics& g, bool, bool isDown)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const auto& im = (getToggleState() || isDown) ? down : up;
    g.drawImage (im, getLocalBounds().toFloat());
}

//==============================================================================
Led::Led (juce::Image off, juce::Image on, std::function<bool()> state)
    : offImg (std::move (off)), onImg (std::move (on)), isOn (std::move (state))
{
    setInterceptsMouseClicks (false, false);
    lit = isOn();
}

void Led::refresh()
{
    const bool now = isOn();
    if (now != lit) { lit = now; repaint(); }
}

void Led::paint (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (lit ? onImg : offImg, getLocalBounds().toFloat());
}

//==============================================================================
void ParametricFuzzEditor::Panel::paint (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (background, getLocalBounds().toFloat());
}

void ParametricFuzzEditor::Panel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) owner.showSizeMenu();
}

ParametricFuzzEditor::ParametricFuzzEditor (ParametricFuzzProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      gain (img (BinaryData::knob_png, BinaryData::knob_pngSize)),
      height (img (BinaryData::knob_png, BinaryData::knob_pngSize)),
      freq (img (BinaryData::knob_png, BinaryData::knob_pngSize)),
      trim (img (BinaryData::trim_png, BinaryData::trim_pngSize)),
      bandwidth (img (BinaryData::bandwidth_png, BinaryData::bandwidth_pngSize), 5),
      channel (img (BinaryData::footswitch_released_png, BinaryData::footswitch_released_pngSize),
               img (BinaryData::footswitch_pressed_png, BinaryData::footswitch_pressed_pngSize)),
      bypass (img (BinaryData::footswitch_released_png, BinaryData::footswitch_released_pngSize),
              img (BinaryData::footswitch_pressed_png, BinaryData::footswitch_pressed_pngSize)),
      channelLed (img (BinaryData::led_green_off_png, BinaryData::led_green_off_pngSize),
                  img (BinaryData::led_green_on_png, BinaryData::led_green_on_pngSize),
                  [&p] { return p.apvts.getRawParameterValue ("channel")->load() > 0.5f; }),
      bypassLed (img (BinaryData::led_blue_off_png, BinaryData::led_blue_off_pngSize),
                 img (BinaryData::led_blue_on_png, BinaryData::led_blue_on_pngSize),
                 [&p] { return p.apvts.getRawParameterValue ("bypass")->load() < 0.5f; })
{
    panel.background = img (BinaryData::panel_jpg, BinaryData::panel_jpgSize);
    addAndMakeVisible (panel);

    auto place = [this] (juce::Component& c, int x, int y, int w, int h, const char* tip)
    {
        panel.addAndMakeVisible (c);
        c.setBounds (x, y, w, h);
        if (auto* tc = dynamic_cast<juce::SettableTooltipClient*> (&c)) tc->setTooltip (tip);
        c.setTitle (tip);
    };
    // Positions from the art package manifest (logical 600 x 1000)
    place (gain,       236, 103, 128, 128, "Gain");
    place (height,      58, 251, 128, 128, "Height (cut / boost, noon = flat)");
    place (freq,       414, 251, 128, 128, "Frequency");
    place (bandwidth,  220, 310, 160, 160, "Bandwidth (narrow - wide)");
    place (trim,       264, 764,  72,  72, "Output Trim (noon = 0 dB)");
    place (channel,     76, 756,  88,  88, "Clean / Dirty");
    place (bypass,     436, 756,  88,  88, "Bypass");
    place (channelLed, 100, 670,  40,  40, "");
    place (bypassLed,  460, 670,  40,  40, "");

    auto& s = proc.apvts;
    gainA = std::make_unique<SA> (s, "gain", gain);
    heightA = std::make_unique<SA> (s, "height", height);
    freqA = std::make_unique<SA> (s, "freq", freq);
    trimA = std::make_unique<SA> (s, "trim", trim);
    bwA = std::make_unique<SA> (s, "bandwidth", bandwidth);
    channelA = std::make_unique<BA> (s, "channel", channel);
    bypassA = std::make_unique<BA> (s, "bypass", bypass);

    gain.setDoubleClickReturnValue (true, 5.0);
    height.setDoubleClickReturnValue (true, 5.0);
    freq.setDoubleClickReturnValue (true, 5.0);
    trim.setDoubleClickReturnValue (true, 0.0);
    bandwidth.setDoubleClickReturnValue (true, 2.0);

    applyScale (proc.uiScale.load());
    startTimerHz (30);
}

ParametricFuzzEditor::~ParametricFuzzEditor() { stopTimer(); }

void ParametricFuzzEditor::applyScale (float sc)
{
    sc = juce::jlimit (0.4f, 2.0f, sc);
    proc.uiScale = sc;
    panel.setBounds (0, 0, kWidth, kHeight);
    panel.setTransform (juce::AffineTransform::scale (sc));
    setSize (juce::roundToInt (kWidth * sc), juce::roundToInt (kHeight * sc));
}

void ParametricFuzzEditor::showSizeMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Window size");
    const float current = proc.uiScale.load();
    for (float s : { 0.5f, 0.6f, 0.75f, 0.9f, 1.0f, 1.25f })
        m.addItem (juce::String (juce::roundToInt (s * 100)) + "%", true, std::abs (s - current) < 0.01f,
                   [this, s] { applyScale (s); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&panel));
}

void ParametricFuzzEditor::timerCallback()
{
    channelLed.refresh();
    bypassLed.refresh();
}
