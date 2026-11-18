cmake_minimum_required(VERSION 3.22)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VB_VERSION LIMIT_COUNT 1)
project(VocalBuilder VERSION ${VB_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- dependencies -----------------------------------------------------------------------
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" CACHE PATH "JUCE 8 source tree")
set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/onnxruntime" CACHE PATH "ONNX Runtime prebuilt package (include/ + lib/)")
option(VB_BUILD_PLUGIN "Build the VST3 plugin" ON)
option(VB_BUILD_ENGINE "Build the VocalEngine helper" ON)
option(VB_BUILD_TESTS "Build unit tests" ON)
option(VB_PERSONAL_BUILD "Personal-use build: no licence activation required" ON)
# CI only: verify licences against a throw-away key so automated tests can activate the runner.
set(VB_PUBLIC_KEY_HEADER "" CACHE FILEPATH "Override header with the licence public key (CI tests)")

add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

add_library(onnxruntime_imported SHARED IMPORTED)
if(WIN32)
  set_target_properties(onnxruntime_imported PROPERTIES
    IMPORTED_LOCATION "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
    IMPORTED_IMPLIB "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
else()
  file(GLOB _ort_so "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so*")
  list(GET _ort_so 0 _ort_first)
  set_target_properties(onnxruntime_imported PROPERTIES IMPORTED_LOCATION "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so")
endif()
target_include_directories(onnxruntime_imported INTERFACE "${ONNXRUNTIME_ROOT}/include")

# ---- shared code: licence verification (Ed25519 via Monocypher) -------------------------
add_library(vb_licence STATIC
  common/Licence.cpp
  third_party/monocypher/monocypher.c
  third_party/monocypher/monocypher-ed25519.c)
target_include_directories(vb_licence PUBLIC common third_party/monocypher)
set_target_properties(vb_licence PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(VB_PERSONAL_BUILD)
  add_compile_definitions(VB_PERSONAL_BUILD=1)
endif()
if(VB_PUBLIC_KEY_HEADER)
  add_compile_definitions(VB_PUBLIC_KEY_HEADER="${VB_PUBLIC_KEY_HEADER}")
endif()

# ---- VocalEngine helper (console app) -----------------------------------------------------
if(VB_BUILD_ENGINE)
  juce_add_console_app(VocalEngine PRODUCT_NAME "VocalEngine")
  target_sources(VocalEngine PRIVATE
    engine/Main.cpp engine/AudioIO.cpp engine/Dsp.cpp engine/Separator.cpp engine/VoiceConverter.cpp engine/Mdx.cpp
    common/LicenceManager.cpp)
  target_compile_definitions(VocalEngine PRIVATE
    JUCE_USE_MP3AUDIOFORMAT=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1
    VB_VERSION="${PROJECT_VERSION}")
  target_link_libraries(VocalEngine PRIVATE
    vb_licence juce::juce_core juce::juce_audio_formats onnxruntime_imported
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  if(UNIX)
    set_target_properties(VocalEngine PROPERTIES BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib" INSTALL_RPATH "$ORIGIN")
  endif()
endif()

# ---- VST3 plugin --------------------------------------------------------------------------
if(VB_BUILD_PLUGIN)
  juce_add_plugin(VocalBuilder
    COMPANY_NAME "Vocal Builder"
    PRODUCT_NAME "Vocal Builder"
    BUNDLE_ID "com.vocalbuilder.plugin"
    PLUGIN_MANUFACTURER_CODE Vcbd
    PLUGIN_CODE Vbld
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Tools
    VERSION ${PROJECT_VERSION})
  juce_generate_juce_header(VocalBuilder)
  target_include_directories(VocalBuilder PRIVATE common)
  target_sources(VocalBuilder PRIVATE
    plugin/PluginProcessor.cpp plugin/PluginEditor.cpp plugin/EngineJob.cpp plugin/Session.cpp
    plugin/Components.cpp common/LicenceManager.cpp)
  target_compile_definitions(VocalBuilder PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 VB_VERSION="${PROJECT_VERSION}")
  target_link_libraries(VocalBuilder PRIVATE
    vb_licence juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endif()

# ---- tests --------------------------------------------------------------------------------
if(VB_BUILD_TESTS)
  add_executable(vb_licence_tests tests/LicenceTests.cpp)
  target_link_libraries(vb_licence_tests PRIVATE vb_licence)

  # Headless workflow test: compiles the real plugin processor/editor code into a console app.
  juce_add_console_app(PluginWorkflowTest PRODUCT_NAME "PluginWorkflowTest")
  juce_generate_juce_header(PluginWorkflowTest)
  target_include_directories(PluginWorkflowTest PRIVATE common plugin)
  target_sources(PluginWorkflowTest PRIVATE tests/PluginWorkflowTest.cpp
    plugin/PluginProcessor.cpp plugin/PluginEditor.cpp plugin/EngineJob.cpp plugin/Session.cpp
    plugin/Components.cpp common/LicenceManager.cpp)
  target_compile_definitions(PluginWorkflowTest PRIVATE
    VB_ALLOW_TEST_VOICES=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1 VB_VERSION="${PROJECT_VERSION}")
  target_link_libraries(PluginWorkflowTest PRIVATE vb_licence juce::juce_audio_utils juce::juce_audio_formats
    juce::juce_dsp juce::juce_cryptography juce::juce_recommended_config_flags)
endif()
