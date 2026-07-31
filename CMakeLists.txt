cmake_minimum_required(VERSION 3.25)

project(YorStudio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

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
)
target_compile_features(yorstudio_project PUBLIC cxx_std_20)
target_include_directories(yorstudio_project PUBLIC include)
target_link_libraries(yorstudio_project PUBLIC nlohmann_json::nlohmann_json)

add_executable(yorstudio_project_validate
    src/cli/validate_project.cpp
)
target_compile_features(yorstudio_project_validate PRIVATE cxx_std_20)
target_link_libraries(yorstudio_project_validate PRIVATE yorstudio_project)

if(MSVC)
    target_compile_options(yorstudio_project PRIVATE /W4 /permissive-)
else()
    target_compile_options(yorstudio_project PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(yorstudio_project_smoke tests/project_manifest_smoke.cpp)
target_compile_features(yorstudio_project_smoke PRIVATE cxx_std_20)
target_link_libraries(yorstudio_project_smoke PRIVATE yorstudio_project)
add_test(NAME yorstudio_project_smoke COMMAND yorstudio_project_smoke)
