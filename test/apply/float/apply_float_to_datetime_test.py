from __future__ import annotations
from datetime import datetime, timezone
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyFloatToDatetimeMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_float_to_datetime_scalar(self):
        na_val = None
        expected = pd.NaT
        result = pdtypes.apply._float_to_datetime(na_val)
        # numpy can't parse pd.NaT, and pd.NaT != pd.NaT, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_float_to_datetime_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        vec = np.vectorize(pdtypes.apply._float_to_datetime)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NaT) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_float_to_datetime_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        result = pd.Series(nones).apply(pdtypes.apply._float_to_datetime)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToDatetimeAccuracyTests(unittest.TestCase):

    ##############################
    ####    Generic Floats    ####
    ##############################

    def test_float_to_datetime_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "UTC") for f in floats]
        for f, d in zip(floats, datetimes):
            result = pdtypes.apply._float_to_datetime(f)
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_float_to_datetime_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "UTC") for f in floats]
        vec = np.vectorize(pdtypes.apply._float_to_datetime)
        result = vec(np.array(floats))
        expected = np.array(datetimes)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_to_datetime_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(f, "UTC") for f in floats]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_datetime)
        expected = pd.Series(datetimes)
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToDatetimeReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_float_to_standard_datetime_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        datetimes = [datetime.fromtimestamp(f, timezone.utc) for f in floats]
        for f, d in zip(floats, datetimes):
            result = pdtypes.apply._float_to_datetime(f, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_standard_float_to_custom_datetime_class_scalar(self):
        class CustomDatetime:
            def __init__(self, f: float, tz):
                self.timestamp = pd.Timestamp.fromtimestamp(f, tz)

            @classmethod
            def fromtimestamp(cls, f: float, tz) -> CustomDatetime:
                return cls(f, tz)

            def to_datetime(self) -> pd.Timestamp:
                return self.timestamp

        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        for f in floats:
            result = pdtypes.apply._float_to_datetime(f,
                                                      return_type=CustomDatetime)
            self.assertEqual(type(result), CustomDatetime)

    def test_numpy_float_to_pandas_timestamp_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(float(f), "UTC") for f in floats]
        for f, d in zip(floats, datetimes):
            result = pdtypes.apply._float_to_datetime(f, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_numpy_float_to_standard_datetime_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        datetimes = [datetime.fromtimestamp(float(f), timezone.utc) 
                     for f in floats]
        for f, d in zip(floats, datetimes):
            result = pdtypes.apply._float_to_datetime(f, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))


if __name__ == "__main__":
    unittest.main()
