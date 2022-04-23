from datetime import datetime, timezone
import random
import unittest

import numpy as np
import pandas as pd
import pytz

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyDatetimeToBooleanMissingValueTests(unittest.TestCase):

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


class ApplyDatetimeToBooleanAccuracyTests(unittest.TestCase):

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


class ApplyDatetimeToBooleanReturnTypeTests(unittest.TestCase):

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


if __name__ == "__main__":
    unittest.main()
