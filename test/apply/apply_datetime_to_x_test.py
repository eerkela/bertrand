from __future__ import annotations
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
DECIMAL_PRECISION = 6


class ApplyDatetimeToIntegerTests(unittest.TestCase):

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


class ApplyDatetimeToFloatTests(unittest.TestCase):

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


class ApplyDatetimeToComplexTests(unittest.TestCase):

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


class ApplyDatetimeToStringTests(unittest.TestCase):

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


class ApplyDatetimeToBooleanTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_datetime_to_boolean_scalar(self):
        na_val = None
        expected = pd.NA
        result = pdtypes.apply._datetime_to_boolean(na_val)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_datetime_to_boolean_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NA) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_datetime_to_boolean_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        result = pd.Series(nones).apply(pdtypes.apply._datetime_to_boolean)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ##################################################################
    ####    Naive Datetime Boolean Flags [timestamp(0/1), ...]    ####
    ##################################################################

    def test_naive_datetime_boolean_flag_to_boolean_scalar(self):
        booleans = [True, False, True, False]
        datetimes = [pd.Timestamp.fromtimestamp(int(b)) for b in booleans]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_naive_datetime_boolean_flag_to_boolean_vector(self):
        booleans = [True, False, True, False]
        datetimes = [pd.Timestamp.fromtimestamp(int(b)) for b in booleans]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes))
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_naive_datetime_boolean_flag_to_boolean_series(self):
        booleans = [True, False, True, False, True]
        datetimes = [pd.Timestamp.fromtimestamp(int(b)) for b in booleans]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    #######################################################################
    ####    Aware Datetime Boolean Flags [timestamp(0/1, UTC), ...]    ####
    #######################################################################

    def test_aware_datetime_boolean_flag_to_boolean_scalar(self):
        booleans = [True, False, True, False]
        datetimes = [pd.Timestamp.fromtimestamp(int(b), "US/Pacific")
                     for b in booleans]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_aware_datetime_boolean_flag_to_boolean_vector(self):
        booleans = [True, False, True, False]
        datetimes = [pd.Timestamp.fromtimestamp(int(b), "US/Pacific")
                     for b in booleans]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes))
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_aware_datetime_boolean_flag_to_boolean_series(self):
        booleans = [True, False, True, False]
        datetimes = [pd.Timestamp.fromtimestamp(int(b), "US/Pacific")
                     for b in booleans]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    ########################################################
    ####    Mixed Aware/Naive Datetime Boolean Flags    ####
    ########################################################

    def test_mixed_aware_naive_datetime_boolean_flag_to_boolean_scalar(self):
        booleans = [True, False, True, False, True]
        datetimes = [pd.Timestamp.fromtimestamp(int(b), "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(int(b))
                     for idx, b in enumerate(booleans)]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_mixed_aware_naive_datetime_boolean_flag_to_boolean_vector(self):
        booleans = [True, False, True, False, True]
        datetimes = [pd.Timestamp.fromtimestamp(int(b), "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(int(b))
                     for idx, b in enumerate(booleans)]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes))
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_aware_naive_datetime_boolean_flag_to_boolean_series(self):
        booleans = [True, False, True, False, True]
        datetimes = [pd.Timestamp.fromtimestamp(int(b), "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(int(b))
                     for idx, b in enumerate(booleans)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    #####################################################
    ####    Mixed Timezone Datetime Boolean Flags    ####
    #####################################################

    def test_mixed_timezone_datetime_boolean_flag_to_boolean_scalar(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        booleans = [True, False, True, False, True]
        datetimes = [pd.Timestamp.fromtimestamp(int(b), get_tz(idx))
                     for idx, b in enumerate(booleans)]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_mixed_timezone_datetime_boolean_flag_to_boolean_vector(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        booleans = [True, False, True, False, True]
        datetimes = [pd.Timestamp.fromtimestamp(int(b), get_tz(idx))
                     for idx, b in enumerate(booleans)]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes))
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_timezone_datetime_boolean_flag_to_boolean_series(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        booleans = [True, False, True, False, True]
        datetimes = [pd.Timestamp.fromtimestamp(int(b), get_tz(idx))
                     for idx, b in enumerate(booleans)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    #######################################
    ####    Generic Naive Datetimes    ####
    #######################################

    def test_naive_datetime_to_boolean_within_ftol_scalar(self):
        integers = [1, 1, 0, 0, 1]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8)
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d, ftol=1e-6)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_naive_datetime_to_boolean_within_ftol_vector(self):
        integers = [1, 1, 0, 0, 1]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8)
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes), ftol=1e-6)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_naive_datetime_to_boolean_within_ftol_series(self):
        integers = [1, 1, 0, 0, 1]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8)
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean,
                                            ftol=1e-6)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    def test_naive_datetime_to_boolean_out_of_range_error(self):
        integers = [-3, -2, -1, 2, 3, 4]
        datetimes = [pd.Timestamp.fromtimestamp(i) for i in integers]
        err_msg = ("[pdtypes.apply._datetime_to_boolean] could not convert "
                   "datetime to bool without losing information: ")
        for d in datetimes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._datetime_to_boolean(d)
            self.assertEqual(str(err.exception), err_msg + repr(d))

    def test_naive_datetime_to_boolean_out_of_range_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i) for i in integers]
        booleans = [bool(i) for i in integers]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_naive_datetime_to_boolean_out_of_range_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i) for i in integers]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_naive_datetime_to_boolean_out_of_range_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i) for i in integers]
        booleans = [bool(i) for i in integers]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean,
                                            force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    def test_naive_datetime_to_boolean_decimals_between_0_and_1_error(self):
        datetimes = [pd.Timestamp.fromtimestamp(random.random())
                     for _ in range(5)]
        err_msg = ("[pdtypes.apply._datetime_to_boolean] could not convert "
                   "datetime to bool without losing information: ")
        for d in datetimes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._datetime_to_boolean(d)
            self.assertEqual(str(err.exception), err_msg + repr(d))

    def test_naive_datetime_to_boolean_decimals_between_0_and_1_forced_scalar(self):
        datetimes = [pd.Timestamp.fromtimestamp(random.random())
                     for _ in range(5)]
        booleans = [True for _ in range(5)]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_naive_datetime_to_boolean_decimals_between_0_and_1_forced_vector(self):
        datetimes = [pd.Timestamp.fromtimestamp(random.random())
                     for _ in range(5)]
        booleans = [True for _ in range(5)]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_naive_datetime_to_boolean_decimals_between_0_and_1_forced_series(self):
        datetimes = [pd.Timestamp.fromtimestamp(random.random())
                     for _ in range(5)]
        booleans = [True for _ in range(5)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean,
                                            force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    #######################################
    ####    Generic Aware Datetimes    ####
    #######################################

    def test_aware_datetime_to_boolean_within_ftol_scalar(self):
        integers = [1, 1, 0, 0, 1]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8, "US/Pacific")
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d, ftol=1e-6)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_aware_datetime_to_boolean_within_ftol_vector(self):
        integers = [1, 1, 0, 0, 1]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8, "US/Pacific")
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes), ftol=1e-6)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_aware_datetime_to_boolean_within_ftol_series(self):
        integers = [1, 1, 0, 0, 1]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8, "US/Pacific")
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean,
                                            ftol=1e-6)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    def test_aware_datetime_to_boolean_out_of_range_error(self):
        integers = [-3, -2, -1, 2, 3, 4]
        datetimes = [pd.Timestamp.fromtimestamp(i, "US/Pacific")
                     for i in integers]
        err_msg = ("[pdtypes.apply._datetime_to_boolean] could not convert "
                   "datetime to bool without losing information: ")
        for d in datetimes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._datetime_to_boolean(d)
            self.assertEqual(str(err.exception), err_msg + repr(d))

    def test_aware_datetime_to_boolean_out_of_range_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "US/Pacific")
                     for i in integers]
        booleans = [bool(i) for i in integers]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_aware_datetime_to_boolean_out_of_range_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "US/Pacific")
                     for i in integers]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_aware_datetime_to_boolean_out_of_range_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "US/Pacific")
                     for i in integers]
        booleans = [bool(i) for i in integers]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean,
                                            force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    def test_aware_datetime_to_boolean_decimals_between_0_and_1_error(self):
        datetimes = [pd.Timestamp.fromtimestamp(random.random(), "US/Pacific")
                     for _ in range(5)]
        err_msg = ("[pdtypes.apply._datetime_to_boolean] could not convert "
                   "datetime to bool without losing information: ")
        for d in datetimes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._datetime_to_boolean(d)
            self.assertEqual(str(err.exception), err_msg + repr(d))

    def test_aware_datetime_to_boolean_decimals_between_0_and_1_forced_scalar(self):
        datetimes = [pd.Timestamp.fromtimestamp(random.random(), "US/Pacific")
                     for _ in range(5)]
        booleans = [True for _ in range(5)]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_aware_datetime_to_boolean_decimals_between_0_and_1_forced_vector(self):
        datetimes = [pd.Timestamp.fromtimestamp(random.random(), "US/Pacific")
                     for _ in range(5)]
        booleans = [True for _ in range(5)]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_aware_datetime_to_boolean_decimals_between_0_and_1_forced_series(self):
        datetimes = [pd.Timestamp.fromtimestamp(random.random(), "US/Pacific")
                     for _ in range(5)]
        booleans = [True for _ in range(5)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean,
                                            force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    ###################################################
    ####    Generic Mixed Aware/Naive Datetimes    ####
    ###################################################

    def test_mixed_aware_naive_datetime_to_boolean_within_ftol_scalar(self):
        integers = [1, 1, 0, 0, 1]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d, ftol=1e-6)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_mixed_aware_naive_datetime_to_boolean_within_ftol_vector(self):
        integers = [1, 1, 0, 0, 1]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes), ftol=1e-6)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_aware_naive_datetime_to_boolean_within_ftol_series(self):
        integers = [1, 1, 0, 0, 1]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean,
                                            ftol=1e-6)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    def test_mixed_aware_naive_datetime_to_boolean_out_of_range_error(self):
        integers = [-3, -2, -1, 2, 3, 4]
        datetimes = [pd.Timestamp.fromtimestamp(i, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(i)
                     for idx, i in enumerate(integers)]
        err_msg = ("[pdtypes.apply._datetime_to_boolean] could not convert "
                   "datetime to bool without losing information: ")
        for d in datetimes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._datetime_to_boolean(d)
            self.assertEqual(str(err.exception), err_msg + repr(d))

    def test_mixed_aware_naive_datetime_to_boolean_out_of_range_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(i)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_mixed_aware_naive_datetime_to_boolean_out_of_range_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(i)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_aware_naive_datetime_to_boolean_out_of_range_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "US/Pacific") if idx % 2
                     else pd.Timestamp.fromtimestamp(i)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean,
                                            force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    def test_mixed_aware_naive_datetime_to_boolean_decimals_between_0_and_1_error(self):
        datetimes = [pd.Timestamp.fromtimestamp(random.random(), "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(random.random())
                     for idx in range(5)]
        err_msg = ("[pdtypes.apply._datetime_to_boolean] could not convert "
                   "datetime to bool without losing information: ")
        for d in datetimes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._datetime_to_boolean(d)
            self.assertEqual(str(err.exception), err_msg + repr(d))

    def test_mixed_aware_naive_datetime_to_boolean_decimals_between_0_and_1_forced_scalar(self):
        datetimes = [pd.Timestamp.fromtimestamp(random.random(), "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(random.random())
                     for idx in range(5)]
        booleans = [True for  _ in range(5)]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_mixed_aware_naive_datetime_to_boolean_decimals_between_0_and_1_forced_vector(self):
        datetimes = [pd.Timestamp.fromtimestamp(random.random(), "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(random.random())
                     for idx in range(5)]
        booleans = [True for  _ in range(5)]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_aware_naive_datetime_to_boolean_decimals_between_0_and_1_forced_series(self):
        datetimes = [pd.Timestamp.fromtimestamp(random.random(), "US/Pacific")
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(random.random())
                     for idx in range(5)]
        booleans = [True for  _ in range(5)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean,
                                            force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    ################################################
    ####    Generic Mixed Timezone Datetimes    ####
    ################################################

    def test_mixed_timezone_datetime_to_boolean_within_ftol_scalar(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [1, 1, 0, 0, 1]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, get_tz(idx))
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8, get_tz(idx))
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d, ftol=1e-6)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_mixed_timezone_datetime_to_boolean_within_ftol_vector(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [1, 1, 0, 0, 1]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, get_tz(idx))
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8, get_tz(idx))
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes), ftol=1e-6)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_timezone_datetime_to_boolean_within_ftol_series(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [1, 1, 0, 0, 1]
        datetimes = [pd.Timestamp.fromtimestamp(i + 1e-8, get_tz(idx))
                     if idx % 2 else
                     pd.Timestamp.fromtimestamp(i - 1e-8, get_tz(idx))
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean,
                                            ftol=1e-6)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    def test_mixed_timezone_datetime_to_boolean_out_of_range_error(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-3, -2, -1, 2, 3, 4]
        datetimes = [pd.Timestamp.fromtimestamp(i, get_tz(idx))
                     for idx, i in enumerate(integers)]
        err_msg = ("[pdtypes.apply._datetime_to_boolean] could not convert "
                   "datetime to bool without losing information: ")
        for d in datetimes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._datetime_to_boolean(d)
            self.assertEqual(str(err.exception), err_msg + repr(d))

    def test_mixed_timezone_datetime_to_boolean_out_of_range_forced_scalar(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, get_tz(idx))
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_mixed_timezone_datetime_to_boolean_out_of_range_forced_vector(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, get_tz(idx))
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_timezone_datetime_to_boolean_out_of_range_forced_series(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, get_tz(idx))
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean,
                                            force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    def test_mixed_timezone_datetime_to_boolean_decimals_between_0_and_1_error(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        datetimes = [pd.Timestamp.fromtimestamp(random.random(), get_tz(idx))
                     for idx in range(5)]
        err_msg = ("[pdtypes.apply._datetime_to_boolean] could not convert "
                   "datetime to bool without losing information: ")
        for d in datetimes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._datetime_to_boolean(d)
            self.assertEqual(str(err.exception), err_msg + repr(d))

    def test_mixed_timezone_datetime_to_boolean_decimals_between_0_and_1_forced_scalar(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        datetimes = [pd.Timestamp.fromtimestamp(random.random(), get_tz(idx))
                     for idx in range(5)]
        booleans = [True for _ in range(5)]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_mixed_timezone_datetime_to_boolean_decimals_between_0_and_1_forced_vector(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        datetimes = [pd.Timestamp.fromtimestamp(random.random(), get_tz(idx))
                     for idx in range(5)]
        booleans = [True for _ in range(5)]
        vec = np.vectorize(pdtypes.apply._datetime_to_boolean)
        result = vec(np.array(datetimes), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_mixed_timezone_datetime_to_boolean_decimals_between_0_and_1_forced_series(self):
        get_tz = lambda idx: pytz.all_timezones[idx % len(pytz.all_timezones)]
        datetimes = [pd.Timestamp.fromtimestamp(random.random(), get_tz(idx))
                     for idx in range(5)]
        booleans = [True for _ in range(5)]
        result = pd.Series(datetimes).apply(pdtypes.apply._datetime_to_boolean,
                                            force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_pandas_timestamp_to_custom_boolean_class_scalar(self):
        class CustomBoolean:
            def __init__(self, timestamp: float):
                self.boolean = bool(timestamp)

            def __bool__(self) -> bool:
                return self.boolean

            def __sub__(self, other) -> int:
                return self.boolean - other
        
        integers = [1, 1, 0, 0, 1]
        datetimes = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        for d in datetimes:
            result = pdtypes.apply._datetime_to_boolean(d, return_type=CustomBoolean)
            self.assertEqual(type(result), CustomBoolean)

    def test_standard_datetime_to_boolean_scalar(self):
        integers = [1, 1, 0, 0, 1]
        datetimes = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        booleans = [bool(i) for i in integers]
        for d, b in zip(datetimes, booleans):
            result = pdtypes.apply._datetime_to_boolean(d)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_standard_datetime_to_custom_boolean_class_scalar(self):
        class CustomBoolean:
            def __init__(self, timestamp: float):
                self.boolean = bool(timestamp)

            def __bool__(self) -> bool:
                return self.boolean

            def __sub__(self, other) -> int:
                return self.boolean - other
        
        integers = [1, 1, 0, 0, 1]
        datetimes = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        for d in datetimes:
            result = pdtypes.apply._datetime_to_boolean(d, return_type=CustomBoolean)
            self.assertEqual(type(result), CustomBoolean)


class ApplyDatetimeToTimedeltaTests(unittest.TestCase):

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
