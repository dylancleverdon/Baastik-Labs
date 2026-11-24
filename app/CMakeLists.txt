# The Tutti app: a normal app with a window (Dock icon on macOS, taskbar on Windows) that keeps
# running in the background when the window is closed, plus a menu bar / tray icon.
juce_add_gui_app(Tutti
    PRODUCT_NAME "Tutti"
    COMPANY_NAME "Baastik Labs"
    BUNDLE_ID com.baastiklabs.tutti
    VERSION ${TUTTI_VERSION}
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png
    PLIST_TO_MERGE "<plist><dict><key>NSHumanReadableCopyright</key><string>Baastik Labs</string><key>NSLocalNetworkUsageDescription</key><string>Tutti connects to your friends' computers over Tailscale to share your song.</string></dict></plist>")

juce_add_binary_data(TuttiAppData
    NAMESPACE TuttiAppData
    SOURCES
        Resources/icon.png
        Resources/tray-template.png
        Resources/tray-color.png)

file(GLOB_RECURSE TUTTI_APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)
target_sources(Tutti PRIVATE ${TUTTI_APP_SOURCES})
target_include_directories(Tutti PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

target_compile_definitions(Tutti PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_XINPUT=0
    JUCE_APPLICATION_NAME_STRING="Tutti"
    JUCE_APPLICATION_VERSION_STRING="${TUTTI_VERSION}"
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(APPLE)
    target_link_libraries(Tutti PRIVATE "-framework CoreServices") # Dock icon clicks (MacReopen.cpp)
    target_link_libraries(Tutti PRIVATE "-framework ApplicationServices") # pressing Save in Live (LiveSave.cpp)
endif()

target_link_libraries(Tutti PRIVATE
    tutti_engine
    TuttiAppData
    juce::juce_core
    juce::juce_cryptography
    juce::juce_audio_formats
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# The Remote Script ships inside the app: Contents/Resources/RemoteScript/Tutti on macOS,
# RemoteScript/Tutti next to the .exe on Windows. The app installs it into Live's User Library.
set(TUTTI_SCRIPT_SOURCE ${CMAKE_SOURCE_DIR}/remote_script/Tutti)
if(APPLE)
    set(TUTTI_SCRIPT_DEST "$<TARGET_BUNDLE_CONTENT_DIR:Tutti>/Resources/RemoteScript/Tutti")
else()
    set(TUTTI_SCRIPT_DEST "$<TARGET_FILE_DIR:Tutti>/RemoteScript/Tutti")
endif()
add_custom_command(TARGET Tutti POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${TUTTI_SCRIPT_DEST}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${TUTTI_SCRIPT_SOURCE}" "${TUTTI_SCRIPT_DEST}"
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${TUTTI_SCRIPT_DEST}/__pycache__"
    VERBATIM)
