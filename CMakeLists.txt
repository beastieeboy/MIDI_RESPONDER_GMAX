cmake_minimum_required(VERSION 3.22)
project(SekcjaKrzemowa VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimalna wersja macOS")

option(SK_COPY_PLUGINS "Skopiuj wtyczki do ~/Library/Audio/Plug-Ins po kompilacji" OFF)
option(SK_BUILD_TESTS "Buduj testy rdzenia" ON)

# JUCE: pobierany automatycznie. Lokalną kopię wskażesz przez -DFETCHCONTENT_SOURCE_DIR_JUCE=/ścieżka/JUCE
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Rdzeń niezależny od JUCE (analiza bębnów, harmonia, licki, link między instancjami)
add_library(sk_core STATIC
    Source/Core/Engine.cpp
    Source/Core/LickLibrary.cpp
    Source/Core/SharedLink.cpp
    Source/Core/GuitarEngine.cpp)
target_include_directories(sk_core PUBLIC Source/Core)
set_target_properties(sk_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(UNIX AND NOT APPLE)
    target_link_libraries(sk_core PUBLIC rt)
endif()

juce_add_binary_data(sk_assets SOURCES
    Resources/baner-naglowek.svg
    Resources/istota-piano.svg
    Resources/istota-bass.svg
    Resources/istota-gitara.svg
    Resources/istota-logo.svg)
set_target_properties(sk_assets PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Tylko VST3: REAPER przepuszcza wejściowe MIDI przez wtyczki AU, więc nuty
# bębnów trafiałyby do instrumentu razem z akompaniamentem.
set(SK_FORMATS VST3)

function(sk_add_plugin target product code is_bass bundle)
    juce_add_plugin(${target}
        COMPANY_NAME "Sekcja Krzemowa"
        COMPANY_WEBSITE "https://github.com/beastieeboy/MIDI_RESPONDER_GMAX"
        BUNDLE_ID "com.sekcjakrzemowa.${bundle}"
        PLUGIN_MANUFACTURER_CODE Skrz
        PLUGIN_CODE ${code}
        FORMATS ${SK_FORMATS}
        PRODUCT_NAME "${product}"
        DESCRIPTION "Reaktywny akompaniament MIDI sterowany bębnami na żywo"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Fx Generator
        COPY_PLUGIN_AFTER_BUILD ${SK_COPY_PLUGINS})

    target_sources(${target} PRIVATE
        Source/Plugin/PluginProcessor.cpp
        Source/Plugin/PluginEditor.cpp
        Source/Plugin/SiliconLookAndFeel.cpp)

    target_compile_definitions(${target} PUBLIC
        SK_ROLE_BASS=${is_bass}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(${target}
        PRIVATE sk_core sk_assets juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
endfunction()

sk_add_plugin(SekcjaKrzemowaPiano "Sekcja Krzemowa Piano" SkPn 0 piano)
sk_add_plugin(SekcjaKrzemowaBass  "Sekcja Krzemowa Bass"  SkBs 1 bass)

# Gitara prowadząca: wtyczka MIDI przed Kontaktem (Session Guitarist Electric Neon).
juce_add_plugin(SekcjaKrzemowaGitara
    COMPANY_NAME "Sekcja Krzemowa"
    COMPANY_WEBSITE "https://github.com/beastieeboy/MIDI_RESPONDER_GMAX"
    BUNDLE_ID "com.sekcjakrzemowa.gitara"
    PLUGIN_MANUFACTURER_CODE Skrz
    PLUGIN_CODE SkGt
    FORMATS ${SK_FORMATS}
    PRODUCT_NAME "Sekcja Krzemowa Gitara"
    DESCRIPTION "Gitara prowadząca MIDI: melodie i zagrywki dopasowane do bębnów, Piano i Bass"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Generator
    COPY_PLUGIN_AFTER_BUILD ${SK_COPY_PLUGINS})
set(SK_GUITAR_SOURCES
    Source/Guitar/GuitarProcessor.cpp
    Source/Guitar/GuitarEditor.cpp
    Source/Plugin/SiliconLookAndFeel.cpp)
target_sources(SekcjaKrzemowaGitara PRIVATE ${SK_GUITAR_SOURCES})
target_compile_definitions(SekcjaKrzemowaGitara PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(SekcjaKrzemowaGitara
    PRIVATE sk_core sk_assets juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(SK_BUILD_TESTS)
    enable_testing()
    add_executable(sk_core_tests tests/CoreTests.cpp)
    target_link_libraries(sk_core_tests PRIVATE sk_core)
    add_test(NAME core COMMAND sk_core_tests)
    add_executable(sk_guitar_tests tests/GuitarTests.cpp)
    target_link_libraries(sk_guitar_tests PRIVATE sk_core)
    add_test(NAME guitar COMMAND sk_guitar_tests)
endif()

# Test w hoście JUCE (ładuje zbudowane VST3). Domyślnie wyłączony.
option(SK_BUILD_HOST_TEST "Buduj test ładowania VST3 w hoście JUCE" OFF)
if(SK_BUILD_HOST_TEST)
    juce_add_console_app(sk_host_test PRODUCT_NAME "sk_host_test")
    target_sources(sk_host_test PRIVATE tests/HostTest.cpp)
    target_compile_definitions(sk_host_test PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(sk_host_test PRIVATE juce::juce_audio_processors juce::juce_gui_basics
                          juce::juce_recommended_config_flags)
endif()
if(SK_BUILD_HOST_TEST)
    foreach(role piano bass)
        juce_add_console_app(sk_snapshot_${role} PRODUCT_NAME "sk_snapshot_${role}")
        target_sources(sk_snapshot_${role} PRIVATE tests/EditorSnapshot.cpp
            Source/Plugin/PluginProcessor.cpp Source/Plugin/PluginEditor.cpp Source/Plugin/SiliconLookAndFeel.cpp)
        if(role STREQUAL "bass")
            set(_isbass 1)
        else()
            set(_isbass 0)
        endif()
        target_compile_definitions(sk_snapshot_${role} PRIVATE SK_ROLE_BASS=${_isbass} JucePlugin_Name="Sekcja"
            JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(sk_snapshot_${role} PRIVATE sk_core sk_assets juce::juce_audio_utils
            juce::juce_recommended_config_flags)
    endforeach()
    juce_add_console_app(sk_snapshot_gitara PRODUCT_NAME "sk_snapshot_gitara")
    target_sources(sk_snapshot_gitara PRIVATE tests/GuitarSnapshot.cpp ${SK_GUITAR_SOURCES})
    target_compile_definitions(sk_snapshot_gitara PRIVATE JucePlugin_Name="Sekcja Krzemowa Gitara"
        JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(sk_snapshot_gitara PRIVATE sk_core sk_assets juce::juce_audio_utils
        juce::juce_recommended_config_flags)

    # Test gitary w hoście JUCE: ładuje Piano, Bass i Gitarę (VST3) i gra jam.
    juce_add_console_app(sk_guitar_host_test PRODUCT_NAME "sk_guitar_host_test")
    target_sources(sk_guitar_host_test PRIVATE tests/GuitarHostTest.cpp)
    target_compile_definitions(sk_guitar_host_test PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(sk_guitar_host_test PRIVATE juce::juce_audio_processors juce::juce_gui_basics
                          juce::juce_recommended_config_flags)
endif()
