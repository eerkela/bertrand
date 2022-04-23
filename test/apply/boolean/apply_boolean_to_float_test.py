import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyBooleanToFloatMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_boolean_to_float_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._boolean_to_float(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_boolean_to_float_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._boolean_to_float)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_boolean_to_float_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._boolean_to_float)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyBooleanToIntegerAccuracyTests(unittest.TestCase):

    ################################
    ####    Generic Booleans    ####
    ################################

    def test_boolean_to_float_scalar(self):
        booleans = [True, False, True, False, True]
        floats = [float(b) for b in booleans]
        for b, f in zip(booleans, floats):
            result = pdtypes.apply._boolean_to_float(b)
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))

    def test_boolean_to_float_vector(self):
        booleans = [True, False, True, False, True]
        floats = [float(b) for b in booleans]
        vec = np.vectorize(pdtypes.apply._boolean_to_float)
        result = vec(np.array(booleans))
        expected = np.array(floats)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_booleans_to_float_series(self):
        booleans = [True, False, True, False, True]
        floats = [float(b) for b in booleans]
        result = pd.Series(booleans).apply(pdtypes.apply._boolean_to_float)
        expected = pd.Series(floats)
        pd.testing.assert_series_equal(result, expected)


class ApplyBooleanToIntegerReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_boolean_to_numpy_float_scalar(self):
        booleans = [True, False, True, False, True]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](b)
                  for idx, b in enumerate(booleans)]
        for b, f in zip(booleans, floats):
            result = pdtypes.apply._boolean_to_float(b, return_type=type(f))
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))


if __name__ == "__main__":
    unittest.main()
