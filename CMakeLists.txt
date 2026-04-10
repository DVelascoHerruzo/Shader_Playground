cmake_minimum_required(VERSION 3.20)
project(ShaderPlayground VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Dependencies via FetchContent ----
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9-docking
    GIT_SHALLOW    TRUE)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE)

FetchContent_Declare(fastnoiselite
    GIT_REPOSITORY https://github.com/Auburn/FastNoiseLite.git
    GIT_TAG        v1.1.1
    GIT_SHALLOW    TRUE)

FetchContent_Declare(tinyexr
    GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
    GIT_TAG        v1.0.9
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(imgui glm stb fastnoiselite tinyexr)

# ---- Sources ----
set(IMGUI_DIR ${imgui_SOURCE_DIR})

set(SOURCES
    src/main.cpp
    src/Application.cpp
    src/D3DContext.cpp
    src/Shader.cpp
    src/RenderTarget.cpp
    src/Camera.cpp
    src/Sun.cpp
    src/TerrainGenerator.cpp
    src/Terrain.cpp
    src/Sky.cpp
    src/Water.cpp
    src/ShadowMap.cpp
    src/SSAO.cpp
    src/PostProcess.cpp
    src/Renderer.cpp
    src/UI.cpp
    src/Config.cpp
    src/Foliage.cpp
    src/RayTracer.cpp
    src/EXRLoader.cpp
    "${tinyexr_SOURCE_DIR}/deps/miniz/miniz.c"
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)

add_executable(ShaderPlayground WIN32 ${SOURCES})

target_include_directories(ShaderPlayground PRIVATE
    src/
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${stb_SOURCE_DIR}
    ${fastnoiselite_SOURCE_DIR}/Cpp
    ${tinyexr_SOURCE_DIR}
    ${tinyexr_SOURCE_DIR}/deps/miniz)

target_link_libraries(ShaderPlayground PRIVATE
    glm::glm
    d3d11
    dxgi
    d3dcompiler
    dxguid)

target_compile_definitions(ShaderPlayground PRIVATE
    UNICODE _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    IMGUI_DEFINE_MATH_OPERATORS)

target_compile_options(ShaderPlayground PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W3 /WX- /MP /permissive->)

# ---- Post-build: copy resources ----
add_custom_command(TARGET ShaderPlayground POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:ShaderPlayground>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/textures"
        "$<TARGET_FILE_DIR:ShaderPlayground>/textures"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/assets/snow_textures/snow_02_diff_4k.jpg"
        "$<TARGET_FILE_DIR:ShaderPlayground>/textures/snow_diff.jpg"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/assets/sand_textures/sand_01_diff_4k.jpg"
        "$<TARGET_FILE_DIR:ShaderPlayground>/textures/sand_diff.jpg"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/assets/sand_textures/sand_01_rough_4k.jpg"
        "$<TARGET_FILE_DIR:ShaderPlayground>/textures/sand_rough.jpg"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/assets/sand_textures/sand_01_disp_4k.png"
        "$<TARGET_FILE_DIR:ShaderPlayground>/textures/sand_disp.png"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/assets/sand_textures/sand_01_nor_gl_4k.exr"
        "$<TARGET_FILE_DIR:ShaderPlayground>/textures/sand_nor.exr"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/assets/snow_textures/snow_02_nor_gl_4k.exr"
        "$<TARGET_FILE_DIR:ShaderPlayground>/textures/snow_nor.exr"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_SOURCE_DIR}/assets/snow_textures/snow_02_rough_4k.jpg"
        "$<TARGET_FILE_DIR:ShaderPlayground>/textures/snow_rough.jpg"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ShaderPlayground>/assets"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ShaderPlayground>/reshade-shaders/Shaders"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ShaderPlayground>/reshade-shaders/Textures"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:ShaderPlayground>/screenshots"
    VERBATIM)

# ---- Install target (create dist/) ----
install(TARGETS ShaderPlayground DESTINATION dist)
install(DIRECTORY shaders/  DESTINATION dist/shaders)
install(DIRECTORY assets/   DESTINATION dist/assets)
install(DIRECTORY textures/ DESTINATION dist/textures)
install(DIRECTORY reshade-shaders/ DESTINATION dist/reshade-shaders)
