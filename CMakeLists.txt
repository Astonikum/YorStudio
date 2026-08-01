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
    GIT_TAG 469d88e489c7f70eedd857139139e7958ea64eae
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(yorengine)

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
)
target_compile_features(yorstudio_app PUBLIC cxx_std_20)
target_include_directories(yorstudio_app PUBLIC include)
target_link_libraries(yorstudio_app PUBLIC yorstudio_project yorengine)

if(YORSTUDIO_BUILD_DESKTOP AND WIN32)
    set(YORSTUDIO_UI_ASSET_DIR "${CMAKE_CURRENT_BINARY_DIR}/yorstudio-assets")
    file(MAKE_DIRECTORY "${YORSTUDIO_UI_ASSET_DIR}")
    file(DOWNLOAD
        "https://fonts.gstatic.com/s/googlesans/v70/4Ua_rENHsxJlGDuGo1OIlJfC6l_24rlCK1Yo_Iqcsih3SAyH6cAwhX9RFD48TE63OOYKtrwEIKli.ttf"
        "${YORSTUDIO_UI_ASSET_DIR}/GoogleSans.ttf"
        EXPECTED_HASH SHA256=1DB8FE3048D1B15519999C1B0372285DED006BF49E109455DF1A09E42876EC64
    )
    file(DOWNLOAD
        "https://raw.githubusercontent.com/Templarian/MaterialDesign-Webfont/v7.4.47/fonts/materialdesignicons-webfont.ttf"
        "${YORSTUDIO_UI_ASSET_DIR}/MaterialDesignIcons.ttf"
        EXPECTED_HASH SHA256=61E8ABA5A4E981FE22CF7C8E8BCDBEA00476E75C62C37F01BF7EE33361D68428
    )
    file(TO_CMAKE_PATH "${YORSTUDIO_UI_ASSET_DIR}" YORSTUDIO_UI_ASSET_DIR_CMAKE)

    set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "Disable LunaSVG examples" FORCE)
    FetchContent_Declare(
        lunasvg
        GIT_REPOSITORY https://github.com/sammycage/lunasvg.git
        GIT_TAG 83c58df8103dc7dca423dfd824992af94d49bed6
        GIT_SUBMODULES plutovg
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(lunasvg)

    FetchContent_Declare(
        material_design_icons_svg
        GIT_REPOSITORY https://github.com/Templarian/MaterialDesign-SVG.git
        GIT_TAG 9e04201d4557e729822fb57f62a316c3dea1d4a8
        GIT_SHALLOW FALSE
    )
    FetchContent_GetProperties(material_design_icons_svg)
    if(NOT material_design_icons_svg_POPULATED)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(material_design_icons_svg)
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 NEW)
        endif()
    endif()

    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG 9b4eb24cee2071e61dc1f9ef3e5228097cdde720
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(imgui)

    FetchContent_Declare(
        imwindow
        GIT_REPOSITORY https://github.com/thennequin/ImWindow.git
        GIT_TAG 05f698289da60108f382fb45fa79769d3f0f238f
        GIT_SHALLOW FALSE
    )
    foreach(yor_imwindow_dependency IN ITEMS imwindow)
        FetchContent_GetProperties(${yor_imwindow_dependency})
        if(NOT ${yor_imwindow_dependency}_POPULATED)
            if(POLICY CMP0169)
                cmake_policy(SET CMP0169 OLD)
            endif()
            FetchContent_Populate(${yor_imwindow_dependency})
            if(POLICY CMP0169)
                cmake_policy(SET CMP0169 NEW)
            endif()
        endif()
    endforeach()

    # ImWindow targets the pre-1.87 ImGui API. Keep its Git source pinned and
    # adapt only the removed fields/signatures in the FetchContent build tree.
    set(yor_imwindow_config_file "${imwindow_SOURCE_DIR}/ImwConfig.h")
    file(READ "${yor_imwindow_config_file}" yor_imwindow_config)
    string(REPLACE "\r\n" "\n" yor_imwindow_config "${yor_imwindow_config}")
    if(NOT yor_imwindow_config MATCHES "YORSTUDIO_IMWINDOW_IMGUI_COMPAT")
        string(REPLACE
            "#define IMW_USE_LAYOUT_SERIALIZATION\n#include \"JsonStthm.h\"\n\n//////////////////////////////////////////////////////////////////////////\n// Include here imgui.h\n//////////////////////////////////////////////////////////////////////////\n#include \"imgui.h\"\n#include \"imgui_internal.h\"\n\n#endif // __IM_CONFIG_H__"
            "// YORSTUDIO_IMWINDOW_NO_LAYOUT_SERIALIZATION\n\n//////////////////////////////////////////////////////////////////////////\n// Include here imgui.h\n//////////////////////////////////////////////////////////////////////////\n#include \"imgui.h\"\n#include \"imgui_internal.h\"\n\n#ifndef YORSTUDIO_IMWINDOW_IMGUI_COMPAT\n#define YORSTUDIO_IMWINDOW_IMGUI_COMPAT\n#define ImGuiSetCond_Always ImGuiCond_Always\n#define ImGuiWindowFlags_AlwaysUseWindowPadding 0\nnamespace ImGui { inline bool Begin(const char* name, bool* p_open, const ImVec2&, float, ImGuiWindowFlags flags) { return Begin(name, p_open, flags); } }\n#endif // YORSTUDIO_IMWINDOW_IMGUI_COMPAT\n\n#endif // __IM_CONFIG_H__"
            yor_imwindow_config
            "${yor_imwindow_config}"
        )
        file(WRITE "${yor_imwindow_config_file}" "${yor_imwindow_config}")
    endif()

    set(yor_imwindow_platform_file "${imwindow_SOURCE_DIR}/ImWindow/ImwPlatformWindow.cpp")
    file(READ "${yor_imwindow_platform_file}" yor_imwindow_platform)
    string(REPLACE "\r\n" "\n" yor_imwindow_platform "${yor_imwindow_platform}")
    if(NOT yor_imwindow_platform MATCHES "YORSTUDIO_IMWINDOW_IO_COMPAT")
        string(REPLACE
            "\t\t\tmemcpy(&(oNewIO.KeyMap), &(oGlobalIO.KeyMap ), sizeof( pGlobalContext->IO.KeyMap ));\n\t\t\toNewIO.RenderDrawListsFn = NULL;\n"
            "\t\t\t// YORSTUDIO_IMWINDOW_IO_COMPAT: input events are shared by the native host context.\n"
            yor_imwindow_platform
            "${yor_imwindow_platform}"
        )
        string(REPLACE
            "\t\t\toNewIO.ImeSetInputScreenPosFn = oGlobalIO.ImeSetInputScreenPosFn;\n"
            ""
            yor_imwindow_platform
            "${yor_imwindow_platform}"
        )
        file(WRITE "${yor_imwindow_platform_file}" "${yor_imwindow_platform}")
    endif()

    # ImWindow also predates the current public ImGui names. Patch the
    # generated FetchContent tree; the upstream checkout remains untouched.
    foreach(yor_imwindow_legacy_file IN ITEMS
        "${imwindow_SOURCE_DIR}/ImWindow/ImwContainer.cpp"
        "${imwindow_SOURCE_DIR}/ImWindow/ImwWindowManager.cpp")
        file(READ "${yor_imwindow_legacy_file}" yor_imwindow_legacy)
        string(REPLACE "\r\n" "\n" yor_imwindow_legacy "${yor_imwindow_legacy}")
        if(NOT yor_imwindow_legacy MATCHES "YORSTUDIO_IMWINDOW_IMGUI_1929_COMPAT")
            string(REPLACE "ImGuiCol_ChildWindowBg" "ImGuiCol_ChildBg" yor_imwindow_legacy "${yor_imwindow_legacy}")
            string(REPLACE "ImGui::GetContentRegionAvailWidth()" "ImGui::GetContentRegionAvail().x" yor_imwindow_legacy "${yor_imwindow_legacy}")
            string(REPLACE "ImGui::GetCurrentWindowRead()->MenuBarHeight()" "ImGui::GetCurrentWindowRead()->MenuBarHeight" yor_imwindow_legacy "${yor_imwindow_legacy}")
            string(REPLACE "ImGui::AlignFirstTextHeightToWidgets()" "ImGui::AlignTextToFramePadding()" yor_imwindow_legacy "${yor_imwindow_legacy}")
            string(REPLACE "ImGui::GetCurrentContext()->OverlayDrawList" "*ImGui::GetForegroundDrawList()" yor_imwindow_legacy "${yor_imwindow_legacy}")
            string(REPLACE "ImGui::IsMouseDragging()" "ImGui::IsMouseDragging(0)" yor_imwindow_legacy "${yor_imwindow_legacy}")
            string(REPLACE "PathBezierCurveTo" "PathBezierCubicCurveTo" yor_imwindow_legacy "${yor_imwindow_legacy}")
            string(PREPEND yor_imwindow_legacy "// YORSTUDIO_IMWINDOW_IMGUI_1929_COMPAT\n")
            file(WRITE "${yor_imwindow_legacy_file}" "${yor_imwindow_legacy}")
        endif()
    endforeach()

    set(yor_imwindow_manager_file "${imwindow_SOURCE_DIR}/ImWindow/ImwWindowManager.cpp")
    file(READ "${yor_imwindow_manager_file}" yor_imwindow_manager)
    string(REPLACE "\r\n" "\n" yor_imwindow_manager "${yor_imwindow_manager}")
    if(NOT yor_imwindow_manager MATCHES "YORSTUDIO_IMWINDOW_NO_EMPTY_MAIN_BOTTOM")
        string(REGEX REPLACE
            "float fBottom = pWindow->IsMainWindow\\(\\) \\?[^\\r\\n]*"
            "float fBottom = pWindow->IsMainWindow() ? GetStatusBarHeight() : 0.f;"
            yor_imwindow_manager
            "${yor_imwindow_manager}"
        )
        file(WRITE "${yor_imwindow_manager_file}" "${yor_imwindow_manager}")
    endif()

    file(READ "${yor_imwindow_manager_file}" yor_imwindow_manager)
    string(REPLACE "\r\n" "\n" yor_imwindow_manager "${yor_imwindow_manager}")
    if(NOT yor_imwindow_manager MATCHES "YORSTUDIO_IMWINDOW_ROUNDED_ROOT")
        string(REPLACE
            "oStyle.WindowRounding = 0.f;"
            "oStyle.WindowRounding = fWindowRoundingBackup;"
            yor_imwindow_manager
            "${yor_imwindow_manager}"
        )
        string(PREPEND yor_imwindow_manager "// YORSTUDIO_IMWINDOW_ROUNDED_ROOT\n")
        file(WRITE "${yor_imwindow_manager_file}" "${yor_imwindow_manager}")
    endif()

    file(READ "${yor_imwindow_platform_file}" yor_imwindow_platform)
    string(REPLACE "\r\n" "\n" yor_imwindow_platform "${yor_imwindow_platform}")
    if(NOT yor_imwindow_platform MATCHES "YORSTUDIO_IMWINDOW_IMGUI_1929_COMPAT")
        string(REPLACE
            "ImGui::CreateContext( pGlobalContext->IO.MemAllocFn, pGlobalContext->IO.MemFreeFn )"
            "ImGui::CreateContext()"
            yor_imwindow_platform
            "${yor_imwindow_platform}"
        )
        string(REPLACE
            "\t\t\t\tm_pContext->SetNextWindowPosCond = m_pContext->SetNextWindowSizeCond = m_pContext->SetNextWindowContentSizeCond = m_pContext->SetNextWindowCollapsedCond = m_pContext->SetNextWindowFocus = 0;"
            "\t\t\t\t// Legacy focus-state fields were removed from ImGui; the shared context owns focus."
            yor_imwindow_platform
            "${yor_imwindow_platform}"
        )
        string(REPLACE "m_pContext->IO.KeysDown[i] = false;" "// Legacy KeysDown storage was removed from ImGui." yor_imwindow_platform "${yor_imwindow_platform}")
        string(PREPEND yor_imwindow_platform "// YORSTUDIO_IMWINDOW_IMGUI_1929_COMPAT\n")
        file(WRITE "${yor_imwindow_platform_file}" "${yor_imwindow_platform}")
    endif()

    add_library(yorstudio_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        src/ui/imgui_ui_port.cpp
        src/ui/yor_imwindow_compositor.cpp
        src/ui/yor_icons.cpp
        src/ui/yor_theme.cpp
        src/platform/win32_window.cpp
        ${imwindow_SOURCE_DIR}/ImWindow/ImwContainer.cpp
        ${imwindow_SOURCE_DIR}/ImWindow/ImwMenu.cpp
        ${imwindow_SOURCE_DIR}/ImWindow/ImwPlatformWindow.cpp
        ${imwindow_SOURCE_DIR}/ImWindow/ImwStatusBar.cpp
        ${imwindow_SOURCE_DIR}/ImWindow/ImwToolBar.cpp
        ${imwindow_SOURCE_DIR}/ImWindow/ImwWindow.cpp
        ${imwindow_SOURCE_DIR}/ImWindow/ImwWindowManager.cpp
    )
    target_compile_features(yorstudio_imgui PUBLIC cxx_std_20)
    target_compile_definitions(yorstudio_imgui PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        IMGUI_USE_WCHAR32
        YORSTUDIO_WITH_IMWINDOW=1
        "YORSTUDIO_UI_ASSET_DIR=\"${YORSTUDIO_UI_ASSET_DIR_CMAKE}\""
    )
    target_include_directories(yorstudio_imgui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        src/platform
        ${imwindow_SOURCE_DIR}
        ${imwindow_SOURCE_DIR}/ImWindow
    )
    target_link_libraries(yorstudio_imgui PUBLIC d3d11 dxgi dxguid imm32 shell32 lunasvg::lunasvg)

    add_executable(yorstudio WIN32
        src/app/main.cpp
    )
    target_compile_features(yorstudio PRIVATE cxx_std_20)
    target_include_directories(yorstudio PRIVATE src/platform src/ui)
    target_link_libraries(yorstudio PRIVATE yorstudio_app yorstudio_imgui ole32 shell32)
    add_custom_command(TARGET yorstudio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:yorstudio>/yorstudio-assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${YORSTUDIO_UI_ASSET_DIR}/GoogleSans.ttf"
            "$<TARGET_FILE_DIR:yorstudio>/yorstudio-assets/GoogleSans.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${YORSTUDIO_UI_ASSET_DIR}/MaterialDesignIcons.ttf"
            "$<TARGET_FILE_DIR:yorstudio>/yorstudio-assets/MaterialDesignIcons.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${material_design_icons_svg_SOURCE_DIR}/svg"
            "$<TARGET_FILE_DIR:yorstudio>/yorstudio-assets/icons"
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
