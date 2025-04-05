#!/bin/bash

# 获取所有已修改的C/C++文件
files_cpp=$(git status --porcelain | grep -E '\.(c|cpp|h|hpp)$' | awk '{print $2}')

# 获取所有已修改的CMakeLists.txt文件
files_cmake=$(git status --porcelain | grep -E 'CMakeLists.txt' | awk '{print $2}')


# 循环遍历并格式化 C/C++ 文件
for file in $files_cpp; do
  echo "Formatting C/C++ file: ${file}"
  clang-format -i "$file"
done

# 循环遍历并格式化 CMakeLists.txt 文件
for file in $files_cmake; do
  echo "Formatting CMake file: ${file}"
  cmake-format -i "$file"
done
