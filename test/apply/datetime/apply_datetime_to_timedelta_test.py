from datetime import datetime, timedelta, timezone
import random
import unittest

import numpy as np
import pandas as pd
import pytz

from context import pdtypes
import pdtypes.apply
import pdtypes.parse


random.seed(12345)


class ApplyDatetimeToTimedeltaMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_datetime_to_timedelta_scalar(self):
        na_val = None
        expected = pd.NaT
        result = pdtypes.apply._datetime_to_timedelta(na_val)
        # numpy can't parse pd.NaT, and pd.NaT != pd.NaT, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_datetime_to_timedelta_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        vec = np.vectorize(pdtypes.apply._datetime_to_timedelta)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NaT) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_datetime_to_timedelta_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        result = pd.Series(nones).apply(pdtypes.apply._datetime_to_timedelta)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyDatetimeToTimedeltaAccuracyTests(unittest.TestCase):

    ###############################
    ####    Naive Datetimes    ####
    ###############################

    def test_naive_datetime_to_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        for d, t in zip(datetimes, timedeltas):
            result = pdtypes.apply._datetime_to_timedelta(d)
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_naive_datetime_to_timedelta_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        vec = np.vectorize(pdtypes.apply._datetime_to_float)
        result = vec(np.array(datetimes))
        expected = np.array(timedeltas)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_naive_datetime_to_timedelta_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_timedelta)
        expected = pd.Series(timedeltas)
        pd.testing.assert_series_equal(result, expected)

    ###############################
    ####    Aware Datetimes    ####
    ###############################

    def test_aware_datetime_to_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        for d, t in zip(datetimes, timedeltas):
            result = pdtypes.apply._datetime_to_timedelta(d)
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_aware_datetime_to_timedelta_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        vec = np.vectorize(pdtypes.apply._datetime_to_timedelta)
        result = vec(np.array(datetimes))
        expected = np.array(timedeltas)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_aware_datetime_to_timedelta_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_timedelta)
        expected = pd.Series(timedeltas)
        pd.testing.assert_series_equal(result, expected)

    ###########################################
    ####    Mixed Aware/Naive Datetimes    ####
    ###########################################

    def test_mixed_aware_naive_datetime_to_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        for d, t in zip(datetimes, timedeltas):
            result = pdtypes.apply._datetime_to_timedelta(d)
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_mixed_aware_naive_datetime_to_timedelta_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        vec = np.vectorize(pdtypes.apply._datetime_to_timedelta)
        result = vec(np.array(datetimes))
        expected = np.array(timedeltas)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_aware_naive_datetime_to_timedelta_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_timedelta)
        expected = pd.Series(timedeltas)
        pd.testing.assert_series_equal(result, expected)

    ########################################
    ####    Mixed Timezone Datetimes    ####
    ########################################

    def test_mixed_timezone_datetime_to_timedelta_scalar(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        for d, t in zip(datetimes, timedeltas):
            result = pdtypes.apply._datetime_to_timedelta(d)
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_mixed_timezone_datetime_to_timedelta_vector(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        vec = np.vectorize(pdtypes.apply._datetime_to_timedelta)
        result = vec(np.array(datetimes))
        expected = np.array(timedeltas)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_timezone_datetime_to_timedelta_series(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_timedelta)
        expected = pd.Series(timedeltas)
        pd.testing.assert_series_equal(result, expected)


class ApplyDatetimeToTimedeltaReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_pandas_timestamp_to_standard_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "UTC") for f in floats]
        timedeltas = [timedelta(seconds=f) for f in floats]
        for d, t in zip(datetimes, timedeltas):
            result = pdtypes.apply._datetime_to_timedelta(d, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_standard_datetime_to_pandas_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [datetime.fromtimestamp(f, timezone.utc) for f in floats]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        for d, t in zip(datetimes, timedeltas):
            result = pdtypes.apply._datetime_to_timedelta(d, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_standard_datetime_to_standard_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [datetime.fromtimestamp(f, timezone.utc) for f in floats]
        timedeltas = [timedelta(seconds=f) for f in floats]
        for d, t in zip(datetimes, timedeltas):
            result = pdtypes.apply._datetime_to_timedelta(d, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))


if __name__ == "__main__":
    unittest.main()
