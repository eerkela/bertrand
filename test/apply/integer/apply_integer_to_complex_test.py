import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


class ApplyIntegerToComplexMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_integer_to_complex_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._integer_to_complex(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_integer_to_complex_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._integer_to_complex)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_integer_to_complex_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._integer_to_complex)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToComplexAccuracyTests(unittest.TestCase):

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i, 0) for i in integers]
        for i, c in zip(integers, complexes):
            result = pdtypes.apply._integer_to_complex(i)
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))

    def test_integer_to_complex_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i, 0) for i in integers]
        vec = np.vectorize(pdtypes.apply._integer_to_complex)
        result = vec(np.array(integers))
        expected = np.array(complexes)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_to_complex_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i, 0) for i in integers]
        result = pd.Series(integers).apply(pdtypes.apply._integer_to_complex)
        expected = pd.Series(complexes)
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToComplexReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_integer_to_numpy_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                     for idx, i in enumerate(integers)]
        for i, c in zip(integers, complexes):
            result = pdtypes.apply._integer_to_complex(i, return_type=type(c))
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))

    def test_numpy_signed_integer_to_standard_complex_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        complexes = [complex(i, 0) for i in integers]
        for i, c in zip(integers, complexes):
            result = pdtypes.apply._integer_to_complex(i, return_type=type(c))
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))

    def test_numpy_signed_integer_to_numpy_complex_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                     for idx, i in enumerate(integers)]
        for i, c in zip(integers, complexes):
            result = pdtypes.apply._integer_to_complex(i, return_type=type(c))
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))

    def test_numpy_unsigned_integer_to_standard_complex_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        complexes = [complex(i, 0) for i in integers]
        for i, c in zip(integers, complexes):
            result = pdtypes.apply._integer_to_complex(i, return_type=type(c))
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))

    def test_numpy_unsigned_integer_to_numpy_complex_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                     for idx, i in enumerate(integers)]
        for i, c in zip(integers, complexes):
            result = pdtypes.apply._integer_to_complex(i, return_type=type(c))
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))


if __name__ == "__main__":
    unittest.main()
