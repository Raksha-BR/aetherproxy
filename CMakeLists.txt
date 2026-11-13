
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
find_package(yaml-cpp REQUIRED)

include(CTest)

# Main executable
add_executable(aetherproxy
    src/main.cpp
    src/core/ProxyServer.cpp
    src/core/ProxyMetrics.cpp
    src/networking/BackendConnectionPool.cpp
    src/networking/HttpListener.cpp
    src/networking/HttpSession.cpp
    src/core/BackendRegistry.cpp
    src/core/BackendHealthMonitor.cpp
    src/config/ProxyConfig.cpp
)

target_include_directories(aetherproxy
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(aetherproxy
    PRIVATE
        Boost::system
        Threads::Threads
        yaml-cpp
)

target_compile_options(aetherproxy
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)

target_compile_features(aetherproxy
    PRIVATE
        cxx_std_20
)

# Tests
if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    # Unit tests
    add_executable(aetherproxy_tests
        tests/unit/BackendRegistryTest.cpp
        tests/unit/ProxyMetricsTest.cpp
        tests/unit/BackendConnectionPoolTest.cpp
        tests/unit/ProxyConfigTest.cpp

        src/core/BackendRegistry.cpp
        src/core/ProxyMetrics.cpp
        src/networking/BackendConnectionPool.cpp
        src/config/ProxyConfig.cpp
    )

    target_include_directories(aetherproxy_tests
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
    )

    target_link_libraries(aetherproxy_tests
        PRIVATE
            GTest::gtest_main
            Threads::Threads
            Boost::system
            yaml-cpp
    )

    target_compile_options(aetherproxy_tests
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )

    include(GoogleTest)
    gtest_discover_tests(aetherproxy_tests)

    # HTTP forwarding integration test
    add_test(
        NAME http_forwarding_integration
        COMMAND
            ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/integration/http_forwarding_test.py
            $<TARGET_FILE:aetherproxy>
    )

    set_tests_properties(
        http_forwarding_integration
        PROPERTIES TIMEOUT 25
    )

    # Round-robin integration test
    add_test(
        NAME round_robin_integration
        COMMAND
            ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/integration/round_robin_test.py
            $<TARGET_FILE:aetherproxy>
    )

    set_tests_properties(
        round_robin_integration
        PROPERTIES TIMEOUT 30
    )

    # Backend failure and recovery integration test
    add_test(
        NAME health_failover_integration
        COMMAND
            ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/integration/health_failover_test.py
            $<TARGET_FILE:aetherproxy>
    )

    set_tests_properties(
        health_failover_integration
        PROPERTIES TIMEOUT 45
    )
endif()
