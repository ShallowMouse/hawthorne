cmake_minimum_required(VERSION 3.15)
project(Hawthorne VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

juce_add_gui_app(Hawthorne
    PRODUCT_NAME "Hawthorne"
    COMPANY_NAME "Hawthorne"
    BUNDLE_ID "com.hawthorne.recorder"
    VERSION ${PROJECT_VERSION}
)

target_sources(Hawthorne PRIVATE
    Source/Main.cpp
    Source/UpdateChecker.h
)

target_compile_definitions(Hawthorne PRIVATE
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    HAWTHORNE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(Hawthorne PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    wininet.lib
)
