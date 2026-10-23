cmake_minimum_required(VERSION 3.22)
project(Hawthorne VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(HAWTHORNE_BUILD_APP   "Build the Hawthorne application" ON)
option(HAWTHORNE_BUILD_TESTS "Build the headless engine tests" OFF)
set(HAWTHORNE_ASIO_SDK "" CACHE PATH "Path to the Steinberg ASIO SDK (enables ASIO on Windows)")

add_subdirectory(JUCE)

set(ENGINE_SOURCES
    Source/Engine/Session.h
    Source/Engine/Session.cpp
    Source/Engine/RecordingStream.h
    Source/Engine/RecordingStream.cpp
    Source/Engine/TapeEngine.h
    Source/Engine/TapeEngine.cpp
)

set(COMMON_DEFS
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    HAWTHORNE_VERSION="${PROJECT_VERSION}"
)

#------------------------------------------------------------------------------
if(HAWTHORNE_BUILD_APP)
    juce_add_gui_app(Hawthorne
        PRODUCT_NAME "Hawthorne"
        COMPANY_NAME "Hawthorne"
        BUNDLE_ID "com.hawthorne.recorder"
        VERSION ${PROJECT_VERSION}
    )

    target_sources(Hawthorne PRIVATE
        ${ENGINE_SOURCES}
        Source/Main.cpp
        Source/UpdateChecker.h
        Source/UI/HawthorneLookAndFeel.h
        Source/UI/TrackStrip.h
        Source/UI/TrackStrip.cpp
        Source/UI/MainComponent.h
        Source/UI/MainComponent.cpp
    )

    target_compile_definitions(Hawthorne PRIVATE ${COMMON_DEFS}
        JUCE_WASAPI=1
        JUCE_DIRECTSOUND=1
    )

    if(WIN32 AND HAWTHORNE_ASIO_SDK AND EXISTS "${HAWTHORNE_ASIO_SDK}/common/iasiodrv.h")
        message(STATUS "Hawthorne: ASIO enabled (${HAWTHORNE_ASIO_SDK})")
        target_include_directories(Hawthorne PRIVATE "${HAWTHORNE_ASIO_SDK}/common")
        target_compile_definitions(Hawthorne PRIVATE JUCE_ASIO=1)
    else()
        message(STATUS "Hawthorne: ASIO disabled")
    endif()

    target_link_libraries(Hawthorne PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
endif()

#------------------------------------------------------------------------------
if(HAWTHORNE_BUILD_TESTS)
    juce_add_console_app(HawthorneEngineTests PRODUCT_NAME "HawthorneEngineTests")
    target_sources(HawthorneEngineTests PRIVATE ${ENGINE_SOURCES} Tests/EngineTests.cpp)
    target_compile_definitions(HawthorneEngineTests PRIVATE ${COMMON_DEFS})
    target_link_libraries(HawthorneEngineTests PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_core
        juce::juce_events
        juce::juce_recommended_config_flags
    )
endif()
