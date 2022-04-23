import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyFloatToComplexMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_float_to_complex_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._float_to_complex(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_float_to_complex_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._float_to_complex)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_float_to_complex_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._float_to_complex)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToComplexAccuracyTests(unittest.TestCase):

    ##############################
    ####    Generic Floats    ####
    ##############################

    def test_float_to_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        complex_numbers = [complex(f, 0) for f in floats]
        for f, c in zip(floats, complex_numbers):
            result = pdtypes.apply._float_to_complex(f)
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))

    def test_float_to_complex_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        complex_numbers = [complex(f, 0) for f in floats]
        vec = np.vectorize(pdtypes.apply._float_to_complex)
        result = vec(np.array(floats))
        expected = np.array(complex_numbers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_to_complex_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        complex_numbers = [complex(f, 0) for f in floats]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_complex)
        expected = pd.Series(complex_numbers)
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToComplexReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_float_to_numpy_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                     for idx, i in enumerate(integers)]
        for f, c in zip(floats, complexes):
            result = pdtypes.apply._float_to_complex(f, return_type=type(c))
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))

    def test_numpy_float_type_to_standard_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        complexes = [complex(f, 0) for f in floats]
        for f, c in zip(floats, complexes):
            result = pdtypes.apply._float_to_complex(f, return_type=type(c))
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))

    def test_numpy_float_type_to_numpy_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                     for idx, i in enumerate(integers)]
        for f, c in zip(floats, complexes):
            result = pdtypes.apply._float_to_complex(f, return_type=type(c))
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))


if __name__ == "__main__":
    unittest.main()
