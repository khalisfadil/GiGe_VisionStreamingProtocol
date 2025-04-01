# Minimum CMake version required
cmake_minimum_required(VERSION 3.10)

# Project name and language
project(ParseGVSP LANGUAGES CXX)

# Set C++ standard to C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable AVX2 optimizations (your CPU supports it)
add_compile_options(-mavx2)

# Find OpenCV using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(OpenCV REQUIRED opencv4)

# Include directories (OpenCV and your headers)
include_directories(
    ${OpenCV_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/source/include
)

# Source files
set(SOURCES
    ${CMAKE_SOURCE_DIR}/source/src/gvsp/parse_gvsp.cpp
)

# Header files (optional, for IDE recognition)
set(HEADERS
    ${CMAKE_SOURCE_DIR}/source/include/gvsp/parse_gvsp.hpp
    ${CMAKE_SOURCE_DIR}/source/include/gvsp/gvsp_config.hpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Link OpenCV libraries
target_link_libraries(${PROJECT_NAME} PRIVATE ${OpenCV_LIBRARIES})

# Set output directory for the executable
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)