from datetime import datetime, timezone
import random
import unittest

import numpy as np
import pandas as pd
import pytz

from context import pdtypes
import pdtypes.apply


random.seed(12345)
DECIMAL_PRECISION = 6


class ApplyDatetimeToFloatMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_datetime_to_float_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._datetime_to_float(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_datetime_to_float_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._datetime_to_float)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_datetime_to_float_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._datetime_to_float)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyDatetimeToFloatAccuracyTests(unittest.TestCase):

    ###############################
    ####    Naive Datetimes    ####
    ###############################

    def test_naive_datetime_to_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        for d, f in zip(datetimes, floats):
            result = pdtypes.apply._datetime_to_float(d)
            self.assertAlmostEqual(result, f, places=DECIMAL_PRECISION)
            self.assertEqual(type(result), type(f))

    def test_naive_datetime_to_float_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        vec = np.vectorize(pdtypes.apply._datetime_to_float)
        result = vec(np.array(datetimes))
        expected = np.array(floats)
        np.testing.assert_array_almost_equal(result, expected,
                                             decimal=DECIMAL_PRECISION)
        self.assertEqual(result.dtype, expected.dtype)

    def test_naive_datetime_to_float_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_float)
        # assert_series_equal approximate matching isn't working for some
        # reason, so it's done manually here
        result = result.apply(lambda c: np.round(c, decimals=DECIMAL_PRECISION)) \
                       .astype(result.dtype)  # dtype remains unchanged
        expected = pd.Series([np.round(f, decimals=DECIMAL_PRECISION)
                              for f in floats])
        pd.testing.assert_series_equal(result, expected)

    ###############################
    ####    Aware Datetimes    ####
    ###############################

    def test_aware_datetime_to_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        for d, f in zip(datetimes, floats):
            result = pdtypes.apply._datetime_to_float(d)
            self.assertAlmostEqual(result, f, places=DECIMAL_PRECISION)
            self.assertEqual(type(result), type(f))

    def test_aware_datetime_to_float_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        vec = np.vectorize(pdtypes.apply._datetime_to_float)
        result = vec(np.array(datetimes))
        expected = np.array(floats)
        np.testing.assert_array_almost_equal(result, expected,
                                             decimal=DECIMAL_PRECISION)
        self.assertEqual(result.dtype, expected.dtype)

    def test_aware_datetime_to_float_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_float)
        expected = pd.Series(floats)
        pd.testing.assert_series_equal(result, expected)

    ###########################################
    ####    Mixed Aware/Naive Datetimes    ####
    ###########################################

    def test_mixed_aware_naive_datetime_to_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        for d, f in zip(datetimes, floats):
            result = pdtypes.apply._datetime_to_float(d)
            self.assertAlmostEqual(result, f, places=DECIMAL_PRECISION)
            self.assertEqual(type(result), type(f))

    def test_mixed_aware_naive_datetime_to_float_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        vec = np.vectorize(pdtypes.apply._datetime_to_float)
        result = vec(np.array(datetimes))
        expected = np.array(floats)
        np.testing.assert_array_almost_equal(result, expected,
                                             decimal=DECIMAL_PRECISION)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_aware_naive_datetime_to_float_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_float)
        expected = pd.Series(floats)
        pd.testing.assert_series_equal(result, expected)

    ########################################
    ####    Mixed Timezone Datetimes    ####
    ########################################

    def test_mixed_timezone_datetime_to_float_scalar(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        for d, f in zip(datetimes, floats):
            result = pdtypes.apply._datetime_to_float(d)
            self.assertAlmostEqual(result, f, places=DECIMAL_PRECISION)
            self.assertEqual(type(result), type(f))

    def test_mixed_timezone_datetime_to_float_vector(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        vec = np.vectorize(pdtypes.apply._datetime_to_float)
        result = vec(np.array(datetimes))
        expected = np.array(floats)
        np.testing.assert_array_almost_equal(result, expected,
                                             decimal=DECIMAL_PRECISION)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_timezone_datetime_to_float_series(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_float)
        expected = pd.Series(floats)
        pd.testing.assert_series_equal(result, expected)


class ApplyDatetimeToFloatReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_pandas_timestamp_to_numpy_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i + random.random())
                  for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(float(f)) for f in floats]
        for d, f in zip(datetimes, floats):
            result = pdtypes.apply._datetime_to_float(d, return_type=type(f))
            self.assertAlmostEqual(result, f, places=DECIMAL_PRECISION)
            self.assertEqual(type(result), type(f))

    def test_standard_datetime_to_standard_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [datetime.fromtimestamp(float(f)) for f in floats]
        for d, f in zip(datetimes, floats):
            result = pdtypes.apply._datetime_to_float(d, return_type=type(f))
            self.assertAlmostEqual(result, f, places=DECIMAL_PRECISION)
            self.assertEqual(type(result), type(f))

    def test_standard_datetime_to_numpy_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i + random.random())
                  for idx, i in enumerate(integers)]
        datetimes = [datetime.fromtimestamp(float(f)) for f in floats]
        for d, f in zip(datetimes, floats):
            result = pdtypes.apply._datetime_to_float(d, return_type=type(f))
            self.assertAlmostEqual(result, f, places=DECIMAL_PRECISION)
            self.assertEqual(type(result), type(f))


if __name__ == "__main__":
    unittest.main()
