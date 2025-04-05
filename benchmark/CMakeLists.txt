file(GLOB SRC_BENCHMARK CONFIGURE_DEPENDS *.cpp)

add_executable(benchmark ${SRC_BENCHMARK})

target_link_libraries(benchmark libclient)
target_link_libraries(benchmark benchmark::benchmark)