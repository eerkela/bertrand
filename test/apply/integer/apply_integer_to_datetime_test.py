from __future__ import annotations
from datetime import datetime, timezone, tzinfo
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


class ApplyIntegerToDatetimeAccuracyTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_integer_to_datetime_returns_pandas_nat(self):
        # Arrange
        na_vals = [None, np.nan, pd.NA, pd.NaT]

        # Act
        result = [pdtypes.apply.integer_to_datetime(na) for na in na_vals]

        # Assert
        expected = [pd.NaT for _ in result]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_datetime_is_accurate_scalar(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i) for i in integers]

        # Assert
        expected = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_integer_to_datetime_is_accurate_vector(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_array = np.array(integers)
        int_to_datetime = np.vectorize(pdtypes.apply.integer_to_datetime)

        # Act
        result = int_to_datetime(input_array)

        # Assert
        expected = np.array([pd.Timestamp.fromtimestamp(i, "UTC")
                             for i in integers])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_to_datetime_is_accurate_series(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = input_series.apply(pdtypes.apply.integer_to_datetime)

        # Assert
        expected = pd.Series([pd.Timestamp.fromtimestamp(i, "UTC")
                              for i in integers])
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToDatetimeReturnTypeTests(unittest.TestCase):

    #################################
    ####    Standard Integers    ####
    #################################

    def test_standard_integer_to_standard_datetime_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=datetime)
                  for i in integers]

        # Assert
        expected = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_integer_to_pandas_timestamp_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=pd.Timestamp)
                  for i in integers]

        # Assert
        expected = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_integer_to_custom_datetime_return_type(self):
        class CustomDatetime:
            def __init__(self, d: datetime):
                self.datetime = d

            @classmethod
            def fromtimestamp(cls, i: int, tz: tzinfo) -> CustomDatetime:
                return cls(datetime.fromtimestamp(i, tz))

            def timestamp(self) -> float:
                return self.datetime.timestamp()

            def __eq__(self, other: datetime) -> bool:
                return self.timestamp() == other.timestamp()

        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=CustomDatetime)
                  for i in integers]

        # Assert
        expected = [CustomDatetime.fromtimestamp(i, timezone.utc)
                    for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    #####################################
    ####    Numpy Signed Integers    ####
    #####################################

    def test_numpy_signed_int8_to_standard_datetime_return_type(self):
        # Arrange
        integers = [np.int8(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=datetime)
                  for i in integers]

        # Assert
        expected = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int8_to_pandas_timestamp_return_type(self):
        # Arrange
        integers = [np.int8(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=pd.Timestamp)
                  for i in integers]

        # Assert
        expected = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int8_to_custom_datetime_return_type(self):
        class CustomDatetime:
            def __init__(self, d: datetime):
                self.datetime = d

            @classmethod
            def fromtimestamp(cls, i: int, tz: tzinfo) -> CustomDatetime:
                return cls(datetime.fromtimestamp(i, tz))

            def timestamp(self) -> float:
                return self.datetime.timestamp()

            def __eq__(self, other: datetime) -> bool:
                return self.timestamp() == other.timestamp()

        # Arrange
        integers = [np.int8(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=CustomDatetime)
                  for i in integers]

        # Assert
        expected = [CustomDatetime.fromtimestamp(i, timezone.utc)
                    for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int16_to_standard_datetime_return_type(self):
        # Arrange
        integers = [np.int16(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=datetime)
                  for i in integers]

        # Assert
        expected = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int16_to_pandas_timestamp_return_type(self):
        # Arrange
        integers = [np.int16(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=pd.Timestamp)
                  for i in integers]

        # Assert
        expected = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int16_to_custom_datetime_return_type(self):
        class CustomDatetime:
            def __init__(self, d: datetime):
                self.datetime = d

            @classmethod
            def fromtimestamp(cls, i: int, tz: tzinfo) -> CustomDatetime:
                return cls(datetime.fromtimestamp(i, tz))

            def timestamp(self) -> float:
                return self.datetime.timestamp()

            def __eq__(self, other: datetime) -> bool:
                return self.timestamp() == other.timestamp()

        # Arrange
        integers = [np.int16(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=CustomDatetime)
                  for i in integers]

        # Assert
        expected = [CustomDatetime.fromtimestamp(i, timezone.utc)
                    for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int32_to_standard_datetime_return_type(self):
        # Arrange
        integers = [np.int32(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=datetime)
                  for i in integers]

        # Assert
        expected = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int32_to_pandas_timestamp_return_type(self):
        # Arrange
        integers = [np.int32(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=pd.Timestamp)
                  for i in integers]

        # Assert
        expected = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int32_to_custom_datetime_return_type(self):
        class CustomDatetime:
            def __init__(self, d: datetime):
                self.datetime = d

            @classmethod
            def fromtimestamp(cls, i: int, tz: tzinfo) -> CustomDatetime:
                return cls(datetime.fromtimestamp(i, tz))

            def timestamp(self) -> float:
                return self.datetime.timestamp()

            def __eq__(self, other: datetime) -> bool:
                return self.timestamp() == other.timestamp()

        # Arrange
        integers = [np.int32(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=CustomDatetime)
                  for i in integers]

        # Assert
        expected = [CustomDatetime.fromtimestamp(i, timezone.utc)
                    for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int64_to_standard_datetime_return_type(self):
        # Arrange
        integers = [np.int64(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=datetime)
                  for i in integers]

        # Assert
        expected = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int64_to_pandas_timestamp_return_type(self):
        # Arrange
        integers = [np.int64(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=pd.Timestamp)
                  for i in integers]

        # Assert
        expected = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int64_to_custom_datetime_return_type(self):
        class CustomDatetime:
            def __init__(self, d: datetime):
                self.datetime = d

            @classmethod
            def fromtimestamp(cls, i: int, tz: tzinfo) -> CustomDatetime:
                return cls(datetime.fromtimestamp(i, tz))

            def timestamp(self) -> float:
                return self.datetime.timestamp()

            def __eq__(self, other: datetime) -> bool:
                return self.timestamp() == other.timestamp()

        # Arrange
        integers = [np.int64(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=CustomDatetime)
                  for i in integers]

        # Assert
        expected = [CustomDatetime.fromtimestamp(i, timezone.utc)
                    for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    #######################################
    ####    Numpy Unsigned Integers    ####
    #######################################

    def test_numpy_unsigned_int8_to_standard_datetime_return_type(self):
        # Arrange
        integers = [np.uint8(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=datetime)
                  for i in integers]

        # Assert
        expected = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int8_to_pandas_timestamp_return_type(self):
        # Arrange
        integers = [np.uint8(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=pd.Timestamp)
                  for i in integers]

        # Assert
        expected = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int8_to_custom_datetime_return_type(self):
        class CustomDatetime:
            def __init__(self, d: datetime):
                self.datetime = d

            @classmethod
            def fromtimestamp(cls, i: int, tz: tzinfo) -> CustomDatetime:
                return cls(datetime.fromtimestamp(i, tz))

            def timestamp(self) -> float:
                return self.datetime.timestamp()

            def __eq__(self, other: datetime) -> bool:
                return self.timestamp() == other.timestamp()

        # Arrange
        integers = [np.uint8(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=CustomDatetime)
                  for i in integers]

        # Assert
        expected = [CustomDatetime.fromtimestamp(i, timezone.utc)
                    for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int16_to_standard_datetime_return_type(self):
        # Arrange
        integers = [np.uint16(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=datetime)
                  for i in integers]

        # Assert
        expected = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int16_to_pandas_timestamp_return_type(self):
        # Arrange
        integers = [np.uint16(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=pd.Timestamp)
                  for i in integers]

        # Assert
        expected = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int16_to_custom_datetime_return_type(self):
        class CustomDatetime:
            def __init__(self, d: datetime):
                self.datetime = d

            @classmethod
            def fromtimestamp(cls, i: int, tz: tzinfo) -> CustomDatetime:
                return cls(datetime.fromtimestamp(i, tz))

            def timestamp(self) -> float:
                return self.datetime.timestamp()

            def __eq__(self, other: datetime) -> bool:
                return self.timestamp() == other.timestamp()

        # Arrange
        integers = [np.uint16(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=CustomDatetime)
                  for i in integers]

        # Assert
        expected = [CustomDatetime.fromtimestamp(i, timezone.utc)
                    for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int32_to_standard_datetime_return_type(self):
        # Arrange
        integers = [np.uint32(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=datetime)
                  for i in integers]

        # Assert
        expected = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int32_to_pandas_timestamp_return_type(self):
        # Arrange
        integers = [np.uint32(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=pd.Timestamp)
                  for i in integers]

        # Assert
        expected = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int32_to_custom_datetime_return_type(self):
        class CustomDatetime:
            def __init__(self, d: datetime):
                self.datetime = d

            @classmethod
            def fromtimestamp(cls, i: int, tz: tzinfo) -> CustomDatetime:
                return cls(datetime.fromtimestamp(i, tz))

            def timestamp(self) -> float:
                return self.datetime.timestamp()

            def __eq__(self, other: datetime) -> bool:
                return self.timestamp() == other.timestamp()

        # Arrange
        integers = [np.uint32(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=CustomDatetime)
                  for i in integers]

        # Assert
        expected = [CustomDatetime.fromtimestamp(i, timezone.utc)
                    for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int64_to_standard_datetime_return_type(self):
        # Arrange
        integers = [np.uint64(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=datetime)
                  for i in integers]

        # Assert
        expected = [datetime.fromtimestamp(i, timezone.utc) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int64_to_pandas_timestamp_return_type(self):
        # Arrange
        integers = [np.uint64(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=pd.Timestamp)
                  for i in integers]

        # Assert
        expected = [pd.Timestamp.fromtimestamp(i, "UTC") for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int64_to_custom_datetime_return_type(self):
        class CustomDatetime:
            def __init__(self, d: datetime):
                self.datetime = d

            @classmethod
            def fromtimestamp(cls, i: int, tz: tzinfo) -> CustomDatetime:
                return cls(datetime.fromtimestamp(i, tz))

            def timestamp(self) -> float:
                return self.datetime.timestamp()

            def __eq__(self, other: datetime) -> bool:
                return self.timestamp() == other.timestamp()

        # Arrange
        integers = [np.uint64(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_datetime(i, return_type=CustomDatetime)
                  for i in integers]

        # Assert
        expected = [CustomDatetime.fromtimestamp(i, timezone.utc)
                    for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])


if __name__ == "__main__":
    unittest.main()
