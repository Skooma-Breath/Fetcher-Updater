cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0174)
    cmake_policy(SET CMP0174 NEW)
endif()

project(FetcherLauncher VERSION 0.5.0 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set("CMAKE_RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}" "${CMAKE_BINARY_DIR}/bin")
endforeach()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)

set(WEBVIEW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(WEBVIEW_BUILD_STATIC_LIBRARY OFF CACHE BOOL "" FORCE)
set(WEBVIEW_ENABLE_CHECKS OFF CACHE BOOL "" FORCE)
set(WEBVIEW_USE_STATIC_MSVC_RUNTIME ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    webview
    GIT_REPOSITORY https://github.com/webview/webview.git
    GIT_TAG 0.12.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(webview)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniz GIT_REPOSITORY https://github.com/richgel999/miniz.git GIT_TAG 3.0.2 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(json miniz)
include(cmake/ArchiveDependencies.cmake)
add_library(fetcher-native STATIC src/native_platform.cpp src/native_backend.cpp src/server_manifest.cpp src/archive_codec.cpp src/mod_layout.cpp src/fomod.cpp)
target_include_directories(fetcher-native PUBLIC src)
target_compile_definitions(fetcher-native PUBLIC UNICODE _UNICODE NOMINMAX)
target_link_libraries(fetcher-native PUBLIC nlohmann_json::nlohmann_json miniz archive_static winhttp bcrypt shell32 crypt32 ws2_32 xmllite shlwapi)

add_executable(FetcherLauncher WIN32
    src/main.cpp
    resources/FetcherLauncher.rc
)

target_include_directories(FetcherLauncher PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/resources"
)

target_compile_definitions(FetcherLauncher PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
)

target_link_libraries(FetcherLauncher PRIVATE
    webview::core
    fetcher-native
    shell32
)

if(MSVC)
    target_compile_options(FetcherLauncher PRIVATE /W4 /permissive- /EHsc)
endif()

enable_testing()
add_executable(fetcher-native-tests tests/native_tests.cpp)
target_link_libraries(fetcher-native-tests PRIVATE fetcher-native)
target_compile_definitions(fetcher-native-tests PRIVATE FETCHER_ARCHIVE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/archives")
add_test(NAME fetcher-native COMMAND fetcher-native-tests)
add_executable(fetcher-fomod-tests tests/fomod_tests.cpp)
target_link_libraries(fetcher-fomod-tests PRIVATE fetcher-native)
target_compile_definitions(fetcher-fomod-tests PRIVATE FETCHER_FOMOD_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../tests/fixtures/fomod")
add_test(NAME fetcher-fomod COMMAND fetcher-fomod-tests)

add_custom_command(TARGET FetcherLauncher POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:FetcherLauncher>/ui/assets"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/index.html"
        "$<TARGET_FILE_DIR:FetcherLauncher>/ui/index.html"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/assets/fetcher-float.gif"
        "$<TARGET_FILE_DIR:FetcherLauncher>/ui/assets/fetcher-float.gif"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/assets/fetcher-float-right.gif"
        "$<TARGET_FILE_DIR:FetcherLauncher>/ui/assets/fetcher-float-right.gif"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/assets/potm2504a.jpg"
        "$<TARGET_FILE_DIR:FetcherLauncher>/ui/assets/potm2504a.jpg"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt"
        "$<TARGET_FILE_DIR:FetcherLauncher>/FetcherLauncher-THIRD-PARTY-NOTICES.txt"
    COMMENT "Copying Fetcher Launcher UI, animation assets, and notices"
)
