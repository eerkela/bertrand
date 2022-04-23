import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyIntegerToStringMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_integer_to_string_scalar(self):
        na_val = None
        expected = pd.NA
        result = pdtypes.apply._integer_to_string(na_val)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_integer_to_string_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        vec = np.vectorize(pdtypes.apply._integer_to_string)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NA) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_integer_to_string_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        result = pd.Series(nones).apply(pdtypes.apply._integer_to_string)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToStringAccuracyTests(unittest.TestCase):

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i) for i in integers]
        for i, s in zip(integers, strings):
            result = pdtypes.apply._integer_to_string(i)
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_integer_to_string_vector(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._integer_to_string)
        result = vec(np.array(integers))
        expected = np.array(strings)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_to_string_series(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i) for i in integers]
        result = pd.Series(integers).apply(pdtypes.apply._integer_to_string)
        expected = pd.Series(strings)
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToStringReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_numpy_signed_integer_to_string_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        strings = [str(i) for i in integers]
        for i, s in zip(integers, strings):
            result = pdtypes.apply._integer_to_complex(i, return_type=type(s))
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_numpy_unsigned_integer_to_string_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        strings = [str(i) for i in integers]
        for i, s in zip(integers, strings):
            result = pdtypes.apply._integer_to_complex(i, return_type=type(s))
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_standard_integer_to_custom_string_class_scalar(self):
        class CustomString:
            def __init__(self, i: int):
                self.string = str(i)

            def __str__(self) -> str:
                return self.string

        integers = [-2, -1, 0, 1, 2]
        for i in integers:
            result = pdtypes.apply._integer_to_string(i, return_type=CustomString)
            self.assertEqual(type(result), CustomString)


if __name__ == "__main__":
    unittest.main()
