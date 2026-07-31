# Engine unit tests. No JUCE, no audio device, no sample library required:
# fixtures are synthesised into a scratch directory at run time.

add_executable(moe_tests
    main.cpp
    test_sfz_parser.cpp
    test_wav_reader.cpp
    test_region_selection.cpp
    test_dsp.cpp
    test_stream.cpp
    test_engine.cpp
)

target_include_directories(moe_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(moe_tests PRIVATE moe_engine)
target_compile_features(moe_tests PRIVATE cxx_std_20)

moe_set_warnings(moe_tests)

# doctest::Approx takes a double, so every comparison against a float value
# trips -Wdouble-promotion. The warning is worth keeping on the engine, where
# an accidental promotion in a per-sample loop is a real cost; in test
# assertions it is noise.
if(NOT MSVC)
    target_compile_options(moe_tests PRIVATE -Wno-double-promotion)
endif()

add_test(NAME moe_tests COMMAND moe_tests --no-intro)

set_tests_properties(moe_tests PROPERTIES TIMEOUT 300)
