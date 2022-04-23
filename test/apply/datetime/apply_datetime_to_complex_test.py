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


class ApplyDatetimeToComplexMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_datetime_to_complex_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._datetime_to_complex(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_datetime_to_complex_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._datetime_to_complex)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_datetime_to_complex_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._datetime_to_complex)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyDatetimeToComplexAccuracyTests(unittest.TestCase):

    ###############################
    ####    Naive Datetimes    ####
    ###############################

    def test_naive_datetime_to_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        complexes = [complex(f, 0) for f in floats]
        for d, c in zip(datetimes, complexes):
            result = pdtypes.apply._datetime_to_complex(d)
            self.assertAlmostEqual(result, c, places=DECIMAL_PRECISION)
            self.assertEqual(type(result), type(c))

    def test_naive_datetime_to_complex_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        complexes = [complex(f, 0) for f in floats]
        vec = np.vectorize(pdtypes.apply._datetime_to_complex)
        result = vec(np.array(datetimes))
        expected = np.array(complexes)
        np.testing.assert_array_almost_equal(result, expected,
                                             decimal=DECIMAL_PRECISION)
        self.assertEqual(result.dtype, expected.dtype)

    def test_naive_datetime_to_complex_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        complexes = [complex(f, 0) for f in floats]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_complex)
        # assert_series_equal approximate matching isn't working for some
        # reason, so it's done manually here
        result = result.apply(lambda c: np.round(c, decimals=DECIMAL_PRECISION)) \
                       .astype(result.dtype)  # dtype remains unchanged
        expected = pd.Series([np.round(c, decimals=DECIMAL_PRECISION)
                              for c in complexes])
        pd.testing.assert_series_equal(result, expected)

    ###############################
    ####    Aware Datetimes    ####
    ###############################

    def test_aware_datetime_to_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        complexes = [complex(f, 0) for f in floats]
        for d, c in zip(datetimes, complexes):
            result = pdtypes.apply._datetime_to_complex(d)
            self.assertAlmostEqual(result, c, places=DECIMAL_PRECISION)
            self.assertEqual(type(result), type(c))

    def test_aware_datetime_to_complex_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        complexes = [complex(f, 0) for f in floats]
        vec = np.vectorize(pdtypes.apply._datetime_to_complex)
        result = vec(np.array(datetimes))
        expected = np.array(complexes)
        np.testing.assert_array_almost_equal(result, expected,
                                             decimal=DECIMAL_PRECISION)
        self.assertEqual(result.dtype, expected.dtype)

    def test_aware_datetime_to_complex_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        complexes = [complex(f, 0) for f in floats]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_complex)
        expected = pd.Series(complexes)
        pd.testing.assert_series_equal(result, expected)

    ###########################################
    ####    Mixed Aware/Naive Datetimes    ####
    ###########################################

    def test_mixed_aware_naive_datetime_to_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        complexes = [complex(f, 0) for f in floats]
        for d, c in zip(datetimes, complexes):
            result = pdtypes.apply._datetime_to_complex(d)
            self.assertAlmostEqual(result, c, places=DECIMAL_PRECISION)
            self.assertEqual(type(result), type(c))

    def test_mixed_aware_naive_datetime_to_complex_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        complexes = [complex(f, 0) for f in floats]
        vec = np.vectorize(pdtypes.apply._datetime_to_complex)
        result = vec(np.array(datetimes))
        expected = np.array(complexes)
        np.testing.assert_array_almost_equal(result, expected,
                                             decimal=DECIMAL_PRECISION)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_aware_naive_datetime_to_complex_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        complexes = [complex(f, 0) for f in floats]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_complex)
        expected = pd.Series(complexes)
        pd.testing.assert_series_equal(result, expected)

    ########################################
    ####    Mixed Timezone Datetimes    ####
    ########################################

    def test_mixed_timezone_datetime_to_complex_scalar(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        complexes = [complex(f, 0) for f in floats]
        for d, c in zip(datetimes, complexes):
            result = pdtypes.apply._datetime_to_complex(d)
            self.assertAlmostEqual(result, c, places=DECIMAL_PRECISION)
            self.assertEqual(type(result), type(c))

    def test_mixed_timezone_datetime_to_complex_vector(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        complexes = [complex(f, 0) for f in floats]
        vec = np.vectorize(pdtypes.apply._datetime_to_complex)
        result = vec(np.array(datetimes))
        expected = np.array(complexes)
        np.testing.assert_array_almost_equal(result, expected,
                                             decimal=DECIMAL_PRECISION)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_timezone_datetime_to_complex_series(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        complexes = [complex(f, 0) for f in floats]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_complex)
        expected = pd.Series(complexes)
        pd.testing.assert_series_equal(result, expected)


class ApplyDatetimeToComplexReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_pandas_timestamp_to_numpy_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](f)
                     for idx, f in enumerate(floats)]
        for d, c in zip(datetimes, complexes):
            result = pdtypes.apply._datetime_to_complex(d, return_type=type(c))
            self.assertAlmostEqual(result, c, places=DECIMAL_PRECISION)
            self.assertEqual(type(result), type(c))

    def test_standard_datetime_to_standard_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [datetime.fromtimestamp(f) for f in floats]
        complexes = [complex(f, 0) for f in floats]
        for d, c in zip(datetimes, complexes):
            result = pdtypes.apply._datetime_to_complex(d, return_type=type(c))
            self.assertAlmostEqual(result, c, places=DECIMAL_PRECISION)
            self.assertEqual(type(result), type(c))

    def test_standard_datetime_to_numpy_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [datetime.fromtimestamp(f) for f in floats]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](f)
                     for idx, f in enumerate(floats)]
        for d, c in zip(datetimes, complexes):
            result = pdtypes.apply._datetime_to_complex(d, return_type=type(c))
            self.assertAlmostEqual(result, c, places=DECIMAL_PRECISION)
            self.assertEqual(type(result), type(c))


if __name__ == "__main__":
    unittest.main()
