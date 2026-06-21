cmake_minimum_required(VERSION 3.22)
project(Chordizer VERSION 0.2.0 LANGUAGES C CXX)

set(JUCE_DIR "" CACHE PATH "Path to a JUCE checkout")
option(CHORDIZER_FETCH_JUCE "Download the pinned JUCE release when JUCE_DIR is not set" ON)

if (NOT JUCE_DIR AND DEFINED ENV{JUCE_DIR})
    set(JUCE_DIR "$ENV{JUCE_DIR}")
endif()

if (JUCE_DIR)
    if (NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "JUCE_DIR must point to a JUCE checkout")
    endif()
    add_subdirectory("${JUCE_DIR}" JUCE-build EXCLUDE_FROM_ALL)
elseif (CHORDIZER_FETCH_JUCE)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.7
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
else()
    message(FATAL_ERROR "Set JUCE_DIR or enable CHORDIZER_FETCH_JUCE")
endif()

set(NEURALNOTE_DIR "${CMAKE_CURRENT_LIST_DIR}/ThirdParty/NeuralNote")
set(RTNEURAL_DIR "${CMAKE_CURRENT_LIST_DIR}/ThirdParty/RTNeural")

juce_add_binary_data(ChordizerNeuralData SOURCES
    ${NEURALNOTE_DIR}/ModelData/cnn_contour_model.json
    ${NEURALNOTE_DIR}/ModelData/cnn_note_model.json
    ${NEURALNOTE_DIR}/ModelData/cnn_onset_1_model.json
    ${NEURALNOTE_DIR}/ModelData/cnn_onset_2_model.json
    ${NEURALNOTE_DIR}/ModelData/features_model.ort)

add_library(ChordizerRTNeural STATIC ${RTNEURAL_DIR}/RTNeural/RTNeural.cpp)
target_compile_features(ChordizerRTNeural PUBLIC cxx_std_17)
target_compile_definitions(ChordizerRTNeural PUBLIC RTNEURAL_USE_EIGEN=1 RTNEURAL_DEFAULT_ALIGNMENT=16)
target_include_directories(ChordizerRTNeural PUBLIC
    ${RTNEURAL_DIR}
    ${RTNEURAL_DIR}/modules/Eigen
    ${RTNEURAL_DIR}/modules/json)

add_library(ChordizerBasicPitchCNN STATIC ${NEURALNOTE_DIR}/Model/BasicPitchCNN.cpp)
target_compile_features(ChordizerBasicPitchCNN PUBLIC cxx_std_17)
target_include_directories(ChordizerBasicPitchCNN PUBLIC ${NEURALNOTE_DIR}/Model)
target_link_libraries(ChordizerBasicPitchCNN PUBLIC ChordizerRTNeural ChordizerNeuralData)

