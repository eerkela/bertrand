from __future__ import annotations
from datetime import datetime, timezone
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyIntegerToDatetimeMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_integer_to_datetime_scalar(self):
        na_val = None
        expected = pd.NaT
        result = pdtypes.apply._integer_to_datetime(na_val)
        # numpy can't parse pd.NaT, and pd.NaT != pd.NaT, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_integer_to_datetime_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        vec = np.vectorize(pdtypes.apply._integer_to_datetime)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NaT) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_integer_to_datetime_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        result = pd.Series(nones).apply(pdtypes.apply._integer_to_datetime)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToDatetimeAccuracyTests(unittest.TestCase):

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_datetime_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        for i, d in zip(integers, datetimes):
            result = pdtypes.apply._integer_to_datetime(i)
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_integer_to_datetime_vector(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        vec = np.vectorize(pdtypes.apply._integer_to_datetime)
        result = vec(np.array(integers))
        expected = np.array(datetimes)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_to_datetime_series(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        result = pd.Series(integers).apply(pdtypes.apply._integer_to_datetime)
        expected = pd.Series(datetimes)
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToDatetimeReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_integer_to_standard_datetime_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        datetimes = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        for i, d in zip(integers, datetimes):
            result = pdtypes.apply._integer_to_datetime(i, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_standard_integer_to_custom_datetime_class_scalar(self):
        class CustomDatetime:
            def __init__(self, i: int, tz):
                self.timestamp = pd.Timestamp.fromtimestamp(i, tz)

            @classmethod
            def fromtimestamp(cls, i: int, tz) -> CustomDatetime:
                return cls(i, tz)

            def to_datetime(self) -> pd.Timestamp:
                return self.timestamp

        integers = [-2, -1, 0, 1, 2]
        for i in integers:
            result = pdtypes.apply._integer_to_datetime(i, return_type=CustomDatetime)
            self.assertEqual(type(result), CustomDatetime)

    def test_numpy_signed_integer_to_pandas_timestamp_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        datetimes = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        for i, d in zip(integers, datetimes):
            result = pdtypes.apply._integer_to_datetime(i, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_numpy_signed_integer_to_standard_datetime_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        datetimes = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        for i, d in zip(integers, datetimes):
            result = pdtypes.apply._integer_to_datetime(i, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_numpy_unsigned_integer_to_pandas_timestamp_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        datetimes = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        for i, d in zip(integers, datetimes):
            result = pdtypes.apply._integer_to_datetime(i, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_numpy_unsigned_integer_to_standard_datetime_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        datetimes = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        for i, d in zip(integers, datetimes):
            result = pdtypes.apply._integer_to_datetime(i, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))


if __name__ == "__main__":
    unittest.main()
