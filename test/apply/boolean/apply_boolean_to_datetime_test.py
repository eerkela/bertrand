from __future__ import annotations
from datetime import datetime, timezone
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyBooleanToDatetimeMissingValueTests(unittest.TestCase):

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


class ApplyBooleanToDatetimeAccuracyTests(unittest.TestCase):

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


class ApplyBooleanToDatetimeReturnTypeTests(unittest.TestCase):

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


if __name__ == "__main__":
    unittest.main()
