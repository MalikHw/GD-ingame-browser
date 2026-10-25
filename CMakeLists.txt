cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(Browser VERSION 1.0.0)

if (NOT WIN32)
    message(WARNING "this only does anything on Windows")
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

CPMAddPackage("gh:matcool/gd-imgui-cocos#b93f08c")
target_link_libraries(${PROJECT_NAME} imgui-cocos)

# NuGet shit
if (WIN32)
    set(WEBVIEW2_VERSION "1.0.2957.106" CACHE STRING "Microsoft.Web.WebView2 NuGet package version")

    if (NOT DEFINED WEBVIEW2_SDK_DIR)
        include(FetchContent)
        FetchContent_Declare(
            webview2sdk
            URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(webview2sdk)
        set(WEBVIEW2_SDK_DIR ${webview2sdk_SOURCE_DIR})
    endif()

    message(STATUS "Using WebView2 SDK at: ${WEBVIEW2_SDK_DIR}")

    find_path(WEBVIEW2_INCLUDE_DIR
        NAMES WebView2.h
        PATHS
            ${WEBVIEW2_SDK_DIR}/build/native/include
            ${WEBVIEW2_SDK_DIR}/include
        NO_DEFAULT_PATH
    )
    if (NOT WEBVIEW2_INCLUDE_DIR)
        message(FATAL_ERROR "Could not find WebView2.h under ${WEBVIEW2_SDK_DIR}")
    endif()

    target_include_directories(${PROJECT_NAME} PRIVATE ${WEBVIEW2_INCLUDE_DIR})

    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_WEBVIEW2_DEFAULT_ARCH "x64")
    else()
        set(_WEBVIEW2_DEFAULT_ARCH "x86")
    endif()
    set(WEBVIEW2_ARCH "${_WEBVIEW2_DEFAULT_ARCH}" CACHE STRING "WebView2 native lib arch dir: x86 | x64 | arm64")
    find_library(WEBVIEW2_LOADER_LIB
        NAMES WebView2LoaderStatic
        PATHS ${WEBVIEW2_SDK_DIR}/build/native/${WEBVIEW2_ARCH}
        NO_DEFAULT_PATH
    )
    if (NOT WEBVIEW2_LOADER_LIB)
        message(FATAL_ERROR "Could not find WebView2LoaderStatic.lib under ${WEBVIEW2_SDK_DIR}/build/native/${WEBVIEW2_ARCH}")
    endif()

    target_link_libraries(${PROJECT_NAME} ${WEBVIEW2_LOADER_LIB})
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        WEBVIEW2_STATIC=1
    )
    target_link_libraries(${PROJECT_NAME} user32 dwmapi shcore)
endif()

setup_geode_mod(${PROJECT_NAME})
