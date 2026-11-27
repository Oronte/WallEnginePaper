# Gestion des dependances tierces via FetchContent (aucun gestionnaire de paquets requis).
#
# Les archives sont telechargees une seule fois dans build/_deps puis reutilisees.
# Aucune n'est ajoutee via add_subdirectory : on declare nos propres cibles minimales
# (SOURCE_SUBDIR pointe vers un dossier inexistant pour que FetchContent se contente d'extraire).

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# --- nlohmann/json (header-only) : persistance de config.json -------------------------------
FetchContent_Declare(lwe_json_src
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR _lwe_no_cmake)
FetchContent_MakeAvailable(lwe_json_src)

add_library(lwe_json INTERFACE)
target_include_directories(lwe_json SYSTEM INTERFACE "${lwe_json_src_SOURCE_DIR}/single_include")
target_compile_definitions(lwe_json INTERFACE JSON_USE_IMPLICIT_CONVERSIONS=0)
add_library(lwe::json ALIAS lwe_json)

# --- Dear ImGui : fenetre de configuration -------------------------------------------------
block()
    FetchContent_Declare(lwe_imgui_src
        URL      https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.tar.gz
        URL_HASH SHA256=8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR _lwe_no_cmake)
    FetchContent_MakeAvailable(lwe_imgui_src)

    set(_imgui "${lwe_imgui_src_SOURCE_DIR}")
    add_library(lwe_imgui STATIC
        "${_imgui}/imgui.cpp"
        "${_imgui}/imgui_draw.cpp"
        "${_imgui}/imgui_tables.cpp"
        "${_imgui}/imgui_widgets.cpp"
        "${_imgui}/backends/imgui_impl_win32.cpp"
        "${_imgui}/backends/imgui_impl_dx11.cpp")
    target_include_directories(lwe_imgui SYSTEM PUBLIC "${_imgui}" "${_imgui}/backends")
    target_compile_definitions(lwe_imgui PUBLIC
        IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DISABLE_DEMO_WINDOWS UNICODE _UNICODE)
    target_compile_options(lwe_imgui PRIVATE /utf-8 /W0)
    target_link_libraries(lwe_imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi)
    add_library(lwe::imgui ALIAS lwe_imgui)
endblock()

# --- Microsoft WebView2 SDK : backend Web (etape 3) -----------------------------------------
# Le paquet NuGet est une simple archive zip : on en extrait les en-tetes et le loader statique
# (WebView2LoaderStatic.lib) pour ne livrer aucune DLL supplementaire.
if(LWE_ENABLE_WEBVIEW2)
    FetchContent_Declare(lwe_webview2_src
        URL      https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2903.40
        URL_HASH SHA256=ef128016dd1e51c59178c827ed5b8aa3322c57afa8675d930f8109505542ad74
        DOWNLOAD_NAME microsoft.web.webview2.1.0.2903.40.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR _lwe_no_cmake)
    FetchContent_MakeAvailable(lwe_webview2_src)

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64" OR CMAKE_VS_PLATFORM_NAME STREQUAL "ARM64")
        set(_wv2_arch arm64)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_wv2_arch x64)
    else()
        set(_wv2_arch x86)
    endif()

    add_library(lwe_webview2 INTERFACE)
    target_include_directories(lwe_webview2 SYSTEM INTERFACE
        "${lwe_webview2_src_SOURCE_DIR}/build/native/include")
    target_link_libraries(lwe_webview2 INTERFACE
        "${lwe_webview2_src_SOURCE_DIR}/build/native/${_wv2_arch}/WebView2LoaderStatic.lib"
        version)
    add_library(lwe::webview2 ALIAS lwe_webview2)
endif()
