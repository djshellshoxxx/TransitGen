# TransitGen JUCE wrapper (07 §1–2). Built only with TG_BUILD_PLUGIN=ON.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# The wrapper sources live in an INTERFACE library so the plugin and the headless tests compile
# the same code (each against its own copy of the JUCE modules).
add_library(transitgen_plugin_shared INTERFACE)
set(TG_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Parameters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/PluginEditor.cpp
)
target_sources(transitgen_plugin_shared INTERFACE ${TG_PLUGIN_SOURCES})
target_include_directories(transitgen_plugin_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
# JUCE headers as system headers: our stricter flags must not fire inside JUCE.
target_include_directories(transitgen_plugin_shared SYSTEM INTERFACE ${juce_SOURCE_DIR}/modules)
target_compile_definitions(transitgen_plugin_shared INTERFACE
    TG_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0
)
target_link_libraries(transitgen_plugin_shared INTERFACE
    transitgen_core
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Our own sources are warning-free and built with -Werror; JUCE's get JUCE's recommended flags only.
# -Wno-float-equal: the core compares floats exactly on purpose (curve validation, bit-exact tests).
if(MSVC)
    set(TG_PLUGIN_WARNINGS /W4)
else()
    set(TG_PLUGIN_WARNINGS -Wall -Wextra -Werror -Wshadow -Wconversion -Wno-sign-conversion -Wno-float-equal)
endif()
set_source_files_properties(${TG_PLUGIN_SOURCES} PROPERTIES COMPILE_OPTIONS "${TG_PLUGIN_WARNINGS}")

set(TG_PLUGIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TG_PLUGIN_FORMATS AU)
endif()

juce_add_plugin(TransitGen
    COMPANY_NAME             "DJ ShellShoxxx"
    BUNDLE_ID                com.djshellshoxxx.transitgen
    PLUGIN_MANUFACTURER_CODE Djsx
    PLUGIN_CODE              Trgn
    PRODUCT_NAME             "TransitGen"
    VERSION                  ${PROJECT_VERSION}
    FORMATS                  ${TG_PLUGIN_FORMATS}
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE
    VST3_CATEGORIES          Fx
    AU_MAIN_TYPE             kAudioUnitType_MusicEffect   # MIDI input requires aumf (07 §3 amendment)
)
target_link_libraries(TransitGen PRIVATE transitgen_plugin_shared)

if(TG_BUILD_TESTS)
    add_subdirectory(tests)
endif()
