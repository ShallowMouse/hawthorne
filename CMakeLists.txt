cmake_minimum_required(VERSION 3.15)

project(Hawthorne VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add JUCE
add_subdirectory(JUCE)

# Create our executable
juce_add_gui_app(Hawthorne
    PRODUCT_NAME "Hawthorne"
    COMPANY_NAME "Hawthorne"
    BUNDLE_ID "com.hawthorne.recorder"
    VERSION "1.0.0"
)

# Add source files
target_sources(Hawthorne PRIVATE
    Source/Main.cpp
    Source/MainWindow.h
    Source/MainWindow.cpp
    Source/AudioEngine.h
    Source/AudioEngine.cpp
    Source/Track.h
    Source/Track.cpp
    Source/OSCController.h
    Source/OSCController.cpp
    Source/Settings.h
    Source/Settings.cpp
    Source/VintageLookAndFeel.h
    Source/TransportComponent.h
    Source/TrackComponent.h
    Source/TapeUI.h
    Source/SettingsDialog.h
    Source/WaveformPeakGenerator.h
    Source/UpdateChecker.h
)

# Version definition (used by UpdateChecker)
target_compile_definitions(Hawthorne PRIVATE
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    HAWTHORNE_VERSION="1.0.0"
    HAWTHORNE_VERSION_MAJOR=1
    HAWTHORNE_VERSION_MINOR=0
    HAWTHORNE_VERSION_PATCH=0
)

# Link JUCE modules
target_link_libraries(Hawthorne PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_osc
    wininet.lib
)
