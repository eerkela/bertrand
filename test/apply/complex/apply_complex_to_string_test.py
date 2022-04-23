import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyComplexToStringMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_complex_to_string_scalar(self):
        na_val = None
        expected = pd.NA
        result = pdtypes.apply._complex_to_string(na_val)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_complex_to_string_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        vec = np.vectorize(pdtypes.apply._complex_to_string)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NA) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_complex_to_string_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        result = pd.Series(nones).apply(pdtypes.apply._complex_to_string)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyComplexToStringAccuracyTests(unittest.TestCase):

    #######################################
    ####    Generic Complex Numbers    ####
    #######################################

    def test_complex_to_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), i + random.random())
                     for i in integers]
        strings = [str(c) for c in complexes]
        for c, s in zip(complexes, strings):
            result = pdtypes.apply._complex_to_string(c)
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_complex_to_string_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), i + random.random())
                     for i in integers]
        strings = [str(c) for c in complexes]
        vec = np.vectorize(pdtypes.apply._complex_to_string)
        result = vec(np.array(complexes))
        expected = np.array(strings)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_complex_to_string_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), i + random.random())
                     for i in integers]
        strings = [str(c) for c in complexes]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_string)
        expected = pd.Series(strings)
        pd.testing.assert_series_equal(result, expected)


class ApplyComplexToStringReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_numpy_complex_type_to_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)]
                     (complex(i + random.random(), i + random.random()))
                     for idx, i in enumerate(integers)]
        strings = [str(c) for c in complexes]
        for c, s in zip(complexes, strings):
            result = pdtypes.apply._complex_to_string(c, return_type=type(s))
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_complex_to_custom_string_class_scalar(self):
        class CustomString:
            def __init__(self, c: complex):
                self.string = str(c)

            def __str__(self) -> str:
                return self.string

        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), i + random.random())
                     for i in integers]
        for c in complexes:
            result = pdtypes.apply._complex_to_string(c, return_type=CustomString)
            self.assertEqual(type(result), CustomString)


if __name__ == "__main__":
    unittest.main()
