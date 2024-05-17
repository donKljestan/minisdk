include(FetchContent)

# Prefers a Catch2 already installed on the machine (apt install catch2) and only falls back
# to cloning, so the build still works behind a proxy that blocks github.com.
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
    GIT_SHALLOW    TRUE
    FIND_PACKAGE_ARGS 3 NAMES Catch2
)
FetchContent_MakeAvailable(Catch2)

add_executable(minisdk_tests
    test_buffer.cpp
    test_register_map.cpp
    test_simulated_device.cpp
    test_event_bus.cpp
    test_device_manager.cpp
    test_thread_pool.cpp
)
target_link_libraries(minisdk_tests PRIVATE minisdk minisdk_warnings Catch2::Catch2WithMain)

# When Catch2 is fetched, its CMake helpers live in the source tree; an installed package
# puts them on the module path itself.
if(catch2_SOURCE_DIR)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()
include(Catch)
# PRE_TEST keeps enumeration out of the build step, so a runtime problem surfaces as a failing
# test instead of a failed build.
catch_discover_tests(minisdk_tests DISCOVERY_MODE PRE_TEST)
