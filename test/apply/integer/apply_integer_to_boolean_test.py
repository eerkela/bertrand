import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyIntegerToBooleanMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_integer_to_boolean_scalar(self):
        na_val = None
        expected = pd.NA
        result = pdtypes.apply._integer_to_boolean(na_val)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_integer_to_boolean_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        vec = np.vectorize(pdtypes.apply._integer_to_boolean)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NA) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_integer_to_boolean_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        result = pd.Series(nones).apply(pdtypes.apply._integer_to_boolean)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToBooleanAccuracyTests(unittest.TestCase):

    #######################################################
    ####    Integer Boolean Flags [1, 0, 1, 0, ...]    ####
    #######################################################

    def test_integer_bool_flag_to_boolean_scalar(self):
        integers = [1, 0, 1, 0, 1]
        booleans = [bool(i) for i in integers]
        for i, b in zip(integers, booleans):
            result = pdtypes.apply._integer_to_boolean(i)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_integer_bool_flag_to_boolean_vector(self):
        integers = [1, 0, 1, 0, 1]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._integer_to_boolean)
        result = vec(np.array(integers))
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_bool_flag_to_boolean_series(self):
        integers = [1, 0, 1, 0, 1]
        booleans = [bool(i) for i in integers]
        result = pd.Series(integers).apply(pdtypes.apply._integer_to_boolean)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_boolean_out_of_range_error(self):
        integers = [-3, -2, -1, 2, 3, 4]
        err_msg = ("[pdtypes.apply._integer_to_boolean] could not convert int "
                   "to bool without losing information: ")
        for i in integers:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._integer_to_boolean(i)
            self.assertEqual(str(err.exception), err_msg + repr(i))

    def test_integer_to_boolean_out_of_range_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        booleans = [True, True, False, True, True]
        for i, b in zip(integers, booleans):
            result = pdtypes.apply._integer_to_boolean(i, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_integer_to_boolean_out_of_range_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        booleans = [True, True, False, True, True]
        vec = np.vectorize(pdtypes.apply._integer_to_boolean)
        result = vec(np.array(integers), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_to_boolean_out_of_range_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        booleans = [True, True, False, True, True]
        result = pd.Series(integers).apply(pdtypes.apply._integer_to_boolean,
                                           force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToBooleanReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_integer_to_custom_boolean_class_scalar(self):
        class CustomBoolean:
            def __init__(self, i: int):
                self.boolean = bool(i)

            def __bool__(self) -> bool:
                return self.boolean

            def __sub__(self, other) -> int:
                return self.boolean - other

        integers = [1, 1, 0, 0, 1]
        for i in integers:
            result = pdtypes.apply._float_to_boolean(i,
                                                     return_type=CustomBoolean)
            self.assertEqual(type(result), CustomBoolean)

    def test_numpy_signed_integer_to_standard_boolean_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([1, 1, 0, 0, 1])]
        booleans = [bool(i) for i in integers]
        for i, b in zip(integers, booleans):
            result = pdtypes.apply._integer_to_complex(i, return_type=type(b))
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_numpy_unsigned_integer_to_standard_boolean_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([1, 1, 0, 0, 1])]
        booleans = [bool(i) for i in integers]
        for i, b in zip(integers, booleans):
            result = pdtypes.apply._integer_to_complex(i, return_type=type(b))
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))


if __name__ == "__main__":
    unittest.main()
