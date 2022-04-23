import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyBooleanToIntegerMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_boolean_to_integer_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._boolean_to_integer(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_boolean_to_integer_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._boolean_to_integer)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_boolean_to_integer_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._boolean_to_integer)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyBooleanToIntegerAccuracyTests(unittest.TestCase):

    ################################
    ####    Generic Booleans    ####
    ################################

    def test_boolean_to_integer_scalar(self):
        booleans = [True, False, True, False, True]
        integers = [int(b) for b in booleans]
        for b, i in zip(booleans, integers):
            result = pdtypes.apply._boolean_to_integer(b)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_boolean_to_integer_vector(self):
        booleans = [True, False, True, False, True]
        integers = [int(b) for b in booleans]
        vec = np.vectorize(pdtypes.apply._boolean_to_integer)
        result = vec(np.array(booleans))
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_boolean_to_integer_series(self):
        booleans = [True, False, True, False, True]
        integers = [int(b) for b in booleans]
        result = pd.Series(booleans).apply(pdtypes.apply._boolean_to_integer)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)


class ApplyBooleanToIntegerReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_boolean_to_numpy_signed_integer_scalar(self):
        booleans = [True, False, True, False, True]
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate(booleans)]
        for b, i in zip(booleans, integers):
            result = pdtypes.apply._boolean_to_integer(b, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_standard_boolean_to_numpy_unsigned_integer_scalar(self):
        booleans = [True, False, True, False, True]
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate(booleans)]
        for b, i in zip(booleans, integers):
            result = pdtypes.apply._boolean_to_integer(b, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))


if __name__ == "__main__":
    unittest.main()
