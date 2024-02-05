cmake_minimum_required(VERSION 3.20)
project(minisdk LANGUAGES CXX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MINISDK_BUILD_TESTS "Build the unit test suite" ON)
option(MINISDK_TRACE "Print lifetime/IO tracing from the library (slow, noisy)" OFF)
set(MINISDK_SANITIZE "" CACHE STRING "Sanitizer to enable: address, thread, undefined")

# Applied before any target so fetched dependencies are instrumented too, which TSan requires.
if(MINISDK_SANITIZE)
    add_compile_options(-fsanitize=${MINISDK_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${MINISDK_SANITIZE})
endif()

# Kept off third-party code: -Werror on a dependency turns its warnings into our build failures.
add_library(minisdk_warnings INTERFACE)
target_compile_options(minisdk_warnings INTERFACE -Wall -Wextra -Wshadow -Werror)

add_library(minisdk
    src/buffer.cpp
    src/SimulatedDevice.cpp
    src/DeviceManager.cpp
    src/EventBus.cpp
    src/ThreadPool.cpp
)
target_include_directories(minisdk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(minisdk PRIVATE minisdk_warnings)

if(MINISDK_TRACE)
    target_compile_definitions(minisdk PUBLIC MINISDK_TRACE)
endif()

find_package(Threads REQUIRED)
target_link_libraries(minisdk PUBLIC Threads::Threads)

add_executable(minisdk_demo src/main.cpp)
target_link_libraries(minisdk_demo PRIVATE minisdk minisdk_warnings)

if(MINISDK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()