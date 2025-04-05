include(GoogleTest)

# 查找所有的测试文件
file(GLOB_RECURSE SPKDFS_TEST_SOURCES "${PROJECT_SOURCE_DIR}/test/src/*test.cpp")

# #####################################################################################################################
# MAKE TARGETS
# #####################################################################################################################

# #########################################
# "make check-tests"
# #########################################
add_custom_target(build-tests COMMAND ${CMAKE_CTEST_COMMAND} --show-only)
add_custom_target(check-tests COMMAND ${CMAKE_CTEST_COMMAND} --verbose)

# #########################################
# 为每个测试创建目标
# #########################################
foreach (spkdfs_test_source ${SPKDFS_TEST_SOURCES})
    # 创建一个人类可读的名字
    get_filename_component(spkdfs_test_filename ${spkdfs_test_source} NAME)
    string(REPLACE ".cpp" "" spkdfs_test_name ${spkdfs_test_filename})

    # 创建单独的测试可执行文件
    add_executable(${spkdfs_test_name} ${spkdfs_test_source})
    add_dependencies(build-tests ${spkdfs_test_name})
    add_dependencies(check-tests ${spkdfs_test_name})

    # 发现并运行 GoogleTest 测试
    gtest_discover_tests(${spkdfs_test_name}
            EXTRA_ARGS
            --gtest_color=auto
            --gtest_output=xml:${CMAKE_BINARY_DIR}/test/${spkdfs_test_name}.xml
            --gtest_catch_exceptions=0
            DISCOVERY_TIMEOUT 120
            PROPERTIES
            TIMEOUT 120
            )

    # 链接 GoogleTest 和 GoogleMock 库
    target_link_libraries(${spkdfs_test_name} libclient GTest::gtest GTest::gmock_main)

    # 设置目标属性
    set_target_properties(${spkdfs_test_name}
            PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test"
            )

endforeach ()


# file(GLOB SRC_TEST CONFIGURE_DEPENDS *.cpp)

# add_executable(mytest ${SRC_TEST})

# target_link_libraries(mytest libclient GTest::gtest)

# add_test(NAME UnitTests COMMAND mytest)

# List of file sizes to test (in bytes)
set(FILE_SIZES
    10
    100
    1000
    10000
    100000
    1000000
    10000000
    100000000
    1000000000
    10000000000)

# Loop over each file size and create a custom command for it
foreach(SIZE IN LISTS FILE_SIZES)
  add_test(
    NAME RunCommandLineTest_${SIZE}
    COMMAND
      bash ${CMAKE_SOURCE_DIR}/test/src/command_line_interface_test.sh ${SIZE} ${CMAKE_BINARY_DIR}/src/client
      COMMENT
      "Running the file generation, upload, and comparison script for ${SIZE} bytes"
  )
endforeach()
