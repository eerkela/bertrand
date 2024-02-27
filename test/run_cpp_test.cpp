#include <gtest/gtest.h>
#include <Python.h>


int main(int argc, char** argv) {
    Py_Initialize();

    ::testing::InitGoogleTest(&argc, argv);
    auto retval = RUN_ALL_TESTS();

    Py_Finalize();
    return retval;
}
