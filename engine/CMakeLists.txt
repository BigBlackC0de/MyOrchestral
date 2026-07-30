# The engine is deliberately free of JUCE and of any plugin SDK: it builds and
# tests anywhere, which is what lets CI run on Linux while the product targets
# macOS.

add_library(moe_engine STATIC
    src/Types.cpp
    src/OrchestraEngine.cpp
    src/sfz/SfzParser.cpp
    src/audio/WavReader.cpp
    src/bank/Region.cpp
    src/bank/Instrument.cpp
    src/bank/RegionSelector.cpp
    src/bank/BankLoader.cpp
    src/stream/SampleStreamer.cpp
    src/voice/Voice.cpp
    src/voice/VoiceManager.cpp
    src/dsp/Fft.cpp
    src/dsp/ConvolutionReverb.cpp
    src/space/StageProcessor.cpp
)

target_include_directories(moe_engine
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(moe_engine PUBLIC cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(moe_engine PUBLIC Threads::Threads)

moe_set_warnings(moe_engine)

set_target_properties(moe_engine PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
