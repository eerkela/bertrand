import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyFloatToBooleanMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_float_to_boolean_scalar(self):
        na_val = None
        expected = pd.NA
        result = pdtypes.apply._float_to_boolean(na_val)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_float_to_boolean_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        vec = np.vectorize(pdtypes.apply._float_to_boolean)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NA) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_float_to_boolean_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        result = pd.Series(nones).apply(pdtypes.apply._float_to_boolean)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToBooleanAccuracyTests(unittest.TestCase):

    #############################################################
    ####    Float Boolean Flags [1.0, 0.0, 1.0, 0.0, ...]    ####
    #############################################################

    def test_float_bool_flag_to_boolean_scalar(self):
        integers = [1, 0, 1, 0, 1]
        floats = [float(i) for i in integers]
        booleans = [bool(i) for i in integers]
        for f, b in zip(floats, booleans):
            result = pdtypes.apply._float_to_boolean(f)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_float_bool_flag_to_boolean_vector(self):
        integers = [1, 0, 1, 0, 1]
        floats = [float(i) for i in integers]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._float_to_boolean)
        result = vec(np.array(floats))
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_bool_flag_to_boolean_series(self):
        integers = [1, 0, 1, 0, 1]
        floats = [float(i) for i in integers]
        booleans = [bool(i) for i in integers]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_boolean)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    ##############################
    ####    Generic Floats    ####
    ##############################

    def test_float_to_boolean_within_ftol_scalar(self):
        integers = [1, 1, 0, 0, 1]
        floats = [i + 1e-8 if idx % 2 else i - 1e-8
                  for idx, i in enumerate(integers)]
        booleans = [True, True, False, False, True]
        for f, b in zip(floats, booleans):
            result = pdtypes.apply._float_to_boolean(f, ftol=1e-6)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_float_to_boolean_within_ftol_vector(self):
        integers = [1, 1, 0, 0, 1]
        floats = [i + 1e-8 if idx % 2 else i - 1e-8
                  for idx, i in enumerate(integers)]
        booleans = [True, True, False, False, True]
        vec = np.vectorize(pdtypes.apply._float_to_boolean)
        result = vec(np.array(floats), ftol=1e-6)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_to_boolean_within_ftol_series(self):
        integers = [1, 1, 0, 0, 1]
        floats = [i + 1e-8 if idx % 2 else i - 1e-8
                  for idx, i in enumerate(integers)]
        booleans = [True, True, False, False, True]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_boolean,
                                         ftol=1e-6)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_boolean_out_of_range_error(self):
        integers = [-3, -2, -1, 2, 3, 4]
        floats = [float(i) for i in integers]
        err_msg = ("[pdtypes.apply._float_to_boolean] could not convert float "
                   "to bool without losing information: ")
        for f in floats:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._float_to_boolean(f)
            self.assertEqual(str(err.exception), err_msg + repr(f))

    def test_float_to_boolean_out_of_range_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        booleans = [True, True, False, True, True]
        for f, b in zip(floats, booleans):
            result = pdtypes.apply._float_to_boolean(f, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_float_to_boolean_out_of_range_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        booleans = [True, True, False, True, True]
        vec = np.vectorize(pdtypes.apply._float_to_boolean)
        result = vec(np.array(floats), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_to_boolean_out_of_range_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        booleans = [True, True, False, True, True]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_boolean,
                                         force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_boolean_decimals_between_0_and_1_error(self):
        floats = [random.random() for _ in range(5)]
        err_msg = ("[pdtypes.apply._float_to_boolean] could not convert float "
                   "to bool without losing information: ")
        for f in floats:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._float_to_boolean(f)
            self.assertEqual(str(err.exception), err_msg + repr(f))

    def test_float_to_boolean_decimals_between_0_and_1_forced_scalar(self):
        floats = [random.random() for _ in range(5)]
        booleans = [True for _ in range(5)]
        for f, b in zip(floats, booleans):
            result = pdtypes.apply._float_to_boolean(f, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_float_to_boolean_decimals_between_0_and_1_forced_vector(self):
        floats = [random.random() for _ in range(5)]
        booleans = [True for _ in range(5)]
        vec = np.vectorize(pdtypes.apply._float_to_boolean)
        result = vec(np.array(floats), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_to_boolean_decimals_between_0_and_1_forced_series(self):
        floats = [random.random() for _ in range(5)]
        booleans = [True for _ in range(5)]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_boolean,
                                         force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToBooleanReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_float_to_custom_boolean_class_scalar(self):
        class CustomBoolean:
            def __init__(self, f: float):
                self.boolean = bool(f)

            def __bool__(self) -> bool:
                return self.boolean

            def __sub__(self, other) -> int:
                return self.boolean - other

        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        for f in floats:
            result = pdtypes.apply._float_to_boolean(f,
                                                     return_type=CustomBoolean)
            self.assertEqual(type(result), CustomBoolean)

    def test_numpy_float_type_to_boolean_scalar(self):
        integers = [1, 0, 1, 0, 1]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        for f, b in zip(floats, booleans):
            result = pdtypes.apply._float_to_boolean(f, return_type=type(b))
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))


if __name__ == "__main__":
    unittest.main()
