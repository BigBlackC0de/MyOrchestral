# Command-line utilities built on the engine. No JUCE, so they build and run
# anywhere the engine does.

add_executable(moe_render render/main.cpp)
target_link_libraries(moe_render PRIVATE moe_engine)
target_compile_features(moe_render PRIVATE cxx_std_20)
moe_set_warnings(moe_render)
