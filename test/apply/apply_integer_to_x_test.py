from __future__ import annotations
from datetime import datetime, timedelta, timezone
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyIntegerToFloatTests(unittest.TestCase):

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


class ApplyIntegerToComplexTests(unittest.TestCase):

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


class ApplyIntegerToStringTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_integer_to_string_scalar(self):
        na_val = None
        expected = pd.NA
        result = pdtypes.apply._integer_to_string(na_val)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_integer_to_string_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        vec = np.vectorize(pdtypes.apply._integer_to_string)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NA) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_integer_to_string_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        result = pd.Series(nones).apply(pdtypes.apply._integer_to_string)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i) for i in integers]
        for i, s in zip(integers, strings):
            result = pdtypes.apply._integer_to_string(i)
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_integer_to_string_vector(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._integer_to_string)
        result = vec(np.array(integers))
        expected = np.array(strings)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_to_string_series(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i) for i in integers]
        result = pd.Series(integers).apply(pdtypes.apply._integer_to_string)
        expected = pd.Series(strings)
        pd.testing.assert_series_equal(result, expected)

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_numpy_signed_integer_to_string_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        strings = [str(i) for i in integers]
        for i, s in zip(integers, strings):
            result = pdtypes.apply._integer_to_complex(i, return_type=type(s))
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_numpy_unsigned_integer_to_string_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        strings = [str(i) for i in integers]
        for i, s in zip(integers, strings):
            result = pdtypes.apply._integer_to_complex(i, return_type=type(s))
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_standard_integer_to_custom_string_class_scalar(self):
        class CustomString:
            def __init__(self, i: int):
                self.string = str(i)

            def __str__(self) -> str:
                return self.string

        integers = [-2, -1, 0, 1, 2]
        for i in integers:
            result = pdtypes.apply._integer_to_string(i, return_type=CustomString)
            self.assertEqual(type(result), CustomString)


class ApplyIntegerToBooleanTests(unittest.TestCase):

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


class ApplyIntegerToDatetimeTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_integer_to_datetime_scalar(self):
        na_val = None
        expected = pd.NaT
        result = pdtypes.apply._integer_to_datetime(na_val)
        # numpy can't parse pd.NaT, and pd.NaT != pd.NaT, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_integer_to_datetime_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        vec = np.vectorize(pdtypes.apply._integer_to_datetime)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NaT) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_integer_to_datetime_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        result = pd.Series(nones).apply(pdtypes.apply._integer_to_datetime)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_datetime_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        for i, d in zip(integers, datetimes):
            result = pdtypes.apply._integer_to_datetime(i)
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_integer_to_datetime_vector(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        vec = np.vectorize(pdtypes.apply._integer_to_datetime)
        result = vec(np.array(integers))
        expected = np.array(datetimes)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_to_datetime_series(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        result = pd.Series(integers).apply(pdtypes.apply._integer_to_datetime)
        expected = pd.Series(datetimes)
        pd.testing.assert_series_equal(result, expected)

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_integer_to_standard_datetime_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        for i, d in zip(integers, datetimes):
            result = pdtypes.apply._integer_to_datetime(i, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_standard_integer_to_custom_datetime_class_scalar(self):
        class CustomDatetime:
            def __init__(self, i: int, tz):
                self.timestamp = pd.Timestamp.fromtimestamp(i, tz)

            @classmethod
            def fromtimestamp(cls, i: int, tz) -> CustomDatetime:
                return cls(i, tz)

            def to_datetime(self) -> pd.Timestamp:
                return self.timestamp

        integers = [-2, -1, 0, 1, 2]
        for i in integers:
            result = pdtypes.apply._integer_to_datetime(i, return_type=CustomDatetime)
            self.assertEqual(type(result), CustomDatetime)

    def test_numpy_signed_integer_to_pandas_timestamp_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        datetimes = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        for i, d in zip(integers, datetimes):
            result = pdtypes.apply._integer_to_datetime(i, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_numpy_signed_integer_to_standard_datetime_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        datetimes = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        for i, d in zip(integers, datetimes):
            result = pdtypes.apply._integer_to_datetime(i, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_numpy_unsigned_integer_to_pandas_timestamp_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        datetimes = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        for i, d in zip(integers, datetimes):
            result = pdtypes.apply._integer_to_datetime(i, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_numpy_unsigned_integer_to_standard_datetime_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        datetimes = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        for i, d in zip(integers, datetimes):
            result = pdtypes.apply._integer_to_datetime(i, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))


class ApplyIntegerToTimedeltaTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_integer_to_timedelta_scalar(self):
        na_val = None
        expected = pd.NaT
        result = pdtypes.apply._integer_to_timedelta(na_val)
        # numpy can't parse pd.NaT, and pd.NaT != pd.NaT, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_integer_to_timedelta_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        vec = np.vectorize(pdtypes.apply._integer_to_timedelta)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NaT) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_integer_to_timedelta_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        result = pd.Series(nones).apply(pdtypes.apply._integer_to_timedelta)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        timedeltas = [pd.Timedelta(seconds=i) for i in integers]
        for i, t in zip(integers, timedeltas):
            result = pdtypes.apply._integer_to_timedelta(i)
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_integer_to_timedelta_vector(self):
        integers = [-2, -1, 0, 1, 2]
        timedeltas = [pd.Timedelta(seconds=i) for i in integers]
        vec = np.vectorize(pdtypes.apply._integer_to_timedelta)
        result = vec(np.array(integers))
        expected = np.array(timedeltas)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_to_timedelta_series(self):
        integers = [-2, -1, 0, 1, 2]
        timedeltas = [pd.Timedelta(seconds=i) for i in integers]
        result = pd.Series(integers).apply(pdtypes.apply._integer_to_timedelta)
        expected = pd.Series(timedeltas)
        pd.testing.assert_series_equal(result, expected)

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_integer_to_standard_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        timedeltas = [timedelta(seconds=i) for i in integers]
        for i, t in zip(integers, timedeltas):
            result = pdtypes.apply._integer_to_timedelta(i, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_standard_integer_to_custom_timedelta_class_scalar(self):
        class CustomTimedelta:
            def __init__(self, seconds: int):
                self.delta = pd.Timedelta(seconds=seconds)

            def to_timedelta(self) -> pd.Timedelta:
                return self.delta

        integers = [-2, -1, 0, 1, 2]
        for i in integers:
            result = pdtypes.apply._integer_to_timedelta(i, return_type=CustomTimedelta)
            self.assertEqual(type(result), CustomTimedelta)

    def test_numpy_signed_integer_to_pandas_timedelta_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        timedeltas = [pd.Timedelta(seconds=i) for i in integers]
        for i, t in zip(integers, timedeltas):
            result = pdtypes.apply._integer_to_timedelta(i, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_numpy_signed_integer_to_standard_timedelta_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        timedeltas = [timedelta(seconds=int(i)) for i in integers]
        for i, t in zip(integers, timedeltas):
            result = pdtypes.apply._integer_to_timedelta(i, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_numpy_unsigned_integer_to_pandas_timedelta_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        timedeltas = [pd.Timedelta(seconds=i) for i in integers]
        for i, t in zip(integers, timedeltas):
            result = pdtypes.apply._integer_to_timedelta(i, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_numpy_unsigned_integer_to_standard_timedelta_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        timedeltas = [timedelta(seconds=int(i)) for i in integers]
        for i, t in zip(integers, timedeltas):
            result = pdtypes.apply._integer_to_timedelta(i, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))


if __name__ == "__main__":
    unittest.main()
