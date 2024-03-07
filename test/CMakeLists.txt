cmake_minimum_required(VERSION 3.14)
project(bertrandTests)

# ensure C++17
set(
    CMAKE_CXX_STANDARD 20
    CMAKE_CXX_STANDARD_REQUIRED ON
)

# include all .cpp files in the test/ directory or any of its subdirectories.
# NOTE: these should always either start with "test_" or end in "_test" in order to be
# discoverable by the test runner.
file(GLOB_RECURSE TEST_SOURCES "${PROJECT_SOURCE_DIR}/*.cpp")
list(FILTER TEST_SOURCES EXCLUDE REGEX "^.*/build/.*$")  # filter out cmake artifacts

# apply compilation flags saved from setup.py
file(READ ${PROJECT_SOURCE_DIR}/.compile_flags COMPILE_FLAGS)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMPILE_FLAGS}")

# Find Python and its include directories
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

# get pybind11 include path
execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import pybind11; print(pybind11.get_include())"
    OUTPUT_VARIABLE PYBIND11_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# get numpy include path
execute_process(
    COMMAND ${Python3_EXECUTABLE} -c "import numpy; print(numpy.get_include())"
    OUTPUT_VARIABLE NUMPY_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# include Python, GoogleTest, pybind11, and bertrand source
include_directories(
    ${Python3_INCLUDE_DIRS}
    ${PYBIND11_INCLUDE_DIR}
    ${NUMPY_INCLUDE_DIR}
    ${PROJECT_SOURCE_DIR}/../third_party/googletest-1.14.0/googletest/include
    ${PROJECT_SOURCE_DIR}/../third_party/pcre2-10.43/src
    ${PROJECT_SOURCE_DIR}/..
)

# link against GoogleTest, PCRE2
link_directories(
    ${PROJECT_SOURCE_DIR}/../third_party/googletest-1.14.0/build/lib
    ${PROJECT_SOURCE_DIR}/../third_party/pcre2-10.43/.libs
)

# specify the executable
add_executable(run_cpp_test ${TEST_SOURCES})

# link against GoogleTest, PCRE2
target_link_libraries(run_cpp_test ${Python3_LIBRARIES} gtest pcre2-8)
