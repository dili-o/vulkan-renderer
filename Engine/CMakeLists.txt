cmake_minimum_required(VERSION 3.8)
project(Engine)

# Set C++ standard globally
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Visual Studio 
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

file(GLOB_RECURSE SOURCE_LIST "Src/*.cpp" "Src/*.hpp")

# Build as a Static Lib
add_library(${PROJECT_NAME} STATIC
    ${SOURCE_LIST}
    "${CMAKE_CURRENT_SOURCE_DIR}/Vendor/tlsf/tlsf.c"
)

if(WIN32)
  target_compile_definitions(${PROJECT_NAME} PUBLIC VK_USE_PLATFORM_WIN32_KHR)
  set(VOLK_STATIC_DEFINES "VK_USE_PLATFORM_WIN32_KHR")
endif()

target_include_directories(${PROJECT_NAME} PUBLIC "Src/")
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Find Vulkan SDK
find_package(Vulkan REQUIRED)

if(Vulkan_FOUND)
  target_link_libraries(${PROJECT_NAME} PUBLIC Vulkan::Vulkan)
else()
    message(FATAL_ERROR "Vulkan SDK not found. Make sure VULKAN_SDK is installed and accessible.")
endif()

#Vendor
add_subdirectory(Vendor/spdlog)
target_link_libraries(${PROJECT_NAME} PUBLIC spdlog::spdlog)

set(SDL_STATIC ON)
add_subdirectory(Vendor/SDL EXCLUDE_FROM_ALL)
target_link_libraries(${PROJECT_NAME} PUBLIC SDL3-static)

target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Vendor/tlsf")

add_subdirectory(Vendor/volk)
target_link_libraries(${PROJECT_NAME} PUBLIC volk_headers)

# Set output directory to Bin/Engine
set_target_properties(${PROJECT_NAME} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../Bin/$<CONFIG>/${PROJECT_NAME}"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../Bin/$<CONFIG>/${PROJECT_NAME}"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../Bin/$<CONFIG>/${PROJECT_NAME}"
)
