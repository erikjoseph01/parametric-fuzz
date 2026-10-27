#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/PdfCircuit.h"
#include <array>
#include <memory>

class ParametricFuzzProcessor : public juce::AudioProcessor
{
public:
    ParametricFuzzProcessor();
    ~ParametricFuzzProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::array<pf::PdfCircuit, 2> circuits;
    juce::SmoothedValue<double> gainS, freqS, heightS;
    juce::SmoothedValue<float> trimS;
    bool lastDirty = false;
    float switchRamp = 1.0f;

    std::atomic<float>* pChannel = nullptr;
    std::atomic<float>* pGain = nullptr;
    std::atomic<float>* pFreq = nullptr;
    std::atomic<float>* pHeight = nullptr;
    std::atomic<float>* pBandwidth = nullptr;
    std::atomic<float>* pTrim = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParametricFuzzProcessor)
};
