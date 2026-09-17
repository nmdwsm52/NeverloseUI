cmake_minimum_required(VERSION 3.20)
project(NeverloseUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    message(FATAL_ERROR "This project targets MSVC (Windows / DirectX 11).")
endif()

if(NOT DEFINED IMGUI_DIR)
    set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../imgui" CACHE PATH "Dear ImGui source directory")
endif()
if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR "Dear ImGui not found at '${IMGUI_DIR}'. Pass -DIMGUI_DIR=<path>.")
endif()

# ---------------- 公共代码（UI / 功能 / 内存层） ----------------
set(NL_COMMON
    src/core/log.cpp
    src/core/paths.cpp
    src/core/memory.cpp
    src/core/sigscan.cpp
    src/core/vmt.cpp
    src/ui/style.cpp
    src/ui/icons.cpp
    src/ui/widgets.cpp
    src/ui/menu.cpp
    src/ui/tab_memory.cpp
    src/features/config.cpp
    src/features/esp.cpp
    src/features/thirdperson.cpp
    src/features/bind.cpp
    src/features/convar.cpp
    src/features/visuals.cpp
    src/features/antiaim.cpp
    src/features/createmove.cpp
    src/features/misc.cpp
    src/features/input.cpp
    src/features/aimbot.cpp
    src/features/offsets.cpp
    src/features/bonemap.cpp
    src/features/game.cpp
)

set(NL_IMGUI
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
)

set(NL_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

set(NL_DEFINITIONS
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    IMGUI_DEFINE_MATH_OPERATORS
)

# ---------------- 外部版（独立窗口覆盖层） ----------------
add_executable(NeverloseUI WIN32
    src/main.cpp
    src/render/renderer.cpp
    ${NL_COMMON}
    ${NL_IMGUI}
)
target_include_directories(NeverloseUI PRIVATE ${NL_INCLUDES})
target_compile_definitions(NeverloseUI PRIVATE ${NL_DEFINITIONS})
target_compile_options(NeverloseUI PRIVATE /utf-8 /MP /W3 /wd4244 /wd4267)
target_link_libraries(NeverloseUI PRIVATE d3d11 dxgi d3dcompiler dwmapi dcomp user32 gdi32 shell32 ole32 imm32)

# ---------------- 注入版 DLL（VMT Hook Present + F6 卸载） ----------------
add_library(NeverloseUI_DLL SHARED
    src/dll/dllmain.cpp
    src/dll/hooks.cpp
    src/render/render_internal.cpp
    ${NL_COMMON}
    ${NL_IMGUI}
)
target_include_directories(NeverloseUI_DLL PRIVATE ${NL_INCLUDES})
target_compile_definitions(NeverloseUI_DLL PRIVATE ${NL_DEFINITIONS})
target_compile_options(NeverloseUI_DLL PRIVATE /utf-8 /MP /W3 /wd4244 /wd4267)
target_link_libraries(NeverloseUI_DLL PRIVATE d3d11 dxgi d3dcompiler user32 gdi32 shell32 ole32 imm32)
set_target_properties(NeverloseUI_DLL PROPERTIES OUTPUT_NAME "NeverloseUI")

# ---------------- 自检用假游戏（内存） ----------------
add_executable(NLTestTarget tools/test_target/main.cpp)
target_compile_options(NLTestTarget PRIVATE /utf-8 /W3)
target_link_libraries(NLTestTarget PRIVATE user32)

# ---------------- 自检用假游戏（渲染/注入） ----------------
add_executable(NLHookHost WIN32 tools/hook_host/main.cpp)
target_compile_options(NLHookHost PRIVATE /utf-8 /W3)
target_link_libraries(NLHookHost PRIVATE d3d11 dxgi user32 gdi32)

# ---------------- 标准 LoadLibrary 注入器 ----------------
add_executable(NLInjector tools/injector/main.cpp)
target_compile_options(NLInjector PRIVATE /utf-8 /W3)
target_link_libraries(NLInjector PRIVATE user32)

# ---------------- 骨骼表诊断 / 骨骼分类器回归（只读，外部模式） ----------------
add_executable(NLBoneProbe
    tools/bone_probe/main.cpp
    src/core/log.cpp
    src/core/paths.cpp
    src/core/memory.cpp
    src/core/sigscan.cpp
    src/features/offsets.cpp
    src/features/bonemap.cpp
)
target_include_directories(NLBoneProbe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(NLBoneProbe PRIVATE ${NL_DEFINITIONS})
target_compile_options(NLBoneProbe PRIVATE /utf-8 /W3 /wd4244 /wd4267)
target_link_libraries(NLBoneProbe PRIVATE user32)







