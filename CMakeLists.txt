cmake_minimum_required(VERSION 3.20)

project(
    AetherProxy
    VERSION 0.1.0
    DESCRIPTION "C++20 Intelligent Cloud Reverse Proxy"
    LANGUAGES CXX
)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies
find_package(Boost 1.74 REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

# Executable
add_executable(aetherproxy

    src/main.cpp

    src/core/ProxyServer.cpp

    src/networking/HttpListener.cpp
    src/networking/HttpSession.cpp
)

# Include directories
target_include_directories(aetherproxy
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

# Link dependencies
target_link_libraries(aetherproxy
    PRIVATE
        Boost::system
        Threads::Threads
)

# Compiler warnings
target_compile_options(aetherproxy
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

# C++ standard
target_compile_features(aetherproxy
    PRIVATE
        cxx_std_20
)