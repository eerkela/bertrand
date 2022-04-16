from __future__ import annotations
from datetime import datetime, timedelta
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyFloatToIntegerTests(unittest.TestCase):

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
        floats = [i + random.random() for i in integers]
        for f in floats:
            result = pdtypes.apply._float_to_integer(f, force=True)
            expected = int(f)
            self.assertEqual(result, expected)
            self.assertEqual(type(result), type(expected))

    def test_decimal_float_to_integer_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        vec = np.vectorize(pdtypes.apply._float_to_integer)
        result = vec(np.array(floats), force=True)
        expected = np.array([int(f) for f in floats])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_decimal_float_to_integer_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_integer,
                                         force=True)
        expected = pd.Series([int(f) for f in floats])
        pd.testing.assert_series_equal(result, expected)

    def test_decimal_float_to_integer_rounded_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        for f in floats:
            result = pdtypes.apply._float_to_integer(f, round=True)
            expected = int(np.round(f))
            self.assertEqual(result, expected)
            self.assertEqual(type(result), type(expected))

    def test_decimal_float_to_integer_rounded_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        vec = np.vectorize(pdtypes.apply._float_to_integer)
        result = vec(np.array(floats), round=True)
        expected = np.array([int(np.round(f)) for f in floats])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_decimal_float_to_integer_rounded_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_integer,
                                         round=True)
        expected = pd.Series([int(np.round(f)) for f in floats])
        pd.testing.assert_series_equal(result, expected)

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_numpy_float_type_to_integer_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [ftype(i) for i, ftype in zip(integers, float_types)]
        for f in floats:
            result = pdtypes.apply._float_to_integer(f)
            self.assertEqual(type(result), int)

    def test_float_to_numpy_signed_integer_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        return_types = [np.int8, np.int16, np.int32, np.int64]
        for f, rtype in zip(floats, return_types):
            result = pdtypes.apply._float_to_integer(f, return_type=rtype)
            self.assertEqual(type(result), rtype)

    def test_float_to_numpy_unsigned_integer_scalar(self):
        integers = [0, 1, 2, 3, 4, 5]
        floats = [float(i) for i in integers]
        return_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        for f, rtype in zip(floats, return_types):
            result = pdtypes.apply._float_to_integer(f, return_type=rtype)
            self.assertEqual(type(result), rtype)


class ApplyFloatToComplexTests(unittest.TestCase):

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

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_numpy_float_type_to_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [ftype(i) for i, ftype in zip(integers, float_types)]
        for f in floats:
            result = pdtypes.apply._float_to_complex(f)
            self.assertEqual(type(result), complex)

    def test_float_to_numpy_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        return_types = [np.complex64, np.complex128]
        for f, rtype in zip(floats, return_types):
            result = pdtypes.apply._float_to_complex(f, return_type=rtype)
            self.assertEqual(type(result), rtype)


class ApplyFloatToStringTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_float_to_string_scalar(self):
        na_val = None
        expected = pd.NA
        result = pdtypes.apply._float_to_string(na_val)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_float_to_string_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        vec = np.vectorize(pdtypes.apply._float_to_string)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NA) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_float_to_string_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        result = pd.Series(nones).apply(pdtypes.apply._float_to_string)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ##############################
    ####    Generic Floats    ####
    ##############################

    def test_float_to_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        strings = [str(f) for f in floats]
        for s, f in zip(strings, floats):
            result = pdtypes.apply._float_to_string(f)
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_float_to_string_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        strings = [str(f) for f in floats]
        vec = np.vectorize(pdtypes.apply._float_to_string)
        result = vec(np.array(floats))
        expected = np.array(strings)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_to_string_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        strings = [str(f) for f in floats]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_string)
        expected = pd.Series(strings)
        pd.testing.assert_series_equal(result, expected)

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_numpy_float_type_to_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [ftype(i) for i, ftype in zip(integers, float_types)]
        for f in floats:
            result = pdtypes.apply._float_to_string(f)
            self.assertEqual(type(result), str)

    def test_float_to_custom_string_class_scalar(self):
        class CustomString:
            def __init__(self, s: str):
                self.string = s

            def __str__(self) -> str:
                return self.string

        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        for f in floats:
            result = pdtypes.apply._float_to_string(f, return_type=CustomString)
            self.assertEqual(type(result), CustomString)