add_library(ChordizerOnnxRuntime STATIC IMPORTED GLOBAL)
set_target_properties(ChordizerOnnxRuntime PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/ThirdParty/onnxruntime/lib/libonnxruntime.a")

set(CHORDIZER_SOURCES
    Source/ChordEngine.cpp Source/ChordEngine.h
    Source/MidiExport.cpp Source/MidiExport.h
    Source/PluginProcessor.cpp Source/PluginProcessor.h
    Source/PluginEditor.cpp Source/PluginEditor.h)

function(configure_chordizer target product bundle code main_type midi_effect audio_fx)
    juce_add_plugin(${target}
        COMPANY_NAME "Santismo"
        BUNDLE_ID "${bundle}"
        PLUGIN_MANUFACTURER_CODE "StTr"
        PLUGIN_CODE "${code}"
        FORMATS AU
        PRODUCT_NAME "${product}"
        IS_SYNTH FALSE
        IS_MIDI_EFFECT ${midi_effect}
        NEEDS_MIDI_INPUT ${midi_effect}
        NEEDS_MIDI_OUTPUT ${midi_effect}
        AU_MAIN_TYPE ${main_type}
        COPY_PLUGIN_AFTER_BUILD FALSE)
    juce_generate_juce_header(${target})
    target_sources(${target} PRIVATE ${CHORDIZER_SOURCES})
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_definitions(${target} PRIVATE CHORDIZER_AUDIO_FX=${audio_fx}
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(${target} PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endfunction()

configure_chordizer(ChordizerMidi "Chordizer" "com.santismo.chordizer.midi" "Chrz" kAudioUnitType_MIDIProcessor TRUE 0)
configure_chordizer(ChordizerAudio "Chordizer Audio" "com.santismo.chordizer.audio" "ChzA" kAudioUnitType_Effect FALSE 1)

target_sources(ChordizerAudio PRIVATE
    Source/AudioTranscriptionRefiner.cpp Source/AudioTranscriptionRefiner.h
    ${NEURALNOTE_DIR}/Model/BasicPitch.cpp
    ${NEURALNOTE_DIR}/Model/Features.cpp
    ${NEURALNOTE_DIR}/Model/Notes.cpp)
target_include_directories(ChordizerAudio SYSTEM PRIVATE
    ${NEURALNOTE_DIR}/Model
    ${NEURALNOTE_DIR}/Utils
    ${CMAKE_CURRENT_LIST_DIR}/ThirdParty/onnxruntime/include)
target_compile_definitions(ChordizerAudio PRIVATE SAVE_DOWNSAMPLED_AUDIO=0 USE_TEST_NOTE_FRAME_TO_TIME=0)
target_link_libraries(ChordizerAudio PRIVATE
    ChordizerBasicPitchCNN ChordizerOnnxRuntime ChordizerNeuralData)
set_source_files_properties(
    ${NEURALNOTE_DIR}/Model/BasicPitch.cpp
    ${NEURALNOTE_DIR}/Model/Features.cpp
    ${NEURALNOTE_DIR}/Model/Notes.cpp
    PROPERTIES COMPILE_OPTIONS "-w")

juce_add_console_app(ChordizerEngineTests PRODUCT_NAME "Chordizer Engine Tests")
juce_generate_juce_header(ChordizerEngineTests)
target_sources(ChordizerEngineTests PRIVATE Source/ChordEngine.cpp Source/ChordEngine.h
    Source/MidiExport.cpp Source/MidiExport.h Tests/ChordEngineTests.cpp)
target_compile_features(ChordizerEngineTests PRIVATE cxx_std_17)
target_compile_definitions(ChordizerEngineTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ChordizerEngineTests PRIVATE juce::juce_audio_basics
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

juce_add_console_app(ChordizerNeuralSmokeTest PRODUCT_NAME "Chordizer Neural Smoke Test")
juce_generate_juce_header(ChordizerNeuralSmokeTest)
target_sources(ChordizerNeuralSmokeTest PRIVATE
    Source/ChordEngine.cpp Source/ChordEngine.h
    Source/AudioTranscriptionRefiner.cpp Source/AudioTranscriptionRefiner.h
    Tests/NeuralModelSmokeTest.cpp
    ${NEURALNOTE_DIR}/Model/BasicPitch.cpp
    ${NEURALNOTE_DIR}/Model/Features.cpp
    ${NEURALNOTE_DIR}/Model/Notes.cpp)
target_compile_features(ChordizerNeuralSmokeTest PRIVATE cxx_std_17)
target_compile_definitions(ChordizerNeuralSmokeTest PRIVATE
    SAVE_DOWNSAMPLED_AUDIO=0 USE_TEST_NOTE_FRAME_TO_TIME=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_include_directories(ChordizerNeuralSmokeTest SYSTEM PRIVATE
    ${NEURALNOTE_DIR}/Model ${NEURALNOTE_DIR}/Utils
    ${CMAKE_CURRENT_LIST_DIR}/ThirdParty/onnxruntime/include)
target_link_libraries(ChordizerNeuralSmokeTest PRIVATE
    juce::juce_audio_basics juce::juce_audio_formats juce::juce_events
    ChordizerBasicPitchCNN ChordizerOnnxRuntime ChordizerNeuralData
    PUBLIC juce::juce_recommended_config_flags)
