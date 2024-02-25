cmake_minimum_required(VERSION 3.14)
project(bertrandTests)

# include GoogleTest, PCRE2, and bertrand source
include_directories(
    ${PROJECT_SOURCE_DIR}/../third_party/googletest-1.14.0/googletest/include
    ${PROJECT_SOURCE_DIR}/../third_party/pcre2-10.43/src
    ${PROJECT_SOURCE_DIR}/../bertrand
)

# link against GoogleTest, PCRE2
link_directories(
    ${PROJECT_SOURCE_DIR}/../third_party/googletest-1.14.0/build/lib
    ${PROJECT_SOURCE_DIR}/../third_party/pcre2-10.43/.libs
)

# add test source files here
file(GLOB TEST_SOURCES "${PROJECT_SOURCE_DIR}/*.cpp")

# specify the executable
add_executable(cpp_test ${TEST_SOURCES})

# link against GoogleTest, PCRE2, and bertrand
target_link_libraries(cpp_test gtest pcre2-8)
