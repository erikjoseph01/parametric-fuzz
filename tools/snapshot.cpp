// Renders the plugin editor to PNG files so the skin can be checked without a DAW.
#include "PluginProcessor.h"
#include "PluginEditor.h"

static void shot (ParametricFuzzProcessor& p, const juce::File& f)
{
    p.uiScale = 1.0f;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto im = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat().writeImageToStream (im, out);
    std::printf ("wrote %s (%d x %d)\n", f.getFullPathName().toRawUTF8(), im.getWidth(), im.getHeight());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshots");
    dir.createDirectory();
    ParametricFuzzProcessor p;
    auto set = [&] (const char* id, float v) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };

    shot (p, dir.getChildFile ("default.png"));
    set ("gain", 0.0f); set ("height", 10.0f); set ("freq", 2.5f); set ("bandwidth", 0.0f);
    set ("trim", -18.0f); set ("channel", 0.0f); set ("bypass", 1.0f);
    shot (p, dir.getChildFile ("min_clean_bypassed.png"));
    set ("gain", 10.0f); set ("height", 0.0f); set ("freq", 10.0f); set ("bandwidth", 4.0f);
    set ("trim", 18.0f); set ("channel", 1.0f); set ("bypass", 0.0f);
    shot (p, dir.getChildFile ("max_dirty_active.png"));
    return 0;
}
