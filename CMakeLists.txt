# The minimum version of CMake that will work.
cmake_minimum_required(VERSION 2.8)

# The project name.
project(TFTPSQS)

# Requires that you build with:
#   -Daws-sdk-cpp_DIR=../sdk_build
find_package(aws-sdk-cpp)

# Link to the AWS SDK for C++ shared libraries.
add_definitions(-DUSE_IMPORT_EXPORT)

# In case something is not in the standard path
#include_directories(/usr/include/libexplain)

#link_directories(/usr/lib/x86_64-linux-gnu/)

# Generate debug info to be able to use gdb and track crash issues
add_compile_options(-g)

# The executable name and its source files.
add_executable(TFTPSQS src/TFTP.cpp src/config.cpp src/main.cpp src/Logger.cpp src/pthread_win.cpp src/AWSSQS.cpp)

# The libraries used by your executable.
target_link_libraries(TFTPSQS aws-cpp-sdk-sqs uuid explain)
