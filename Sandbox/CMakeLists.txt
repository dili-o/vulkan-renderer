cmake_minimum_required(VERSION 3.8)
project(Sandbox)

# Set C++ standard globally
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
#option(GLM_TEST_ENABLE_CXX11 "Enable C++11 features" ON)

# Visual Studio 
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Define your project sources and headers
file(GLOB_RECURSE SOURCE_LIST "Src/*.cpp" "Src/*.hpp")

# Build as an EXE
add_executable(${PROJECT_NAME} 
    ${SOURCE_LIST}
)

target_link_libraries(${PROJECT_NAME} PRIVATE Engine)

target_include_directories(${PROJECT_NAME} PUBLIC "Src/")
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Set output directory to Bin/Sandbox
set_target_properties(${PROJECT_NAME} PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../Bin/$<CONFIG>/${PROJECT_NAME}"
)