class ApplyFloatToBooleanTests(unittest.TestCase):

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

    #####################################################
    ####    Float Boolean Flags [1, 0, 1, 0, ...]    ####
    #####################################################

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

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_numpy_float_type_to_boolean_scalar(self):
        integers = [1, 0, 1, 0, 1]
        float_types = [np.float16, np.float32, np.float64]
        floats = [ftype(i) for i, ftype in zip(integers, float_types)]
        for f in floats:
            result = pdtypes.apply._float_to_boolean(f)
            self.assertEqual(type(result), bool)

    def test_float_to_custom_boolean_class_scalar(self):
        class CustomBoolean:
            def __init__(self, b: bool):
                self.boolean = b

            def __bool__(self) -> bool:
                return self.boolean

            def __sub__(self, other) -> int:
                return self.boolean - other

        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        for f in floats:
            result = pdtypes.apply._float_to_boolean(f,
                                                     return_type=CustomBoolean)
            self.assertEqual(type(result), CustomBoolean)


class ApplyFloatToDatetimeTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_float_to_datetime_scalar(self):
        na_val = None
        expected = pd.NaT
        result = pdtypes.apply._float_to_datetime(na_val)
        # numpy can't parse pd.NaT, and pd.NaT != pd.NaT, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_float_to_datetime_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        vec = np.vectorize(pdtypes.apply._float_to_datetime)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NaT) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_float_to_datetime_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        result = pd.Series(nones).apply(pdtypes.apply._float_to_datetime)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ##############################
    ####    Generic Floats    ####
    ##############################

    def test_float_to_datetime_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "UTC") for f in floats]
        for f, d in zip(floats, datetimes):
            result = pdtypes.apply._float_to_datetime(f)
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_float_to_datetime_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "UTC") for f in floats]
        vec = np.vectorize(pdtypes.apply._float_to_datetime)
        result = vec(np.array(floats))
        expected = np.array(datetimes)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_to_datetime_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "UTC") for f in floats]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_datetime)
        expected = pd.Series(datetimes)
        pd.testing.assert_series_equal(result, expected)

    # #########################################
    # ####    Non-standard Return Types    ####
    # #########################################

    def test_numpy_float_type_to_datetime_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [ftype(i) for i, ftype in zip(integers, float_types)]
        for f in floats:
            result = pdtypes.apply._float_to_datetime(f)
            self.assertEqual(type(result), pd.Timestamp)

    def test_float_to_non_pandas_datetime_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        for f in floats:
            result = pdtypes.apply._float_to_datetime(f, return_type=datetime)
            self.assertEqual(type(result), datetime)

    def test_float_to_custom_datetime_class_scalar(self):
        class CustomDatetime:
            def __init__(self, d: pd.Timestamp):
                self.timestamp = d

            @classmethod
            def fromtimestamp(cls, stamp: float, tz) -> CustomDatetime:
                return cls(pd.Timestamp.fromtimestamp(stamp, tz))

            def to_datetime(self) -> pd.Timestamp:
                return self.timestamp

        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        for f in floats:
            result = pdtypes.apply._float_to_datetime(f,
                                                      return_type=CustomDatetime)
            self.assertEqual(type(result), CustomDatetime)


class ApplyFloatToTimedeltaTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_float_to_timedelta_scalar(self):
        na_val = None
        expected = pd.NaT
        result = pdtypes.apply._float_to_timedelta(na_val)
        # numpy can't parse pd.NaT, and pd.NaT != pd.NaT, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_float_to_timedelta_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        vec = np.vectorize(pdtypes.apply._float_to_timedelta)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NaT) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_float_to_timedelta_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        result = pd.Series(nones).apply(pdtypes.apply._float_to_timedelta)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ##############################
    ####    Generic Floats    ####
    ##############################

    def test_float_to_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        for f, t in zip(floats, timedeltas):
            result = pdtypes.apply._float_to_timedelta(f)
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_float_to_timedelta_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        vec = np.vectorize(pdtypes.apply._float_to_timedelta)
        result = vec(np.array(floats))
        expected = np.array(timedeltas)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_to_timedelta_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_timedelta)
        expected = pd.Series(timedeltas)
        pd.testing.assert_series_equal(result, expected)

    # #########################################
    # ####    Non-standard Return Types    ####
    # #########################################

    def test_numpy_float_type_to_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [ftype(i) for i, ftype in zip(integers, float_types)]
        for f in floats:
            result = pdtypes.apply._float_to_timedelta(f)
            self.assertEqual(type(result), pd.Timedelta)

    def test_float_to_non_pandas_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        for f in floats:
            result = pdtypes.apply._float_to_timedelta(f, return_type=timedelta)
            self.assertEqual(type(result), timedelta)

    def test_float_to_custom_timedelta_class_scalar(self):
        class CustomTimedelta:
            def __init__(self, seconds: float):
                self.delta = pd.Timedelta(seconds=seconds)

            def to_timedelta(self) -> pd.Timedelta:
                return self.delta

        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        for f in floats:
            result = pdtypes.apply._float_to_timedelta(f, return_type=CustomTimedelta)
            self.assertEqual(type(result), CustomTimedelta)


if __name__ == "__main__":
    unittest.main()