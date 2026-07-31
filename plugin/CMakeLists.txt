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

# ---------------------------------------------------------------------------
# Headless UI screenshot tool.
#
# Renders the real editor to a PNG with no host and no window server (under
# Xvfb on Linux, natively on macOS). Used for documentation and for reviewing
# layout changes without launching a DAW.
# ---------------------------------------------------------------------------
option(MOE_BUILD_SCREENSHOT "Build the headless UI screenshot tool" OFF)

if(MOE_BUILD_SCREENSHOT)
    juce_add_console_app(moe_screenshot PRODUCT_NAME "moe_screenshot")

    target_sources(moe_screenshot PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../tools/screenshot/main.cpp
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/state/Parameters.cpp
        source/ui/Theme.cpp
        source/ui/StageView.cpp
        source/ui/SectionList.cpp
        source/ui/InstrumentPanel.cpp
    )

    target_include_directories(moe_screenshot PRIVATE source)

    # The plugin sources expect the JucePlugin_* defines that juce_add_plugin
    # generates, so mirror the ones they actually use.
    target_compile_definitions(moe_screenshot PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        # The plugin forbids modal loops, as a plugin must. This tool is not a
        # plugin: it needs `runDispatchLoopUntil` to pump the message queue
        # between audio blocks so the UI timers fire before the snapshot.
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="MyOrchestral"
    )

    target_link_libraries(moe_screenshot PRIVATE
        moe_engine
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
    )
endif()
