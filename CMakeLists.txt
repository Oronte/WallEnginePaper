cmake_minimum_required(VERSION 3.26)

project(WallEnginePaper
    VERSION 0.3.0
    DESCRIPTION "Moteur de fonds d'ecran animes 100 % local pour Windows 10/11"
    LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "WallEnginePaper cible uniquement Windows 10/11.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "Compilez avec MSVC (Visual Studio 2022 17.8+ ou plus recent).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# CRT statique : un seul .exe, aucune dependance au redistribuable VC++.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

option(LWE_ENABLE_WEBVIEW2 "Fonds web (Microsoft WebView2)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

configure_file(cmake/lwe_version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/lwe_version.h" @ONLY)

set(LWE_SOURCES
    src/main.cpp

    src/app/AppServices.h
    src/app/Application.h
    src/app/Application.cpp

    src/core/Log.h
    src/core/Log.cpp
    src/core/Paths.h
    src/core/Paths.cpp
    src/core/StringUtil.h
    src/core/StringUtil.cpp
    src/core/Win32Util.h
    src/core/Win32Util.cpp

    src/config/AppConfig.h
    src/config/AppConfig.cpp
    src/config/ConfigManager.h
    src/config/ConfigManager.cpp

    src/desktop/DesktopHost.h
    src/desktop/DesktopHost.cpp
    src/desktop/MonitorEnumerator.h
    src/desktop/MonitorEnumerator.cpp
    src/desktop/MonitorContext.h
    src/desktop/MonitorContext.cpp

    src/input/InputForwarder.h
    src/input/InputForwarder.cpp

    src/library/Library.h
    src/library/Library.cpp

    src/render/RenderDevice.h
    src/render/RenderDevice.cpp
    src/render/FrameLimiter.h
    src/render/FrameLimiter.cpp
    src/render/RenderTexture.h
    src/render/RenderTexture.cpp
    src/render/TextureLoader.h
    src/render/TextureLoader.cpp

    src/system/Autostart.h
    src/system/Autostart.cpp

    src/watchdog/PerformanceWatchdog.h
    src/watchdog/PerformanceWatchdog.cpp

    src/wallpaper/IWallpaperBackend.h
    src/wallpaper/WallpaperFactory.h
    src/wallpaper/WallpaperFactory.cpp
    src/wallpaper/shader/ShaderBackend.h
    src/wallpaper/shader/ShaderBackend.cpp
    src/wallpaper/media/MediaCompositor.h
    src/wallpaper/media/MediaCompositor.cpp
    src/wallpaper/scene/GifAnimation.h
    src/wallpaper/scene/GifAnimation.cpp
    src/wallpaper/scene/SceneBackend.h
    src/wallpaper/scene/SceneBackend.cpp
    src/wallpaper/video/VideoPlayer.h
    src/wallpaper/video/VideoPlayer.cpp
    src/wallpaper/video/VideoBackend.h
    src/wallpaper/video/VideoBackend.cpp
    src/wallpaper/video/VideoThumbnail.h
    src/wallpaper/video/VideoThumbnail.cpp

    src/ui/TrayIcon.h
    src/ui/TrayIcon.cpp
    src/ui/FileDialogs.h
    src/ui/FileDialogs.cpp
    src/ui/UiStyle.h
    src/ui/UiStyle.cpp
    src/ui/ControlPanel.h
    src/ui/ControlPanel.cpp
    src/ui/ControlPanelViews.h
    src/ui/ControlPanelViews.cpp
)

add_executable(WallEnginePaper WIN32
    ${LWE_SOURCES}
    res/app.rc
    res/app.manifest
)

target_include_directories(WallEnginePaper PRIVATE
    src
    res
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_compile_definitions(WallEnginePaper PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00
)

target_compile_options(WallEnginePaper PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc /MP /sdl>
    # Symboles meme en Release (PDB a part, binaire identique) : rend les rapports de plantage lisibles.
    $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Release>>:/Zi>
)
target_link_options(WallEnginePaper PRIVATE
    $<$<CONFIG:Release>:/DEBUG:FULL /OPT:REF /OPT:ICF /INCREMENTAL:NO>
)

target_link_libraries(WallEnginePaper PRIVATE
    lwe::json
    lwe::imgui
    d3d11 dxgi d3dcompiler dxguid dwmapi windowscodecs
    mfplat mfuuid mfreadwrite
    user32 gdi32 shell32 ole32 uuid advapi32 shcore wtsapi32
)
target_compile_definitions(WallEnginePaper PRIVATE LWE_HAS_IMGUI=1)

if(LWE_ENABLE_WEBVIEW2)
    target_sources(WallEnginePaper PRIVATE src/wallpaper/web/WebBackend.h src/wallpaper/web/WebBackend.cpp)
    target_link_libraries(WallEnginePaper PRIVATE lwe::webview2)
    target_compile_definitions(WallEnginePaper PRIVATE LWE_HAS_WEBVIEW2=1)
endif()

# Les fonds d'exemple sont copies a cote de l'executable (chemins relatifs dans config.json).
add_custom_command(TARGET WallEnginePaper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:WallEnginePaper>/assets"
    COMMENT "Copie des assets a cote de l'executable")

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT WallEnginePaper)
set_target_properties(WallEnginePaper PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:WallEnginePaper>")

install(TARGETS WallEnginePaper RUNTIME DESTINATION .)
install(DIRECTORY assets DESTINATION .)
