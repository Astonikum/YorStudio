cmake_minimum_required(VERSION 3.25)

project(YorStudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

option(YORSTUDIO_BUILD_DESKTOP "Build the native YorStudio desktop application" ${WIN32})

include(FetchContent)

set(JSON_BuildTests OFF CACHE BOOL "Disable nlohmann/json tests" FORCE)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(nlohmann_json)

set(YORENGINE_BUILD_JNI OFF CACHE BOOL "Disable YorEngine JNI for YorStudio" FORCE)
set(YORENGINE_BUILD_TESTS OFF CACHE BOOL "Disable YorEngine tests inside YorStudio" FORCE)
FetchContent_Declare(
    yorengine
    GIT_REPOSITORY https://github.com/Astonikum/YorEngine.git
    GIT_TAG 0d8e968759aa198427b9563fdbaef750cb23db01
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(yorengine)

set(YORGL_BUILD_JNI OFF CACHE BOOL "Disable YorGL JNI for YorStudio" FORCE)
set(YORGL_BUILD_TESTS OFF CACHE BOOL "Disable YorGL tests inside YorStudio" FORCE)
set(YORGL_BUILD_DX11 ${WIN32} CACHE BOOL "Build YorGL DX11 backend for YorStudio" FORCE)
FetchContent_Declare(
    yorgl
    GIT_REPOSITORY https://github.com/Astonikum/YorGL.git
    GIT_TAG 0497b11ed387e6dbe1528b7b3f90331d1a7c2deb
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(yorgl)

add_library(yorstudio_project STATIC
    src/project/project_manifest.cpp
    src/project/project_lock.cpp
    src/project/project_workspace.cpp
    src/project/project_lifecycle.cpp
)
target_compile_features(yorstudio_project PUBLIC cxx_std_20)
target_include_directories(yorstudio_project PUBLIC include)
target_link_libraries(yorstudio_project PUBLIC nlohmann_json::nlohmann_json)

add_executable(yorstudio_project_validate
    src/cli/validate_project.cpp
)
target_compile_features(yorstudio_project_validate PRIVATE cxx_std_20)
target_link_libraries(yorstudio_project_validate PRIVATE yorstudio_project)

add_library(yorstudio_app STATIC
    src/app/studio_application.cpp
    src/editor/editor_document.cpp
    src/ui/viewport_math.cpp
)
target_compile_features(yorstudio_app PUBLIC cxx_std_20)
target_include_directories(yorstudio_app PUBLIC include)
target_link_libraries(yorstudio_app PUBLIC yorstudio_project yorengine)

if(YORSTUDIO_BUILD_DESKTOP AND WIN32)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG 9b4eb24cee2071e61dc1f9ef3e5228097cdde720
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(imgui)

    add_library(yorstudio_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        src/ui/imgui_ui_port.cpp
        src/ui/yorstudio_viewport.cpp
        src/platform/win32_window.cpp
    )
    target_compile_features(yorstudio_imgui PUBLIC cxx_std_20)
    target_compile_definitions(yorstudio_imgui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(yorstudio_imgui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        src/platform
        ${yorgl_SOURCE_DIR}/src
    )
    target_link_libraries(yorstudio_imgui PUBLIC yorgl d3d11 dxgi dxguid imm32 shell32)

    add_executable(yorstudio WIN32
        src/app/main.cpp
    )
    target_compile_features(yorstudio PRIVATE cxx_std_20)
    target_include_directories(yorstudio PRIVATE src/platform src/ui)
    target_link_libraries(yorstudio PRIVATE yorstudio_app yorstudio_imgui ole32 shell32)
    add_custom_command(TARGET yorstudio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:yorgl> $<TARGET_FILE_DIR:yorstudio>
    )
endif()

if(MSVC)
    target_compile_options(yorstudio_project PRIVATE /W4 /permissive-)
else()
    target_compile_options(yorstudio_project PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(yorstudio_project_smoke tests/project_manifest_smoke.cpp)
target_compile_features(yorstudio_project_smoke PRIVATE cxx_std_20)
target_link_libraries(yorstudio_project_smoke PRIVATE yorstudio_app)
add_test(NAME yorstudio_project_smoke COMMAND yorstudio_project_smoke)
