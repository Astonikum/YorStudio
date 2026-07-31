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
)
target_compile_features(yorstudio_app PUBLIC cxx_std_20)
target_include_directories(yorstudio_app PUBLIC include)
target_link_libraries(yorstudio_app PUBLIC yorstudio_project)

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
        src/platform/win32_window.cpp
    )
    target_compile_features(yorstudio_imgui PUBLIC cxx_std_20)
    target_compile_definitions(yorstudio_imgui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(yorstudio_imgui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        src/platform
    )
    target_link_libraries(yorstudio_imgui PUBLIC d3d11 dxgi dxguid imm32 shell32)

    add_executable(yorstudio WIN32
        src/app/main.cpp
    )
    target_compile_features(yorstudio PRIVATE cxx_std_20)
    target_include_directories(yorstudio PRIVATE src/platform src/ui)
    target_link_libraries(yorstudio PRIVATE yorstudio_app yorstudio_imgui ole32 shell32)
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
