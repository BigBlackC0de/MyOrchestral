# The JUCE plugin: a thin adapter over moe_engine.
#
# AAX is not enabled: it requires the Avid SDK plus a signed developer account,
# neither of which can be assumed here. Add AAX to FORMATS and point
# juce_set_aax_sdk_path at the SDK once you have one — nothing else changes.

juce_add_plugin(MyOrchestral
    COMPANY_NAME                "MyOrchestral"
    BUNDLE_ID                   com.myorchestral.plugin
    PLUGIN_MANUFACTURER_CODE    Myor
    PLUGIN_CODE                 Morc
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "MyOrchestral"
    DESCRIPTION                 "Symphonic orchestra sampler"

    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    COPY_PLUGIN_AFTER_BUILD     TRUE

    VST3_CATEGORIES             "Instrument" "Sampler"
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
)

target_sources(MyOrchestral PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/state/Parameters.cpp
    source/ui/Theme.cpp
    source/ui/StageView.cpp
    source/ui/SectionList.cpp
    source/ui/InstrumentPanel.cpp
)

target_include_directories(MyOrchestral PRIVATE source)

target_compile_definitions(MyOrchestral PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0

    # The engine reads and writes audio files itself, so JUCE's format readers
    # are not needed and only add weight.
    JUCE_MODAL_LOOPS_PERMITTED=0
)

target_link_libraries(MyOrchestral
    PRIVATE
        moe_engine
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
