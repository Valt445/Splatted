cmake_minimum_required(VERSION 3.25)
project(splatter LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Dependencies (all pinned; fetched into build/<preset>/_deps on first configure)
# ---------------------------------------------------------------------------

# Vulkan headers are pinned to the exact version vk-bootstrap is generated
# against, so the build doesn't depend on how old the distro's headers are.
# Only the loader library (libvulkan / vulkan-1) comes from the system.
find_library(VULKAN_LOADER NAMES vulkan vulkan-1
    HINTS "$ENV{VULKAN_SDK}/lib" "$ENV{VULKAN_SDK}/Lib" REQUIRED)

include(FetchContent)

FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers
    GIT_TAG        v1.4.366
    GIT_SHALLOW    TRUE
    SYSTEM)
FetchContent_MakeAvailable(vulkan_headers)   # defines Vulkan::Headers

FetchContent_Declare(vk_bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap
    GIT_TAG        v1.4.366
    GIT_SHALLOW    TRUE
    SYSTEM)

FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    GIT_TAG        v3.3.0
    GIT_SHALLOW    TRUE
    SYSTEM)

set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm
    GIT_TAG        1.0.3
    GIT_SHALLOW    TRUE
    SYSTEM)

# cgltf is a single C header with no CMake project: we only download it.
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf
    GIT_TAG        v1.15
    GIT_SHALLOW    TRUE
    SYSTEM)

FetchContent_MakeAvailable(vk_bootstrap vma glm cgltf)

# Prefer the system GLFW (Fedora: glfw-devel), fall back to fetching it (Windows).
find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
        SYSTEM)
    FetchContent_MakeAvailable(glfw)
endif()

# ---------------------------------------------------------------------------
# Shaders: every shaders/*.comp|vert|frag becomes build/<preset>/shaders/<name>.spv
# (e.g. shaders/splat.vert -> build/debug/shaders/splat.vert.spv)
# ---------------------------------------------------------------------------
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin" REQUIRED)

file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/shaders/*.comp"
    "${CMAKE_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_SOURCE_DIR}/shaders/*.frag")

set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

set(SHADER_BINARIES "")
foreach(src IN LISTS SHADER_SOURCES)
    get_filename_component(name "${src}" NAME)
    set(spv "${SHADER_OUT_DIR}/${name}.spv")
    add_custom_command(
        OUTPUT  "${spv}"
        COMMAND "${GLSLC}" --target-env=vulkan1.3 -g
                -MD -MF "${spv}.d"
                "${src}" -o "${spv}"
        DEPFILE "${spv}.d"
        MAIN_DEPENDENCY "${src}"
        COMMENT "glslc ${name}"
        VERBATIM)
    list(APPEND SHADER_BINARIES "${spv}")
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_BINARIES})

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(splatter
    src/main.cpp
    src/base.cpp
    src/camera.cpp
    src/gltf_loader.cpp
    src/splat_pipeline.cpp
    src/third_party.cpp)

target_include_directories(splatter PRIVATE src)
target_include_directories(splatter SYSTEM PRIVATE "${cgltf_SOURCE_DIR}")
target_link_libraries(splatter PRIVATE
    Vulkan::Headers
    ${VULKAN_LOADER}
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator
    glm::glm
    glfw)
target_compile_definitions(splatter PRIVATE
    SHADER_DIR="${SHADER_OUT_DIR}"
    ASSET_DIR="${CMAKE_SOURCE_DIR}/assets"
    GLFW_INCLUDE_NONE
    GLM_FORCE_DEPTH_ZERO_TO_ONE      # Vulkan depth range is [0, 1]
    GLM_FORCE_RADIANS)
add_dependencies(splatter shaders)

if(MSVC)
    target_compile_options(splatter PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(splatter PRIVATE _CRT_SECURE_NO_WARNINGS)  # fopen etc. are fine
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT splatter)
    set_source_files_properties(src/third_party.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
    target_compile_options(splatter PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion
        -Wno-missing-field-initializers)   # {VK_STRUCTURE_TYPE_...} zero-fills the rest on purpose
    set_source_files_properties(src/third_party.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
