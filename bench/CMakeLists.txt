add_executable(minisdk_bench bench_allocator.cpp)
target_link_libraries(minisdk_bench PRIVATE minisdk minisdk_warnings)
