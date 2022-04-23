from datetime import datetime, timezone
import random
import unittest

import numpy as np
import pandas as pd
import pytz

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyDatetimeToStringMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_datetime_to_string_scalar(self):
        na_val = None
        expected = pd.NA
        result = pdtypes.apply._datetime_to_string(na_val)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_datetime_to_string_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        vec = np.vectorize(pdtypes.apply._datetime_to_string)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NA) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_datetime_to_string_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        result = pd.Series(nones).apply(pdtypes.apply._datetime_to_string)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyDatetimeToStringAccuracyTests(unittest.TestCase):

    ###############################
    ####    Naive Datetimes    ####
    ###############################

    def test_naive_datetime_to_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        strings = [pdtypes.parse.to_utc(d).isoformat() for d in datetimes]
        for d, s in zip(datetimes, strings):
            result = pdtypes.apply._datetime_to_string(d)
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_naive_datetime_to_string_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        strings = [pdtypes.parse.to_utc(d).isoformat() for d in datetimes]
        vec = np.vectorize(pdtypes.apply._datetime_to_string)
        result = vec(np.array(datetimes))
        expected = np.array(strings)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_naive_datetime_to_string_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        strings = [pdtypes.parse.to_utc(d).isoformat() for d in datetimes]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_string)
        expected = pd.Series(strings)
        pd.testing.assert_series_equal(result, expected)

    ###############################
    ####    Aware Datetimes    ####
    ###############################

    def test_aware_datetime_to_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        strings = [pdtypes.parse.to_utc(d).isoformat() for d in datetimes]
        for d, s in zip(datetimes, strings):
            result = pdtypes.apply._datetime_to_string(d)
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_aware_datetime_to_string_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        strings = [pdtypes.parse.to_utc(d).isoformat() for d in datetimes]
        vec = np.vectorize(pdtypes.apply._datetime_to_string)
        result = vec(np.array(datetimes))
        expected = np.array(strings)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_aware_datetime_to_string_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        strings = [pdtypes.parse.to_utc(d).isoformat() for d in datetimes]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_string)
        expected = pd.Series(strings)
        pd.testing.assert_series_equal(result, expected)

    ###########################################
    ####    Mixed Aware/Naive Datetimes    ####
    ###########################################

    def test_mixed_aware_naive_datetime_to_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        strings = [pdtypes.parse.to_utc(d).isoformat() for d in datetimes]
        for d, s in zip(datetimes, strings):
            result = pdtypes.apply._datetime_to_string(d)
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_mixed_aware_naive_datetime_to_string_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        strings = [pdtypes.parse.to_utc(d).isoformat() for d in datetimes]
        vec = np.vectorize(pdtypes.apply._datetime_to_string)
        result = vec(np.array(datetimes))
        expected = np.array(strings)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_aware_naive_datetime_to_string_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        strings = [pdtypes.parse.to_utc(d).isoformat() for d in datetimes]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_string)
        expected = pd.Series(strings)
        pd.testing.assert_series_equal(result, expected)

    ########################################
    ####    Mixed Timezone Datetimes    ####
    ########################################

    def test_mixed_timezone_datetime_to_string_scalar(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        strings = [pdtypes.parse.to_utc(d).isoformat() for d in datetimes]
        for d, s in zip(datetimes, strings):
            result = pdtypes.apply._datetime_to_string(d)
            self.assertAlmostEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_mixed_timezone_datetime_to_string_vector(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        strings = [pdtypes.parse.to_utc(d).isoformat() for d in datetimes]
        vec = np.vectorize(pdtypes.apply._datetime_to_string)
        result = vec(np.array(datetimes))
        expected = np.array(strings)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_timezone_datetime_to_string_series(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        strings = [pdtypes.parse.to_utc(d).isoformat() for d in datetimes]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_string)
        expected = pd.Series(strings)
        pd.testing.assert_series_equal(result, expected)


class ApplyDatetimeToStringFormatStringTests(unittest.TestCase):

    #####################################
    ####    Custom Format Strings    ####
    #####################################

    def test_pandas_timestamp_to_custom_format_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        format_string = "%A %d %B %Y, %I:%M:%S.%f %p"
        strings = [pdtypes.parse.to_utc(d).strftime(format_string)
                   for d in datetimes]
        for d, s in zip(datetimes, strings):
            result = pdtypes.apply._datetime_to_string(d, format=format_string)
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_pandas_timestamp_to_custom_format_string_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        format_string = "%A %d %B %Y, %I:%M:%S.%f %p"
        strings = [pdtypes.parse.to_utc(d).strftime(format_string)
                   for d in datetimes]
        vec = np.vectorize(pdtypes.apply._datetime_to_string)
        result = vec(np.array(datetimes), format=format_string)
        expected = np.array(strings)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_pandas_timestamp_to_custom_format_string_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        format_string = "%A %d %B %Y, %I:%M:%S.%f %p"
        strings = [pdtypes.parse.to_utc(d).strftime(format_string)
                   for d in datetimes]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_string,
                                            format=format_string)
        expected = pd.Series(strings)
        pd.testing.assert_series_equal(result, expected)

    def test_standard_datetime_to_custom_format_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [datetime.fromtimestamp(f) for f in floats]
        format_string = "%A %d %B %Y, %I:%M:%S.%f %p"
        strings = [pdtypes.parse.to_utc(d).strftime(format_string)
                   for d in datetimes]
        for d, s in zip(datetimes, strings):
            result = pdtypes.apply._datetime_to_string(d, format=format_string)
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_pandas_timestamp_to_custom_format_string_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [datetime.fromtimestamp(f) for f in floats]
        format_string = "%A %d %B %Y, %I:%M:%S.%f %p"
        strings = [pdtypes.parse.to_utc(d).strftime(format_string)
                   for d in datetimes]
        vec = np.vectorize(pdtypes.apply._datetime_to_string)
        result = vec(np.array(datetimes), format=format_string)
        expected = np.array(strings)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_pandas_timestamp_to_custom_format_string_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [datetime.fromtimestamp(f) for f in floats]
        format_string = "%A %d %B %Y, %I:%M:%S.%f %p"
        strings = [pdtypes.parse.to_utc(d).strftime(format_string)
                   for d in datetimes]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_string,
                                            format=format_string)
        expected = pd.Series(strings)
        pd.testing.assert_series_equal(result, expected)


class ApplyDatetimeToStringReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_pandas_timestamp_to_custom_string_class_scalar(self):
        class CustomString:
            def __init__(self, s: str):
                self.string = s

            def __str__(self) -> str:
                return self.string

        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i) for i in integers]
        for d in datetimes:
            result = pdtypes.apply._datetime_to_string(d, return_type=CustomString)
            self.assertEqual(type(result), CustomString)

    def test_standard_datetime_to_custom_string_class_scalar(self):
        class CustomString:
            def __init__(self, s: str):
                self.string = s

            def __str__(self) -> str:
                return self.string

        integers = [-2, -1, 0, 1, 2]
        datetimes = [datetime.fromtimestamp(i) for i in integers]
        for d in datetimes:
            result = pdtypes.apply._datetime_to_string(d, return_type=CustomString)
            self.assertEqual(type(result), CustomString)


if __name__ == "__main__":
    unittest.main()
