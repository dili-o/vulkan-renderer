cmake_minimum_required(VERSION 3.8)
project(HelixEngine)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Projects
add_subdirectory("Engine")
add_subdirectory("Sandbox")


# Create the shaders cache directory
file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Engine/Assets/Shaders/Caches")

# Path to assests folder
target_compile_definitions(Engine PUBLIC ASSETS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/Engine/Assets")

# Set preprocessor definitions
target_compile_definitions(Engine PRIVATE
    $<$<CONFIG:DEBUG>:_DEBUG>
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    VK_NO_PROTOTYPES
)

target_compile_definitions(Sandbox PRIVATE
    $<$<CONFIG:DEBUG>:_DEBUG>
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    VK_NO_PROTOTYPES
)

# Set the startup project to Sandbox
if(MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Sandbox)
endif()
