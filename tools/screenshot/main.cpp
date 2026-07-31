/*
    moe_screenshot — renders the plugin's editor to a PNG without a DAW.

    This is not a mock-up generator: it instantiates the real
    `MyOrchestralProcessor`, loads real banks into real sections, pushes real
    MIDI through `processBlock` so the meters have something to show, then asks
    the real editor for a snapshot. What comes out is what the plugin looks
    like.

    Useful for documentation, for reviewing a UI change without opening a host,
    and — since it runs under a virtual display — for catching layout
    regressions in CI.

    Usage:
        moe_screenshot --out shot.png [--bank a.sfz --bank b.sfz ...]
                       [--width 1180] [--height 760]
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

namespace
{

/** Plays a spread chord across the loaded sections and runs enough audio for
    the meters to settle, pumping the message loop so the UI timers fire. */
void warmUp (moe::plugin::MyOrchestralProcessor& processor, int numSections)
{
    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;

    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer         midi;

    // One voicing per desk, low to high, so every meter shows a different level.
    static const int voicing[] = { 40, 47, 52, 59, 64, 67, 71, 76 };

    for (int section = 0; section < numSections; ++section)
    {
        const int note = voicing[section % (sizeof (voicing) / sizeof (voicing[0]))];
        midi.addEvent (juce::MidiMessage::noteOn (section + 1, note, 0.85f), 0);
        midi.addEvent (juce::MidiMessage::controllerEvent (section + 1, 1, 96), 0);
    }

    for (int block = 0; block < 60; ++block)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        midi.clear();

        // Let the editor's timers run between blocks; the meters are driven by
        // them, not by the audio callback.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (12);
    }
}

/** Waits for the background bank loads to finish, or gives up. */
bool waitForLoading (moe::plugin::MyOrchestralProcessor& processor, int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);

    while (processor.isLoading() && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

    return ! processor.isLoading();
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    juce::String            outputPath = "myorchestral.png";
    juce::StringArray       bankPaths;
    int                     width = 1180, height = 760;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String argument (argv[i]);
        auto next = [&] { return (i + 1 < argc) ? juce::String (argv[++i]) : juce::String(); };

        if      (argument == "--out")    outputPath = next();
        else if (argument == "--bank")   bankPaths.add (next());
        else if (argument == "--width")  width = next().getIntValue();
        else if (argument == "--height") height = next().getIntValue();
    }

    std::unique_ptr<juce::AudioProcessor> processorBase (createPluginFilter());
    auto* processor = dynamic_cast<moe::plugin::MyOrchestralProcessor*> (processorBase.get());

    if (processor == nullptr)
    {
        std::cerr << "could not create the processor\n";
        return 1;
    }

    // Load the banks before the editor exists, so it opens on a populated
    // orchestra exactly as it would after restoring a session.
    int loadedSections = 0;

    for (int index = 0; index < bankPaths.size(); ++index)
    {
        const juce::File bank (bankPaths[index]);
        if (! bank.existsAsFile())
        {
            std::cerr << "skipping missing bank: " << bank.getFullPathName() << "\n";
            continue;
        }

        bool finished = false;
        processor->loadBankAsync (index, bank, [&finished] (bool ok, juce::String message)
        {
            if (! ok)
                std::cerr << "load failed: " << message << "\n";
            finished = true;
        });

        if (! waitForLoading (*processor, 60000))
            std::cerr << "timed out loading " << bank.getFileName() << "\n";

        while (! finished)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

        processor->setSectionMidiChannel (index, index);
        ++loadedSections;
    }

    // Owned by us, but the processor keeps a back-pointer that has to be
    // cleared before the editor goes away.
    auto* editor = processor->createEditorIfNeeded();
    if (editor == nullptr)
    {
        std::cerr << "the processor produced no editor\n";
        return 2;
    }

    editor->setSize (width, height);

    // The editor is never added to a desktop window: `createComponentSnapshot`
    // paints it into an image directly, which is why this works headless.
    juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

    warmUp (*processor, juce::jmax (1, loadedSections));

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), false);

    if (! image.isValid())
    {
        std::cerr << "snapshot failed\n";
        return 3;
    }

    const juce::File output (juce::File::getCurrentWorkingDirectory().getChildFile (outputPath));
    output.deleteFile();

    juce::FileOutputStream stream (output);
    if (! stream.openedOk())
    {
        std::cerr << "cannot write " << output.getFullPathName() << "\n";
        return 4;
    }

    juce::PNGImageFormat png;
    if (! png.writeImageToStream (image, stream))
    {
        std::cerr << "PNG encoding failed\n";
        return 5;
    }

    std::cout << "wrote " << output.getFullPathName()
              << " (" << image.getWidth() << "x" << image.getHeight() << ")\n";

    processor->editorBeingDeleted (editor);
    delete editor;

    return 0;
}
