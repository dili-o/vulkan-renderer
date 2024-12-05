cmake_minimum_required (VERSION 3.13)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Release>:Release>")

project(HelixEngine)
set(CMAKE_CXX_STANDARD 17)

# Ensure Vulkan SDK environment variable is correctly normalized
file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" VULKAN_SDK_NORMALIZED)

# Find and include SPIRV-Reflect headers
set(SPIRV_REFLECT_INCLUDE_DIR "${VULKAN_SDK_NORMALIZED}/Source/SPIRV-Reflect")

file(GLOB_RECURSE SOURCE_LIST "src/*.h" "src/*.hpp" "src/*.cpp" 
    "vendor/imgui/*.cpp" 
    "vendor/imgui/*.h"
    "vendor/enkiTS/TaskScheduler.cpp"
    "vendor/tlsf.c"
    "vendor/meshoptimizer/*.cpp"
    "${SPIRV_REFLECT_INCLUDE_DIR}/spirv_reflect.c"
)

# Create the shaders cache directory
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/cache")

add_executable (HelixEngine ${SOURCE_LIST}  "src/Renderer/FrameGraph.cpp" )

find_package(Vulkan REQUIRED)

if(Vulkan_FOUND)
    # Include Vulkan headers
    target_include_directories(HelixEngine PUBLIC ${Vulkan_INCLUDE_DIRS})

    # Link against Vulkan library using keyword signature
    target_link_libraries(HelixEngine PUBLIC ${Vulkan_LIBRARIES})
else()
    message(FATAL_ERROR "Vulkan SDK not found. Make sure it is installed and accessible.")
endif()


target_include_directories(HelixEngine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/")
target_include_directories(HelixEngine PUBLIC "src/")
target_include_directories(HelixEngine PUBLIC "vendor/")
target_include_directories(HelixEngine PUBLIC "vendor/spdlog/include")
target_include_directories(HelixEngine PUBLIC "vendor/SDL2/include")
target_include_directories(HelixEngine PUBLIC "${SPIRV_REFLECT_INCLUDE_DIR}/")

# Vendor Libraries
# Find SDL2 library
find_library(SDL2_LIBRARY SDL2 PATHS ${CMAKE_CURRENT_SOURCE_DIR}/vendor/SDL2/lib/x64)

if(SDL2_LIBRARY)
    target_link_libraries(HelixEngine PRIVATE ${SDL2_LIBRARY})
else()
    message(FATAL_ERROR "SDL2 library not found. Make sure it is installed and accessible.")
endif()

# Set output directory to bin/Engine
set_target_properties(HelixEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/$<CONFIG>/HelixEngine"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/$<CONFIG>/HelixEngine"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin/$<CONFIG>/HelixEngine"
)


target_compile_definitions(HelixEngine PRIVATE
    $<$<CONFIG:DEBUG>:_DEBUG>
    CONSOLE
    UNICODE
    _UNICODE
    VEXPORT
    _CRT_SECURE_NO_WARNINGS
    HELIX_SHADER_FOLDER="${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/"
    HELIX_TEXTURE_FOLDER="${CMAKE_CURRENT_SOURCE_DIR}/assets/textures/"
    HELIX_FRAMEGRAPH_FOLDER="${CMAKE_CURRENT_SOURCE_DIR}/assets/frame_graphs/"
)