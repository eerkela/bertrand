cmake_minimum_required(VERSION 3.28)
project(bertrand-ast)

set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Clang REQUIRED)

add_executable(bertrand-ast bertrand-ast.cpp)
target_link_libraries(bertrand-ast PRIVATE
    clang-cpp
    LLVMCore
    LLVMSupport
)

install(TARGETS bertrand-ast RUNTIME DESTINATION bin)
