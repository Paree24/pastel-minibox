cmake_minimum_required(VERSION 3.22)
project(PastelMinibox VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
# Reuse a local JUCE checkout when one exists (sibling projects pin the same tag).
set(_localJuce "${CMAKE_CURRENT_SOURCE_DIR}/../ersa/build/_deps/juce-src")
if(EXISTS "${_localJuce}/CMakeLists.txt" AND NOT FETCHCONTENT_SOURCE_DIR_JUCE)
  set(FETCHCONTENT_SOURCE_DIR_JUCE "${_localJuce}" CACHE PATH "" FORCE)
endif()
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(MiniboxAssets
  HEADER_NAME MiniboxAssets.h
  NAMESPACE MiniboxAssets
  SOURCES Assets/Lato-Regular.ttf Assets/Lato-Bold.ttf Assets/Lato-Black.ttf)

# Shared core: engine, model, sequencer, DSP, devices and UI. Compiled into each target.
file(GLOB_RECURSE MINIBOX_CORE CONFIGURE_DEPENDS
  Source/engine/*.cpp Source/model/*.cpp Source/seq/*.cpp Source/dsp/*.cpp
  Source/devices/*.cpp Source/ui/*.cpp Source/app/presets/*.cpp)

set(MINIBOX_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_MODAL_LOOPS_PERMITTED=1)
set(MINIBOX_LIBS juce::juce_audio_utils juce::juce_dsp MiniboxAssets
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

function(minibox_setup target)
  target_include_directories(${target} PRIVATE Source)
  target_compile_definitions(${target} PRIVATE ${MINIBOX_DEFS} ${ARGN})
  target_sources(${target} PRIVATE ${MINIBOX_CORE})
endfunction()

# ---- Standalone groovebox
juce_add_gui_app(MiniboxStandalone PRODUCT_NAME "Pastel Minibox" COMPANY_NAME "Pastel")
file(GLOB MINIBOX_APP CONFIGURE_DEPENDS Source/app/*.cpp)
target_sources(MiniboxStandalone PRIVATE ${MINIBOX_APP})
minibox_setup(MiniboxStandalone MINIBOX_STANDALONE=1 JUCE_APPLICATION_NAME_STRING="Pastel Minibox"
              JUCE_APPLICATION_VERSION_STRING="1.0.0")
target_link_libraries(MiniboxStandalone PRIVATE ${MINIBOX_LIBS})

# ---- Plugin: one instrument at a time
juce_add_plugin(MiniboxPlugin
  COMPANY_NAME "Pastel"
  BUNDLE_ID "com.pastel.minibox"
  PLUGIN_MANUFACTURER_CODE Pstl
  PLUGIN_CODE Mnbx
  FORMATS VST3
  PRODUCT_NAME "Minibox"
  DESCRIPTION "Seven-device instrument: Mono, Poly, Acid, FM, Sampler, Drums, Drum Sampler"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE)
file(GLOB MINIBOX_PLUGIN CONFIGURE_DEPENDS Source/plugin/*.cpp)
target_sources(MiniboxPlugin PRIVATE ${MINIBOX_PLUGIN})
minibox_setup(MiniboxPlugin MINIBOX_PLUGIN=1)
target_link_libraries(MiniboxPlugin PRIVATE ${MINIBOX_LIBS})

# ---- Tests and tools (headless)
juce_add_console_app(MiniboxTests PRODUCT_NAME "MiniboxTests")
file(GLOB MINIBOX_TESTS CONFIGURE_DEPENDS Test/*.cpp)
set(MINIBOX_APP_NOMAIN ${MINIBOX_APP})
list(FILTER MINIBOX_APP_NOMAIN EXCLUDE REGEX "Main\\.cpp$")
target_sources(MiniboxTests PRIVATE ${MINIBOX_TESTS} ${MINIBOX_APP_NOMAIN} ${MINIBOX_PLUGIN})
minibox_setup(MiniboxTests MINIBOX_TESTS=1 JUCE_STANDALONE_APPLICATION=0 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(MiniboxTests PRIVATE ${MINIBOX_LIBS})

# Factory content (samples) is found next to the binaries or in the source tree.
target_compile_definitions(MiniboxStandalone PRIVATE MINIBOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(MiniboxPlugin PRIVATE MINIBOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(MiniboxTests PRIVATE MINIBOX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# Ship the factory content (samples and preset files) with the binaries (the runtime also falls back to the source
# tree). A target that runs on every build, so regenerated presets reach the bundles even when no code changed.
set(MB_PLUGIN_FACTORY "${CMAKE_CURRENT_BINARY_DIR}/MiniboxPlugin_artefacts/$<CONFIG>/VST3/Minibox.vst3/Contents/Resources/Factory")
set(MB_APP_FACTORY "${CMAKE_CURRENT_BINARY_DIR}/MiniboxStandalone_artefacts/$<CONFIG>/Factory")
add_custom_target(MiniboxFactoryContent ALL
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${MB_PLUGIN_FACTORY}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Factory" "${MB_PLUGIN_FACTORY}"
  COMMAND ${CMAKE_COMMAND} -E remove_directory "${MB_APP_FACTORY}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Factory" "${MB_APP_FACTORY}"
  VERBATIM)
add_dependencies(MiniboxFactoryContent MiniboxPlugin_VST3 MiniboxStandalone)
