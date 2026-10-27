#include "PluginProcessor.h"

namespace
{
    // Knobs are shown 0-10 like the hardware; the circuit wants 0-1 rotation.
    juce::String knobText (float v, int) { return juce::String (v, 1); }
}

juce::AudioProcessorValueTreeState::ParameterLayout ParametricFuzzProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "channel", 1 }, "Channel", StringArray { "Clean", "Dirty" }, 1));
    auto knob = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 10.0f, 0.01f), def,
                                                      AudioParameterFloatAttributes().withStringFromValueFunction (knobText));
    };
    l.add (knob ("gain", "Gain", 5.0f));
    l.add (knob ("freq", "Frequency", 5.0f));
    l.add (knob ("height", "Height", 5.0f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "bandwidth", 1 }, "Bandwidth", StringArray { "1", "2", "3", "4", "5" }, 2));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "trim", 1 }, "Output Trim", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    return l;
}

ParametricFuzzProcessor::ParametricFuzzProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pChannel = apvts.getRawParameterValue ("channel");
    pGain = apvts.getRawParameterValue ("gain");
    pFreq = apvts.getRawParameterValue ("freq");
    pHeight = apvts.getRawParameterValue ("height");
    pBandwidth = apvts.getRawParameterValue ("bandwidth");
    pTrim = apvts.getRawParameterValue ("trim");
}

bool ParametricFuzzProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void ParametricFuzzProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Run the circuit at roughly 176-192 kHz to keep the fuzz free of aliasing.
    const int factorLog2 = sampleRate <= 50000.0 ? 2 : (sampleRate <= 100000.0 ? 1 : 0);
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (2, (size_t) factorLog2,
                                                                     juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples (juce::roundToInt (oversampler->getLatencyInSamples()));

    const double osRate = sampleRate * (double) (1 << factorLog2);
    for (auto& c : circuits) c.prepare (osRate);

    gainS.reset (osRate, 0.03);
    freqS.reset (osRate, 0.03);
    heightS.reset (osRate, 0.03);
    trimS.reset (sampleRate, 0.05);
    gainS.setCurrentAndTargetValue (*pGain / 10.0);
    freqS.setCurrentAndTargetValue (*pFreq / 10.0);
    heightS.setCurrentAndTargetValue (*pHeight / 10.0);
    trimS.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pTrim->load()));
    lastDirty = *pChannel > 0.5f;
    switchRamp = 1.0f;
}

void ParametricFuzzProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, numSamples);
    if (numIn == 0 || numSamples == 0) return;

    const int procCh = juce::jmin (numIn, 2);
    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) procCh, (size_t) numSamples);
    auto os = oversampler->processSamplesUp (block);
    const int osN = (int) os.getNumSamples();
    const double osRate = getSampleRate() * (double) oversampler->getOversamplingFactor();

    const bool dirty = *pChannel > 0.5f;
    if (dirty != lastDirty) { lastDirty = dirty; switchRamp = 0.0f; }
    const float rampInc = (float) (1.0 / (0.005 * osRate));

    gainS.setTargetValue (*pGain / 10.0);
    freqS.setTargetValue (*pFreq / 10.0);
    heightS.setTargetValue (*pHeight / 10.0);
    const int bw = juce::roundToInt (pBandwidth->load()) + 1;

    constexpr int kCtl = 16;
    for (int start = 0; start < osN; start += kCtl)
    {
        const int len = juce::jmin (kCtl, osN - start);
        pf::Params p;
        p.dirty = dirty;
        p.gain = gainS.getNextValue();
        p.freq = freqS.getNextValue();
        p.height = heightS.getNextValue();
        p.bandwidth = bw;
        gainS.skip (len - 1); freqS.skip (len - 1); heightS.skip (len - 1);
        for (int ch = 0; ch < procCh; ++ch) circuits[(size_t) ch].setParams (p);

        float ramp = switchRamp;
        for (int ch = 0; ch < procCh; ++ch)
        {
            auto* d = os.getChannelPointer ((size_t) ch) + start;
            ramp = switchRamp;
            for (int n = 0; n < len; ++n)
            {
                // sample value 1.0 == 1 volt at the jacks
                const double y = circuits[(size_t) ch].process ((double) d[n]);
                d[n] = (float) y * ramp;
                ramp = juce::jmin (1.0f, ramp + rampInc);
            }
        }
        switchRamp = ramp;
    }

    oversampler->processSamplesDown (block);

    trimS.setTargetValue (juce::Decibels::decibelsToGain (pTrim->load()));
    for (int n = 0; n < numSamples; ++n)
    {
        const float g = trimS.getNextValue();
        for (int ch = 0; ch < procCh; ++ch) buffer.getWritePointer (ch)[n] *= g;
    }
    if (procCh == 1 && numOut > 1) buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
}

juce::AudioProcessorEditor* ParametricFuzzProcessor::createEditor()
{
    // Placeholder UI: a custom skin comes later.
    return new juce::GenericAudioProcessorEditor (*this);
}

void ParametricFuzzProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void ParametricFuzzProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ParametricFuzzProcessor(); }
