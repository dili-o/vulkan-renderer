cmake_minimum_required(VERSION 3.8)
project(HelixEngine)

# Projects
add_subdirectory("Engine")
add_subdirectory("Sandbox")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Path to assests folder
#target_compile_definitions(Engine PUBLIC ASSETS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/bin/Assets")

# Set preprocessor definitions
target_compile_definitions(Engine PRIVATE
    $<$<CONFIG:DEBUG>:_DEBUG>
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
)

target_compile_definitions(Sandbox PRIVATE
    $<$<CONFIG:DEBUG>:_DEBUG>
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
)

# Set the startup project to Sandbox
if(MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Sandbox)
endif()
