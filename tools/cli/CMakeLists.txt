# tutti-peer: a headless Tutti driven by JSON lines on stdin (used by the end-to-end tests).
juce_add_console_app(TuttiPeer
    PRODUCT_NAME "tutti-peer"
    COMPANY_NAME "Baastik Labs"
    BUNDLE_ID com.baastiklabs.tutti.peer
    VERSION ${TUTTI_VERSION})

target_sources(TuttiPeer PRIVATE Main.cpp)
target_compile_definitions(TuttiPeer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(TuttiPeer PRIVATE
    tutti_engine
    juce::juce_core
    juce::juce_cryptography
    juce::juce_audio_formats
    juce::juce_recommended_config_flags)
set_target_properties(TuttiPeer PROPERTIES OUTPUT_NAME "tutti-peer" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
