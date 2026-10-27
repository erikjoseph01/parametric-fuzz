#pragma once
#include "PluginProcessor.h"
#include <functional>

// Rotary knob drawn from a single upright image, rotated through 270 degrees.
class ImageKnob : public juce::Slider
{
public:
    explicit ImageKnob (juce::Image im);
    void paint (juce::Graphics&) override;
private:
    juce::Image image;
};

// Five-position selector drawn from a vertical filmstrip (frame 0 = narrow).
class StripSelector : public juce::Slider
{
public:
    StripSelector (juce::Image strip, int frames);
    void paint (juce::Graphics&) override;
private:
    juce::Image strip;
    int numFrames;
};

// Latching footswitch: pressed image when on.
class Footswitch : public juce::Button
{
public:
    Footswitch (juce::Image released, juce::Image pressed);
    void paintButton (juce::Graphics&, bool, bool) override;
private:
    juce::Image up, down;
};

class Led : public juce::Component
{
public:
    Led (juce::Image off, juce::Image on, std::function<bool()> state);
    void paint (juce::Graphics&) override;
    void refresh();
private:
    juce::Image offImg, onImg;
    std::function<bool()> isOn;
    bool lit = false;
};

class ParametricFuzzEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ParametricFuzzEditor (ParametricFuzzProcessor&);
    ~ParametricFuzzEditor() override;
    void resized() override {}

    static constexpr int kWidth = 600, kHeight = 1000;

private:
    struct Panel : public juce::Component
    {
        explicit Panel (ParametricFuzzEditor& e) : owner (e) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        ParametricFuzzEditor& owner;
        juce::Image background;
    };

    void timerCallback() override;
    void applyScale (float s);
    void showSizeMenu();

    ParametricFuzzProcessor& proc;
    Panel panel { *this };
    ImageKnob gain, height, freq, trim;
    StripSelector bandwidth;
    Footswitch channel, bypass;
    Led channelLed, bypassLed;
    juce::TooltipWindow tooltips { this, 600 };

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> gainA, heightA, freqA, trimA, bwA;
    std::unique_ptr<BA> channelA, bypassA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParametricFuzzEditor)
};
