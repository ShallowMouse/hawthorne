cmake_minimum_required(VERSION 3.22)
project(Hawthorne VERSION 1.18.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(HAWTHORNE_BUILD_APP   "Build the Hawthorne application" ON)
option(HAWTHORNE_BUILD_TESTS "Build the headless engine tests" OFF)
set(HAWTHORNE_ASIO_SDK "" CACHE PATH "Path to the Steinberg ASIO SDK (enables ASIO on Windows)")

add_subdirectory(JUCE)

set(ENGINE_SOURCES
    Source/Engine/Reel.h
    Source/Engine/Reel.cpp
    Source/Engine/Session.h
    Source/Engine/Session.cpp
    Source/Engine/Library.h
    Source/Engine/Library.cpp
    Source/Engine/RecordingStream.h
    Source/Engine/RecordingStream.cpp
    Source/Engine/Varispeed.cpp
    Source/Engine/Mtc.cpp
    Source/Network/NetReceiver.cpp
    Source/Network/NetSender.cpp
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
    # Logo (header mark) for the apps
    juce_add_binary_data(HawthorneBinaryData SOURCES Resources/hawthorne-mark.svg Resources/manual.html)

    juce_add_gui_app(Hawthorne
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/hawthorne-logo.png"
        PRODUCT_NAME "Hawthorne"
        COMPANY_NAME "Hawthorne"
        BUNDLE_ID "com.hawthorne.recorder"
        VERSION ${PROJECT_VERSION}
    )

    target_sources(Hawthorne PRIVATE
        ${ENGINE_SOURCES}
        Source/Main.cpp
        Source/Updater.h
        Source/Updater.cpp
        Source/UI/HawthorneLookAndFeel.h
        Source/UI/Logo.cpp
        Source/UI/UIHelpers.h
        Source/UI/ChannelModule.h
        Source/UI/ChannelModule.cpp
        Source/UI/MarkerPanel.h
        Source/UI/MarkerPanel.cpp
        Source/UI/SessionSheet.h
        Source/UI/SessionSheet.cpp
        Source/UI/LibraryView.h
        Source/UI/LibraryView.cpp
        Source/UI/VUMeter.h
        Source/UI/VUMeter.cpp
        Source/UI/RoutingMatrix.h
        Source/UI/RoutingMatrix.cpp
        Source/UI/NetworkWindow.h
        Source/UI/NetworkWindow.cpp
        Source/UI/SyncWindow.h
        Source/UI/SyncWindow.cpp
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
        HawthorneBinaryData
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

    #--------------------------------------------------------------------------
    # Hawthorne Link: runs on the other PC and streams its inputs to Hawthorne.
    juce_add_gui_app(HawthorneLink
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/hawthorne-logo.png"
        PRODUCT_NAME "Hawthorne Link"
        COMPANY_NAME "Hawthorne"
        BUNDLE_ID "com.hawthorne.link"
        VERSION ${PROJECT_VERSION}
    )
    target_sources(HawthorneLink PRIVATE
        Source/Link/LinkMain.cpp
        Source/UI/Logo.cpp
        Source/Network/NetProtocol.h
        Source/Network/NetSender.h
        Source/Network/NetSender.cpp
    )
    target_compile_definitions(HawthorneLink PRIVATE ${COMMON_DEFS} JUCE_WASAPI=1 JUCE_DIRECTSOUND=1)
    if(WIN32 AND HAWTHORNE_ASIO_SDK AND EXISTS "${HAWTHORNE_ASIO_SDK}/common/iasiodrv.h")
        target_include_directories(HawthorneLink PRIVATE "${HAWTHORNE_ASIO_SDK}/common")
        target_compile_definitions(HawthorneLink PRIVATE JUCE_ASIO=1)
    endif()
    target_link_libraries(HawthorneLink PRIVATE
        HawthorneBinaryData
        juce::juce_audio_basics
        juce::juce_audio_devices
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
