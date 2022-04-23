import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyFloatToStringMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_float_to_string_scalar(self):
        na_val = None
        expected = pd.NA
        result = pdtypes.apply._float_to_string(na_val)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_float_to_string_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        vec = np.vectorize(pdtypes.apply._float_to_string)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NA) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_float_to_string_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        result = pd.Series(nones).apply(pdtypes.apply._float_to_string)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToStringAccuracyTests(unittest.TestCase):

    ##############################
    ####    Generic Floats    ####
    ##############################

    def test_float_to_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        strings = [str(f) for f in floats]
        for s, f in zip(strings, floats):
            result = pdtypes.apply._float_to_string(f)
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_float_to_string_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        strings = [str(f) for f in floats]
        vec = np.vectorize(pdtypes.apply._float_to_string)
        result = vec(np.array(floats))
        expected = np.array(strings)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_to_string_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        strings = [str(f) for f in floats]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_string)
        expected = pd.Series(strings)
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToStringReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_numpy_float_type_to_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        strings = [str(f) for f in floats]
        for f, s in zip(floats, strings):
            result = pdtypes.apply._float_to_string(f, return_type=type(s))
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_float_to_custom_string_class_scalar(self):
        class CustomString:
            def __init__(self, f: float):
                self.string = str(f)

            def __str__(self) -> str:
                return self.string

        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        for f in floats:
            result = pdtypes.apply._float_to_string(f, return_type=CustomString)
            self.assertEqual(type(result), CustomString)


if __name__ == "__main__":
    unittest.main()
