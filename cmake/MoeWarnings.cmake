# Shared warning configuration.
#
# Warnings are strict on our own code and left alone for JUCE, which has its own
# opinions about them. `MOE_WERROR` is off by default so a compiler upgrade never
# blocks local work, and on in CI.

option(MOE_WERROR "Treat warnings as errors" OFF)

function(moe_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(MOE_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wdouble-promotion
        )
        if(MOE_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
