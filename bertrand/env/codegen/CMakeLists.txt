cmake_minimum_required(VERSION 3.28)
project(bertrand-ast)

set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Clang REQUIRED)

add_library(bertrand-attrs SHARED bertrand-attrs.cpp)
target_link_libraries(bertrand-attrs PRIVATE
    clang-cpp
    LLVMCore
    LLVMSupport
)
set_target_properties(bertrand-attrs PROPERTIES
    PREFIX ""
)

add_library(bertrand-ast SHARED bertrand-ast.cpp)
target_link_libraries(bertrand-ast PRIVATE
    clang-cpp
    LLVMCore
    LLVMSupport
)
set_target_properties(bertrand-ast PROPERTIES
    PREFIX ""
)

install(TARGETS bertrand-attrs bertrand-ast LIBRARY DESTINATION lib)
