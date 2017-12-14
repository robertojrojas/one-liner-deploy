cmake_minimum_required(VERSION 2.8)
project(one-liner-deploy)

set (CMAKE_CXX_STANDARD 14)
# Locate the aws sdk for c++ package.
find_package(aws-sdk-cpp)

set(CODE_FILES "")
list(APPEND CODE_FILES "main")

# The executables to build.
foreach(CODE_FILE IN LISTS CODE_FILES)
    add_executable(${CODE_FILE} ${CODE_FILE}.cpp)
    target_link_libraries(${CODE_FILE} aws-cpp-sdk-ec2)
endforeach()