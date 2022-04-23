from datetime import datetime, timezone
import random
import unittest

import numpy as np
import pandas as pd
import pytz

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyDatetimeToIntegerMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_datetime_to_integer_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._datetime_to_integer(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_datetime_to_integer_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_datetime_to_integer_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._datetime_to_integer)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyDatetimeToIntegerAccuracyTests(unittest.TestCase):

    ###############################################
    ####    Whole Timestamp Naive Datetimes    ####
    ###############################################

    def test_whole_timestamp_naive_datetime_to_integer_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i) for i in integers]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_whole_timestamp_naive_datetime_to_integer_vector(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes))
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_whole_timestamp_naive_datetime_to_integer_series(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i) for i in integers]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    ###############################################
    ####    Whole Timestamp Aware Datetimes    ####
    ###############################################

    def test_whole_timestamp_aware_datetime_to_integer_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "US/Pacific")
                    for i in integers]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_whole_timestamp_aware_datetime_to_integer_vector(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "US/Pacific")
                    for i in integers]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes))
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_whole_timestamp_aware_datetime_to_integer_series(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "US/Pacific")
                    for i in integers]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    ###########################################################
    ####    Whole Timestamp Mixed Aware/Naive Datetimes    ####
    ###########################################################

    def test_whole_timestamp_mixed_aware_naive_datetime_to_integer_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(i)
                     for idx, i in enumerate(integers)]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_whole_timestamp_mixed_aware_naive_datetime_to_integer_vector(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(i)
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes))
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_whole_timestamp_mixed_aware_naive_datetime_to_integer_series(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(i)
                     for idx, i in enumerate(integers)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    ########################################################
    ####    Whole Timestamp Mixed Timezone Datetimes    ####
    ########################################################

    def test_whole_timestamp_mixed_timezone_datetime_to_integer_scalar(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, get_tz(idx))
                     for idx, i in enumerate(integers)]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_whole_timestamp_mixed_timezone_datetime_to_integer_vector(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, get_tz(idx))
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes))
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_whole_timestamp_mixed_timezone_datetime_to_integer_series(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, get_tz(idx))
                     for idx, i in enumerate(integers)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    #######################################
    ####    Generic Naive Datetimes    ####
    #######################################

    def test_naive_datetime_to_integer_within_ftol_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8)
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8)
                     for idx, i in enumerate(integers)]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d, ftol=1e-6)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_naive_datetime_to_integer_within_ftol_vector(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8)
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8)
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes), ftol=1e-6)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_naive_datetime_to_integer_within_ftol_series(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8)
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8)
                     for idx, i in enumerate(integers)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer,
                                            ftol=1e-6)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_naive_datetime_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2)
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2)
                     for idx, i in enumerate(integers)]
        err_msg = ("[pdtypes.apply._datetime_to_integer] could not convert "
                   "datetime to int without losing information: ")
        for d in datetimes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._datetime_to_integer(d)
            self.assertEqual(str(err.exception), err_msg + repr(d))

    def test_naive_datetime_to_integer_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2)
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2)
                     for idx, i in enumerate(integers)]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d, force=True)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_naive_datetime_to_integer_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2)
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2)
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes), force=True)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_naive_datetime_to_integer_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2)
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2)
                     for idx, i in enumerate(integers)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer,
                                            force=True)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_naive_datetime_to_integer_forced_not_rounded_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        for d, f in zip(datetimes, floats):
            result = pdtypes.apply._datetime_to_integer(d, force=True,
                                                        round=False)
            expected = int(f)
            self.assertEqual(result, expected)
            self.assertEqual(type(result), type(expected))

    def test_naive_datetime_to_integer_forced_not_rounded_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes), force=True, round=False)
        expected = np.array([int(f) for f in floats])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_naive_datetime_to_integer_forced_not_rounded_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(f) for f in floats]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer,
                                            force=True, round=False)
        expected = pd.Series([int(f) for f in floats])
        pd.testing.assert_series_equal(result, expected)

    #######################################
    ####    Generic Aware Datetimes    ####
    #######################################

    def test_aware_datetime_to_integer_within_ftol_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8, "US/Pacific")
                     for idx, i in enumerate(integers)]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d, ftol=1e-6)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_aware_datetime_to_integer_within_ftol_vector(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8, "US/Pacific")
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes), ftol=1e-6)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_aware_datetime_to_integer_within_ftol_series(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8, "US/Pacific")
                     for idx, i in enumerate(integers)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer,
                                            ftol=1e-6)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_aware_datetime_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2,
                                                "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2,
                                                "US/Pacific")
                     for idx, i in enumerate(integers)]
        err_msg = ("[pdtypes.apply._datetime_to_integer] could not convert "
                   "datetime to int without losing information: ")
        for d in datetimes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._datetime_to_integer(d)
            self.assertEqual(str(err.exception), err_msg + repr(d))

    def test_aware_datetime_to_integer_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2,
                                                "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2,
                                                "US/Pacific")
                     for idx, i in enumerate(integers)]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d, force=True)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_aware_datetime_to_integer_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2,
                                                "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2,
                                                "US/Pacific")
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes), force=True)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_aware_datetime_to_integer_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2,
                                                "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2,
                                                "US/Pacific")
                     for idx, i in enumerate(integers)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer,
                                            force=True)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_aware_datetime_to_integer_forced_not_rounded_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        for d, f in zip(datetimes, floats):
            result = pdtypes.apply._datetime_to_integer(d, force=True,
                                                        round=False)
            expected = int(f)
            self.assertEqual(result, expected)
            self.assertEqual(type(result), type(expected))

    def test_aware_datetime_to_integer_forced_not_rounded_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes), force=True, round=False)
        expected = np.array([int(f) for f in floats])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_aware_datetime_to_integer_forced_not_rounded_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific")
                     for f in floats]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer,
                                            force=True, round=False)
        expected = pd.Series([int(f) for f in floats])
        pd.testing.assert_series_equal(result, expected)

    ###################################################
    ####    Generic Mixed Aware/Naive Datetimes    ####
    ###################################################

    def test_mixed_aware_naive_datetime_to_integer_within_ftol_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8)
                     for idx, i in enumerate(integers)]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d, ftol=1e-6)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_mixed_aware_naive_datetime_to_integer_within_ftol_vector(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8)
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes), ftol=1e-6)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_aware_naive_datetime_to_integer_within_ftol_series(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8)
                     for idx, i in enumerate(integers)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer,
                                            ftol=1e-6)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_mixed_aware_naive_datetime_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2,
                                                "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2)
                     for idx, i in enumerate(integers)]
        err_msg = ("[pdtypes.apply._datetime_to_integer] could not convert "
                   "datetime to int without losing information: ")
        for d in datetimes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._datetime_to_integer(d)
            self.assertEqual(str(err.exception), err_msg + repr(d))

    def test_mixed_aware_naive_datetime_to_integer_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2,
                                                "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2)
                     for idx, i in enumerate(integers)]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d, force=True)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_mixed_aware_naive_datetime_to_integer_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2,
                                                "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2)
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes), force=True)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_aware_naive_datetime_to_integer_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2,
                                                "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2)
                     for idx, i in enumerate(integers)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer,
                                            force=True)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_mixed_aware_naive_datetime_to_integer_forced_not_rounded_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        for d, f in zip(datetimes, floats):
            result = pdtypes.apply._datetime_to_integer(d, force=True,
                                                        round=False)
            expected = int(f)
            self.assertEqual(result, expected)
            self.assertEqual(type(result), type(expected))

    def test_mixed_aware_naive_datetime_to_integer_forced_not_rounded_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes), force=True, round=False)
        expected = np.array([int(f) for f in floats])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_aware_naive_datetime_to_integer_forced_not_rounded_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(f, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(f)
                     for idx, f in enumerate(floats)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer,
                                            force=True, round=False)
        expected = pd.Series([int(f) for f in floats])
        pd.testing.assert_series_equal(result, expected)

    ################################################
    ####    Generic Mixed Timezone Datetimes    ####
    ################################################

    def test_mixed_timezone_datetime_to_integer_within_ftol_scalar(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, get_tz(idx))
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8, get_tz(idx))
                     for idx, i in enumerate(integers)]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d, ftol=1e-6)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_mixed_timezone_datetime_to_integer_within_ftol_vector(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, get_tz(idx))
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8, get_tz(idx))
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes), ftol=1e-6)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_timezone_datetime_to_integer_within_ftol_series(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, get_tz(idx))
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8, get_tz(idx))
                     for idx, i in enumerate(integers)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer,
                                            ftol=1e-6)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_mixed_timezone_datetime_to_integer_error(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2,
                                                get_tz(idx))
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2,
                                                get_tz(idx))
                     for idx, i in enumerate(integers)]
        err_msg = ("[pdtypes.apply._datetime_to_integer] could not convert "
                   "datetime to int without losing information: ")
        for d in datetimes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._datetime_to_integer(d)
            self.assertEqual(str(err.exception), err_msg + repr(d))

    def test_mixed_timezone_datetime_to_integer_forced_scalar(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2,
                                                get_tz(idx))
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2,
                                                get_tz(idx))
                     for idx, i in enumerate(integers)]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d, force=True)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_mixed_timezone_datetime_to_integer_forced_vector(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2,
                                                get_tz(idx))
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2,
                                                get_tz(idx))
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes), force=True)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_timezone_datetime_to_integer_forced_series(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i + random.random() / 2,
                                                get_tz(idx))
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - random.random() / 2,
                                                get_tz(idx))
                     for idx, i in enumerate(integers)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer,
                                            force=True)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_mixed_timezone_datetime_to_integer_forced_not_rounded_scalar(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        for d, f in zip(datetimes, floats):
            result = pdtypes.apply._datetime_to_integer(d, force=True,
                                                        round=False)
            expected = int(f)
            self.assertEqual(result, expected)
            self.assertEqual(type(result), type(expected))

    def test_mixed_timezone_datetime_to_integer_forced_not_rounded_vector(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        vec = np.vectorize(pdtypes.apply._datetime_to_integer)
        result = vec(np.array(datetimes), force=True, round=False)
        expected = np.array([int(f) for f in floats])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_timezone_datetime_to_integer_forced_not_rounded_series(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(f, get_tz(idx))
                     for idx, f in enumerate(floats)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_integer,
                                            force=True, round=False)
        expected = pd.Series([int(f) for f in floats])
        pd.testing.assert_series_equal(result, expected)


class ApplyDatetimeToIntegerReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_pandas_timestamp_to_numpy_signed_integer_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        datetimes = [pd.Timestamp.fromtimestamp(i) for i in integers]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_pandas_timestamp_to_numpy_unsigned_integer_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        datetimes = [pd.Timestamp.fromtimestamp(i) for i in integers]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_standard_datetime_to_standard_integer_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_standard_datetime_to_numpy_signed_integer_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        datetimes = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_standard_datetime_to_numpy_unsigned_integer_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        datetimes = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        for d, i in zip(datetimes, integers):
            result = pdtypes.apply._datetime_to_integer(d, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))


if __name__ == "__main__":
    unittest.main()
