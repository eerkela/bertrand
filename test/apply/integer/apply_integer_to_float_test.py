import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyIntegerToFloatMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_integer_to_float_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._integer_to_float(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_integer_to_float_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._integer_to_float)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_integer_to_float_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._integer_to_float)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToFloatAccuracyTests(unittest.TestCase):

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        for i, f in zip(integers, floats):
            result = pdtypes.apply._integer_to_float(i)
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))

    def test_integer_to_float_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._integer_to_float)
        result = vec(np.array(integers))
        expected = np.array(floats)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_to_float_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        result = pd.Series(integers).apply(pdtypes.apply._integer_to_float)
        expected = pd.Series(floats)
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToFloatReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_integer_to_numpy_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        for i, f in zip(integers, floats):
            result = pdtypes.apply._integer_to_float(i, return_type=type(f))
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))

    def test_numpy_signed_integer_to_standard_float_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        floats = [float(i) for i in integers]
        for i, f in zip(integers, floats):
            result = pdtypes.apply._integer_to_float(i, return_type=type(f))
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))

    def test_numpy_signed_integer_to_numpy_float_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        for i, f in zip(integers, floats):
            result = pdtypes.apply._integer_to_float(i, return_type=type(f))
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))

    def test_numpy_unsigned_integer_to_standard_float_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        floats = [float(i) for i in integers]
        for i, f in zip(integers, floats):
            result = pdtypes.apply._integer_to_float(i, return_type=type(f))
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))

    def test_numpy_unsigned_integer_to_numpy_float_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        for i, f in zip(integers, floats):
            result = pdtypes.apply._integer_to_float(i, return_type=type(f))
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))


if __name__ == "__main__":
    unittest.main()
