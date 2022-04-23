import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyBooleanToComplexMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_boolean_to_complex_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._boolean_to_complex(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_boolean_to_complex_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._boolean_to_complex)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_boolean_to_complex_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._boolean_to_complex)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyBooleanToComplexAccuracyTests(unittest.TestCase):

    ################################
    ####    Generic Booleans    ####
    ################################

    def test_boolean_to_complex_scalar(self):
        booleans = [True, False, True, False, True]
        complexes = [complex(b, 0) for b in booleans]
        for b, c in zip(booleans, complexes):
            result = pdtypes.apply._boolean_to_complex(b)
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))

    def test_boolean_to_complex_vector(self):
        booleans = [True, False, True, False, True]
        complexes = [complex(b, 0) for b in booleans]
        vec = np.vectorize(pdtypes.apply._boolean_to_complex)
        result = vec(np.array(booleans))
        expected = np.array(complexes)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_boolean_to_complex_series(self):
        booleans = [True, False, True, False, True]
        complexes = [complex(b, 0) for b in booleans]
        result = pd.Series(booleans).apply(pdtypes.apply._boolean_to_complex)
        expected = pd.Series(complexes)
        pd.testing.assert_series_equal(result, expected)


class ApplyBooleanToIntegerReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_boolean_to_numpy_complex_scalar(self):
        booleans = [True, False, True, False, True]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](b)
                     for idx, b in enumerate(booleans)]
        for b, c in zip(booleans, complexes):
            result = pdtypes.apply._boolean_to_complex(b, return_type=type(c))
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))


if __name__ == "__main__":
    unittest.main()
