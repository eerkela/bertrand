import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyBooleanToStringMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_boolean_to_string_scalar(self):
        na_val = None
        expected = pd.NA
        result = pdtypes.apply._boolean_to_string(na_val)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_boolean_to_string_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        vec = np.vectorize(pdtypes.apply._boolean_to_string)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NA) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_boolean_to_string_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        result = pd.Series(nones).apply(pdtypes.apply._boolean_to_string)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyBooleanToStringAccuracyTests(unittest.TestCase):

    ################################
    ####    Generic Booleans    ####
    ################################

    def test_boolean_to_string_scalar(self):
        booleans = [True, False, True, False, True]
        strings = [str(b) for b in booleans]
        for b, s in zip(booleans, strings):
            result = pdtypes.apply._boolean_to_string(b)
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_boolean_to_string_vector(self):
        booleans = [True, False, True, False, True]
        strings = [str(b) for b in booleans]
        vec = np.vectorize(pdtypes.apply._boolean_to_string)
        result = vec(np.array(booleans))
        expected = np.array(strings)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_boolean_to_string_series(self):
        booleans = [True, False, True, False, True]
        strings = [str(b) for b in booleans]
        result = pd.Series(booleans).apply(pdtypes.apply._boolean_to_string)
        expected = pd.Series(strings)
        pd.testing.assert_series_equal(result, expected)


class ApplyBooleanToStringReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_boolean_to_custom_string_class_scalar(self):
        class CustomString:
            def __init__(self, b: bool):
                self.string = str(b)

            def __str__(self) -> str:
                return self.string

        booleans = [True, False, True, False, True]
        for b in booleans:
            result = pdtypes.apply._boolean_to_string(b, return_type=CustomString)
            self.assertEqual(type(result), CustomString)


if __name__ == "__main__":
    unittest.main()
