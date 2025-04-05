# 查找 dbg.h 头文件所在路径
find_path(DBG_MACRO_INCLUDE_DIRS "dbg-macro/dbg.h")

set(Protobuf_IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR})

# 获取其他源文件
file(GLOB SRC_COMMON CONFIGURE_DEPENDS common/*.cpp)
file(
  GLOB
  SRC_DAEMON
  CONFIGURE_DEPENDS
  client/config.cpp
  client/daemon_upload.cpp
  client/sdk.cpp
  client/daemon_main.cpp
  client/daemon_service.cpp
  client/main.cpp)

# 创建 libcommon 静态库
add_library(libcommon STATIC ${SRC_COMMON})
# 创建 libdaemon 静态库
add_library(libdaemon STATIC ${SRC_DAEMON})

# 设置 protobuf 文件路径
set(PROTO_FILE common/common.proto common/namenode.proto common/datanode.proto)
# 设置 daemon protobuf 路径
set(PROTO_FILE_DAEMON common/daemon.proto)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/common)
# 使用 protobuf_generate 生成 protobuf 文件
protobuf_generate(
  TARGET
  libcommon
  PROTOS
  ${PROTO_FILE}
  OUT_VAR
  PROTO_COMMON_SRCS
  IMPORT_DIRS
  ${Protobuf_IMPORT_DIRS})
protobuf_generate(
  TARGET
  libdaemon
  PROTOS
  ${PROTO_FILE_DAEMON}
  OUT_VAR
  PROTO_DAEMON_SRCS
  IMPORT_DIRS
  ${Protobuf_IMPORT_DIRS})

# 设置头文件目录
include_directories(libcommon PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}
                    ${DBG_MACRO_INCLUDE_DIRS} ../include)
if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
  target_link_libraries(libcommon PUBLIC unofficial::brpc::brpc-shared)
  target_link_libraries(libdaemon PRIVATE unofficial::brpc::brpc-shared)
else()
  target_link_libraries(libcommon PUBLIC unofficial::brpc::brpc-static)
  target_link_libraries(libdaemon PRIVATE unofficial::brpc::brpc-static)
endif()

# 链接相关库
target_link_libraries(
  libcommon PUBLIC gflags::gflags nlohmann_json::nlohmann_json glog::glog
                   PkgConfig::LIBERASURECODE cryptopp::cryptopp)

# 如果启用了 BUILD_NODE 选项
if(BUILD_NODE)
  find_package(unofficial-braft CONFIG REQUIRED)
  find_package(unofficial-breakpad CONFIG REQUIRED)
  find_package(RocksDB CONFIG REQUIRED)

  # 获取节点相关的源文件
  file(GLOB SRC_NODE CONFIGURE_DEPENDS node/*.cpp)

  # 创建 node_main 可执行文件
  add_executable(node_main ${SRC_NODE})

  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/node)
  if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
    target_link_libraries(node_main PUBLIC unofficial::braft::braft-shared)
  else()
    target_link_libraries(node_main PUBLIC unofficial::braft::braft-static)
  endif()
  target_link_libraries(
    node_main PUBLIC libcommon unofficial::breakpad::libbreakpad
                     unofficial::breakpad::libbreakpad_client RocksDB::rocksdb)
endif(BUILD_NODE)

# 如果启用了 BUILD_CLIENT 选项
option(BUILD_CLIENT "Build the client target" ON)
if(BUILD_CLIENT)
  pkg_check_modules(FUSE3 REQUIRED IMPORTED_TARGET GLOBAL fuse3)

  add_library(libclient STATIC client/sdk.cpp client/config.cpp
                               client/daemon_upload_queue.cpp)

  file(GLOB SRC_FUSE CONFIGURE_DEPENDS client/fuse.cpp)

  add_executable(client client/main.cpp)
  add_executable(fuse ${SRC_FUSE})
  add_executable(
    daemon
    client/daemon_main.cpp client/daemon.cpp client/daemon_download.cpp
    client/daemon_service.cpp client/daemon_upload.cpp
    client/daemon_upload_queue.cpp)
  target_link_libraries(daemon PUBLIC libdaemon libcommon libclient)

  target_include_directories(
    libcommon PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}
                     ${DBG_MACRO_INCLUDE_DIRS} ../include)

  target_link_libraries(libclient PUBLIC libcommon)
  target_link_libraries(fuse PUBLIC PkgConfig::FUSE3 libclient)
  target_link_libraries(client PUBLIC libclient libdaemon)
endif(BUILD_CLIENT)
