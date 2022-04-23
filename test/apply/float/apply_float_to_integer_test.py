import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyFloatToIntegerMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_float_to_integer_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._float_to_integer(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_float_to_integer_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._float_to_integer)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_float_to_integer_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._float_to_integer)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToIntegerAccuracyTests(unittest.TestCase):

    ############################
    ####    Whole Floats    ####
    ############################

    def test_whole_float_to_integer_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        for i, f in zip(integers, floats):
            result = pdtypes.apply._float_to_integer(f)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_whole_float_to_integer_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._float_to_integer)
        result = vec(np.array(floats))
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_whole_float_to_integer_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_integer)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    ##############################
    ####    Decimal Floats    ####
    ##############################

    def test_decimal_float_to_integer_within_ftol_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + 1e-8 if idx % 2 else i - 1e-8
                  for idx, i in enumerate(integers)]
        for expected, f in zip(integers, floats):
            result = pdtypes.apply._float_to_integer(f, ftol=1e-6)
            self.assertEqual(result, expected)
            self.assertEqual(type(result), type(expected))

    def test_decimal_float_to_integer_within_ftol_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + 1e-8 if idx % 2 else i - 1e-8
                  for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._float_to_integer)
        result = vec(np.array(floats), ftol=1e-6)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_decimal_float_to_integer_within_ftol_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + 1e-8 if idx % 2 else i - 1e-8
                  for idx, i in enumerate(integers)]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_integer,
                                         ftol=1e-6)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_decimal_float_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        err_msg = ("[pdtypes.apply._float_to_integer] could not convert float "
                   "to int without losing information: ")
        for f in floats:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._float_to_integer(f)
            self.assertEqual(str(err.exception), err_msg + repr(f))

    def test_decimal_float_to_integer_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        for f, i in zip(floats, integers):
            result = pdtypes.apply._float_to_integer(f, force=True)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_decimal_float_to_integer_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._float_to_integer)
        result = vec(np.array(floats), force=True)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_decimal_float_to_integer_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_integer,
                                         force=True)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_decimal_float_to_integer_forced_not_rounded_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        for f in floats:
            result = pdtypes.apply._float_to_integer(f, force=True, round=False)
            expected = int(f)
            self.assertEqual(result, expected)
            self.assertEqual(type(result), type(expected))

    def test_decimal_float_to_integer_forced_not_rounded_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._float_to_integer)
        result = vec(np.array(floats), force=True, round=False)
        expected = np.array([int(f) for f in floats])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_decimal_float_to_integer_forced_not_rounded_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_integer,
                                         force=True, round=False)
        expected = pd.Series([int(f) for f in floats])
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToIntegerReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_float_to_numpy_signed_integer_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        floats = [float(i) for i in integers]
        for f, i in zip(floats, integers):
            result = pdtypes.apply._float_to_integer(f, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_standard_float_to_numpy_unsigned_integer_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        floats = [float(i) for i in integers]
        for f, i in zip(floats, integers):
            result = pdtypes.apply._float_to_integer(f, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_numpy_float_to_standard_integer_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        for f, i in zip(floats, integers):
            result = pdtypes.apply._float_to_integer(f, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_numpy_float_to_numpy_signed_integer_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        for f, i in zip(floats, integers):
            result = pdtypes.apply._float_to_integer(f, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_numpy_float_to_numpy_unsigned_integer_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        for f, i in zip(floats, integers):
            result = pdtypes.apply._float_to_integer(f, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))


if __name__ == "__main__":
    unittest.main()
