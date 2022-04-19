from __future__ import annotations
from datetime import datetime, timedelta, timezone
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyBooleanToIntegerTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_boolean_to_integer_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._boolean_to_integer(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_boolean_to_integer_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._boolean_to_integer)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_boolean_to_integer_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._boolean_to_integer)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ################################
    ####    Generic Booleans    ####
    ################################

    def test_boolean_to_integer_scalar(self):
        booleans = [True, False, True, False, True]
        integers = [int(b) for b in booleans]
        for b, i in zip(booleans, integers):
            result = pdtypes.apply._boolean_to_integer(b)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_boolean_to_integer_vector(self):
        booleans = [True, False, True, False, True]
        integers = [int(b) for b in booleans]
        vec = np.vectorize(pdtypes.apply._boolean_to_integer)
        result = vec(np.array(booleans))
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_boolean_to_integer_series(self):
        booleans = [True, False, True, False, True]
        integers = [int(b) for b in booleans]
        result = pd.Series(booleans).apply(pdtypes.apply._boolean_to_integer)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)
      
    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_boolean_to_numpy_signed_integer_scalar(self):
        booleans = [True, False, True, False, True]
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate(booleans)]
        for b, i in zip(booleans, integers):
            result = pdtypes.apply._boolean_to_integer(b, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_standard_boolean_to_numpy_unsigned_integer_scalar(self):
        booleans = [True, False, True, False, True]
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate(booleans)]
        for b, i in zip(booleans, integers):
            result = pdtypes.apply._boolean_to_integer(b, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))


class ApplyBooleanToFloatTests(unittest.TestCase):

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


class ApplyBooleanToComplexTests(unittest.TestCase):

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


class ApplyBooleanToStringTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_boolean_to_string_scalar(self):
        na_val = None
        expected = pd.NA
        result = pdtypes.apply._boolean_to_string(na_val)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_boolean_to_string_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        vec = np.vectorize(pdtypes.apply._boolean_to_string)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NA) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_boolean_to_string_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        result = pd.Series(nones).apply(pdtypes.apply._boolean_to_string)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ################################
    ####    Generic Booleans    ####
    ################################

    def test_boolean_to_string_scalar(self):
        booleans = [True, False, True, False, True]
        strings = [str(b) for b in booleans]
        for b, s in zip(booleans, strings):
            result = pdtypes.apply._boolean_to_string(b)
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_boolean_to_string_vector(self):
        booleans = [True, False, True, False, True]
        strings = [str(b) for b in booleans]
        vec = np.vectorize(pdtypes.apply._boolean_to_string)
        result = vec(np.array(booleans))
        expected = np.array(strings)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_boolean_to_string_series(self):
        booleans = [True, False, True, False, True]
        strings = [str(b) for b in booleans]
        result = pd.Series(booleans).apply(pdtypes.apply._boolean_to_string)
        expected = pd.Series(strings)
        pd.testing.assert_series_equal(result, expected)

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_boolean_to_custom_string_class_scalar(self):
        class CustomString:
            def __init__(self, b: bool):
                self.string = str(b)

            def __str__(self) -> str:
                return self.string

        booleans = [True, False, True, False, True]
        for b in booleans:
            result = pdtypes.apply._boolean_to_string(b, return_type=CustomString)
            self.assertEqual(type(result), CustomString)


class ApplyBooleanToDatetimeTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_boolean_to_datetime_scalar(self):
        na_val = None
        expected = pd.NaT
        result = pdtypes.apply._boolean_to_datetime(na_val)
        # numpy can't parse pd.NaT, and pd.NaT != pd.NaT, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_boolean_to_datetime_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        vec = np.vectorize(pdtypes.apply._boolean_to_datetime)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NaT) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_boolean_to_datetime_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        result = pd.Series(nones).apply(pdtypes.apply._boolean_to_datetime)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ################################
    ####    Generic Booleans    ####
    ################################

    def test_boolean_to_datetime_scalar(self):
        booleans = [True, False, True, False, True]
        datetimes = [pd.Timestamp.fromtimestamp(b, "UTC") for b in booleans]
        for b, d in zip(booleans, datetimes):
            result = pdtypes.apply._boolean_to_datetime(b)
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_boolean_to_datetime_vector(self):
        booleans = [True, False, True, False, True]
        datetimes = [pd.Timestamp.fromtimestamp(b, "UTC") for b in booleans]
        vec = np.vectorize(pdtypes.apply._boolean_to_datetime)
        result = vec(np.array(booleans))
        expected = np.array(datetimes)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_boolean_to_datetime_series(self):
        booleans = [True, False, True, False, True]
        datetimes = [pd.Timestamp.fromtimestamp(b, "UTC") for b in booleans]
        result = pd.Series(booleans).apply(pdtypes.apply._boolean_to_datetime)
        expected = pd.Series(datetimes)
        pd.testing.assert_series_equal(result, expected)

    ########################################
    ####   Non-standard Return Types    ####
    ########################################

    def test_standard_boolean_to_standard_datetime_scalar(self):
        booleans = [True, False, True, False, True]
        datetimes = [datetime.fromtimestamp(b, timezone.utc) for b in booleans]
        for b, d in zip(booleans, datetimes):
            result = pdtypes.apply._boolean_to_datetime(b, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_standard_boolean_to_custom_datetime_class_scalar(self):
        class CustomDatetime:
            def __init__(self, b: bool, tz):
                self.timestamp = pd.Timestamp.fromtimestamp(b, tz)

            @classmethod
            def fromtimestamp(cls, b: bool, tz) -> CustomDatetime:
                return cls(b, tz)

            def to_datetime(self) -> pd.Timestamp:
                return self.timestamp

        booleans = [True, False, True, False, True]
        for b in booleans:
            result = pdtypes.apply._boolean_to_datetime(b, return_type=CustomDatetime)
            self.assertEqual(type(result), CustomDatetime)


class ApplyBooleanToTimedeltaTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_boolean_to_timedelta_scalar(self):
        na_val = None
        expected = pd.NaT
        result = pdtypes.apply._boolean_to_timedelta(na_val)
        # numpy can't parse pd.NaT, and pd.NaT != pd.NaT, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_boolean_to_timedelta_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        vec = np.vectorize(pdtypes.apply._boolean_to_timedelta)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NaT) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_boolean_to_timedelta_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        result = pd.Series(nones).apply(pdtypes.apply._boolean_to_timedelta)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ################################
    ####    Generic Booleans    ####
    ################################

    def test_boolean_to_timedelta_scalar(self):
        booleans = [True, False, True, False, True]
        timedeltas = [pd.Timedelta(seconds=float(b)) for b in booleans]
        for b, t in zip(booleans, timedeltas):
            result = pdtypes.apply._boolean_to_timedelta(b)
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_boolean_to_timedelta_vector(self):
        booleans = [True, False, True, False, True]
        timedeltas = [pd.Timedelta(seconds=float(b)) for b in booleans]
        vec = np.vectorize(pdtypes.apply._boolean_to_timedelta)
        result = vec(np.array(booleans))
        expected = np.array(timedeltas)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_boolean_to_timedelta_series(self):
        booleans = [True, False, True, False, True]
        timedeltas = [pd.Timedelta(seconds=float(b)) for b in booleans]
        result = pd.Series(booleans).apply(pdtypes.apply._boolean_to_timedelta)
        expected = pd.Series(timedeltas)
        pd.testing.assert_series_equal(result, expected)

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_boolean_to_standard_timedelta_scalar(self):
        booleans = [True, False, True, False, True]
        timedeltas = [timedelta(seconds=b) for b in booleans]
        for b, t in zip(booleans, timedeltas):
            result = pdtypes.apply._boolean_to_timedelta(b, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_standard_boolean_to_custom_timedelta_scalar(self):
        class CustomTimedelta:
            def __init__(self, seconds: bool):
                self.delta = pd.Timedelta(seconds=seconds)

            def to_timedelta(self) -> pd.Timedelta:
                return self.delta

        booleans = [True, False, True, False, True]
        for b in booleans:
            result = pdtypes.apply._float_to_timedelta(b, return_type=CustomTimedelta)
            self.assertEqual(type(result), CustomTimedelta)



if __name__ == "__main__":
    unittest.main()
