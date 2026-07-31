#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "moe/OrchestraEngine.h"
#include "moe/bank/BankLoader.h"
#include "state/Parameters.h"

#include <atomic>
#include <memory>

namespace moe::plugin
{

/** The DAW-facing adapter.

    It owns the engine and does three things: translate MIDI, copy parameters
    into the engine once per block, and load banks on a background thread. All
    the actual work lives in `moe::OrchestraEngine`. */
class MyOrchestralProcessor final : public juce::AudioProcessor
{
public:
    MyOrchestralProcessor();
    ~MyOrchestralProcessor() override;

    // ---- AudioProcessor ----------------------------------------------------
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destination) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- editor-facing API -------------------------------------------------

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    OrchestraEngine& getEngine() noexcept { return engine; }

    /** Loads a bank into a section on a background thread. `onFinished` is
        called on the message thread with the outcome. */
    void loadBankAsync (int section,
                        const juce::File& sfzFile,
                        std::function<void (bool success, juce::String message)> onFinished);

    void clearSection (int section);

    /** Loads a convolution impulse response. Blocking, but called from the UI. */
    bool loadImpulseResponse (const juce::File& irFile);
    juce::File getImpulseResponseFile() const;

    void setSectionMidiChannel (int section, int channel);
    void setSectionArticulation (int section, Articulation articulation);
    void setSectionSoloed (int section, bool soloed);

    void setStreamingProfile (StreamingProfile profile);
    StreamingProfile getStreamingProfile() const noexcept { return engine.getStreamingProfile(); }

    juce::File getSectionBankFile (int section) const;

    /** 0..1 while a bank is loading; 1 when idle. */
    float getLoadProgress() const noexcept { return loadProgress.load(); }
    bool  isLoading() const noexcept { return loadingSection.load() >= 0; }
    int   getLoadingSection() const noexcept { return loadingSection.load(); }

    /** Most recent warnings from a bank load, for the UI to surface. */
    juce::StringArray getLastLoadWarnings() const;

private:
    void pushParametersToEngine();

    /** Writes `value` to a parameter, but only if the user has not moved it.

        Loading a bank should seat the section on the stage, and the parameters
        are what the engine actually reads — so the default has to be written
        there. Doing it unconditionally would throw away a position the user
        chose, or one restored from a session, so it is only applied to
        parameters still sitting at their factory value. */
    void setParameterIfUntouched (const juce::String& parameterId, float value);

    juce::AudioProcessorValueTreeState parameters;
    juce::ValueTree                    extraState { state::tree };

    OrchestraEngine engine;

    // Bank loading runs on its own thread so a 30 GB library does not block the
    // message thread, let alone the audio thread.
    std::unique_ptr<juce::ThreadPool> loaderPool;
    bank::BankLoader                  bankLoader;
    std::atomic<float>                loadProgress { 1.0f };
    std::atomic<int>                  loadingSection { -1 };

    mutable juce::CriticalSection warningLock;
    juce::StringArray             lastWarnings;

    // Cached parameter pointers: looking these up by string every block would be
    // wasteful and is exactly the kind of thing that shows up as CPU on a
    // 16-section template.
    struct SectionParameters
    {
        std::atomic<float>* gain     = nullptr;
        std::atomic<float>* pan      = nullptr;
        std::atomic<float>* distance = nullptr;
        std::atomic<float>* width    = nullptr;
        std::atomic<float>* send     = nullptr;
        std::atomic<float>* mute     = nullptr;
        std::atomic<float>* attack   = nullptr;
        std::atomic<float>* release  = nullptr;
    };

    std::array<SectionParameters, params::numSections> sectionParameters;
    std::atomic<float>* masterGainParameter    = nullptr;
    std::atomic<float>* reverbMixParameter     = nullptr;
    std::atomic<float>* humaniseParameter      = nullptr;
    std::atomic<float>* legatoParameter        = nullptr;

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MyOrchestralProcessor)
};

} // namespace moe::plugin
