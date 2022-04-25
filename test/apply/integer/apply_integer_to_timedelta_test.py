from datetime import timedelta
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyIntegerToTimedeltaAccuracyTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_integer_to_timedelta_returns_pandas_nat(self):
        # Arrange
        na_vals = [None, np.nan, pd.NA, pd.NaT]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(na) for na in na_vals]

        # Assert
        expected = [pd.NaT for _ in result]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_timedelta_is_accurate_scalar(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i) for i in integers]

        # Assert
        expected = [pd.Timedelta(seconds=i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_integer_to_timedelta_is_accurate_vector(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_array = np.array(integers)
        int_to_datetime = np.vectorize(pdtypes.apply.integer_to_timedelta)

        # Act
        result = int_to_datetime(input_array)

        # Assert
        expected = np.array([pd.Timedelta(seconds=i) for i in integers])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_to_timedelta_is_accurate_series(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = input_series.apply(pdtypes.apply.integer_to_timedelta)

        # Assert
        expected = pd.Series([pd.Timedelta(seconds=i) for i in integers])
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToTimedeltaReturnTypeTests(unittest.TestCase):

    #################################
    ####    Standard Integers    ####
    #################################

    def test_standard_integer_to_standard_timedelta_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=timedelta)
                  for i in integers]

        # Assert
        expected = [timedelta(seconds=i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_integer_to_pandas_timedelta_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=pd.Timedelta)
                  for i in integers]

        # Assert
        expected = [pd.Timedelta(seconds=i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_integer_to_custom_timedelta_return_type(self):
        class CustomTimedelta:
            def __init__(self, seconds: float):
                self.timedelta = timedelta(seconds=seconds)

            def total_seconds(self) -> float:
                return self.timedelta.total_seconds()

            def __eq__(self, other: timedelta) -> bool:
                return self.total_seconds() == other.total_seconds()

        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=CustomTimedelta)
                  for i in integers]

        # Assert
        expected = [CustomTimedelta(seconds=i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    #####################################
    ####    Numpy Signed Integers    ####
    #####################################

    def test_numpy_signed_int8_to_standard_timedelta_return_type(self):
        # Arrange
        integers = [np.int8(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=timedelta)
                  for i in integers]

        # Assert
        expected = [timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int8_to_pandas_timedelta_return_type(self):
        # Arrange
        integers = [np.int8(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=pd.Timedelta)
                  for i in integers]

        # Assert
        expected = [pd.Timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int8_to_custom_timedelta_return_type(self):
        class CustomTimedelta:
            def __init__(self, seconds: float):
                self.timedelta = timedelta(seconds=seconds)

            def total_seconds(self) -> float:
                return self.timedelta.total_seconds()

            def __eq__(self, other: timedelta) -> bool:
                return self.total_seconds() == other.total_seconds()

        # Arrange
        integers = [np.int8(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=CustomTimedelta)
                  for i in integers]

        # Assert
        expected = [CustomTimedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int16_to_standard_timedelta_return_type(self):
        # Arrange
        integers = [np.int16(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=timedelta)
                  for i in integers]

        # Assert
        expected = [timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int16_to_pandas_timedelta_return_type(self):
        # Arrange
        integers = [np.int16(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=pd.Timedelta)
                  for i in integers]

        # Assert
        expected = [pd.Timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int16_to_custom_timedelta_return_type(self):
        class CustomTimedelta:
            def __init__(self, seconds: float):
                self.timedelta = timedelta(seconds=seconds)

            def total_seconds(self) -> float:
                return self.timedelta.total_seconds()

            def __eq__(self, other: timedelta) -> bool:
                return self.total_seconds() == other.total_seconds()

        # Arrange
        integers = [np.int16(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=CustomTimedelta)
                  for i in integers]

        # Assert
        expected = [CustomTimedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int32_to_standard_timedelta_return_type(self):
        # Arrange
        integers = [np.int32(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=timedelta)
                  for i in integers]

        # Assert
        expected = [timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int32_to_pandas_timedelta_return_type(self):
        # Arrange
        integers = [np.int32(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=pd.Timedelta)
                  for i in integers]

        # Assert
        expected = [pd.Timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int32_to_custom_timedelta_return_type(self):
        class CustomTimedelta:
            def __init__(self, seconds: float):
                self.timedelta = timedelta(seconds=seconds)

            def total_seconds(self) -> float:
                return self.timedelta.total_seconds()

            def __eq__(self, other: timedelta) -> bool:
                return self.total_seconds() == other.total_seconds()

        # Arrange
        integers = [np.int32(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=CustomTimedelta)
                  for i in integers]

        # Assert
        expected = [CustomTimedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int64_to_standard_timedelta_return_type(self):
        # Arrange
        integers = [np.int64(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=timedelta)
                  for i in integers]

        # Assert
        expected = [timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int64_to_pandas_timedelta_return_type(self):
        # Arrange
        integers = [np.int64(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=pd.Timedelta)
                  for i in integers]

        # Assert
        expected = [pd.Timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int64_to_custom_timedelta_return_type(self):
        class CustomTimedelta:
            def __init__(self, seconds: float):
                self.timedelta = timedelta(seconds=seconds)

            def total_seconds(self) -> float:
                return self.timedelta.total_seconds()

            def __eq__(self, other: timedelta) -> bool:
                return self.total_seconds() == other.total_seconds()

        # Arrange
        integers = [np.int64(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=CustomTimedelta)
                  for i in integers]

        # Assert
        expected = [CustomTimedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    #######################################
    ####    Numpy Unsigned Integers    ####
    #######################################

    def test_numpy_unsigned_int8_to_standard_timedelta_return_type(self):
        # Arrange
        integers = [np.uint8(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=timedelta)
                  for i in integers]

        # Assert
        expected = [timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int8_to_pandas_timedelta_return_type(self):
        # Arrange
        integers = [np.uint8(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=pd.Timedelta)
                  for i in integers]

        # Assert
        expected = [pd.Timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int8_to_custom_timedelta_return_type(self):
        class CustomTimedelta:
            def __init__(self, seconds: float):
                self.timedelta = timedelta(seconds=seconds)

            def total_seconds(self) -> float:
                return self.timedelta.total_seconds()

            def __eq__(self, other: timedelta) -> bool:
                return self.total_seconds() == other.total_seconds()

        # Arrange
        integers = [np.uint8(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=CustomTimedelta)
                  for i in integers]

        # Assert
        expected = [CustomTimedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int16_to_standard_timedelta_return_type(self):
        # Arrange
        integers = [np.uint16(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=timedelta)
                  for i in integers]

        # Assert
        expected = [timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int16_to_pandas_timedelta_return_type(self):
        # Arrange
        integers = [np.uint16(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=pd.Timedelta)
                  for i in integers]

        # Assert
        expected = [pd.Timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int16_to_custom_timedelta_return_type(self):
        class CustomTimedelta:
            def __init__(self, seconds: float):
                self.timedelta = timedelta(seconds=seconds)

            def total_seconds(self) -> float:
                return self.timedelta.total_seconds()

            def __eq__(self, other: timedelta) -> bool:
                return self.total_seconds() == other.total_seconds()

        # Arrange
        integers = [np.uint16(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=CustomTimedelta)
                  for i in integers]

        # Assert
        expected = [CustomTimedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int32_to_standard_timedelta_return_type(self):
        # Arrange
        integers = [np.uint32(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=timedelta)
                  for i in integers]

        # Assert
        expected = [timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int32_to_pandas_timedelta_return_type(self):
        # Arrange
        integers = [np.uint32(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=pd.Timedelta)
                  for i in integers]

        # Assert
        expected = [pd.Timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int32_to_custom_timedelta_return_type(self):
        class CustomTimedelta:
            def __init__(self, seconds: float):
                self.timedelta = timedelta(seconds=seconds)

            def total_seconds(self) -> float:
                return self.timedelta.total_seconds()

            def __eq__(self, other: timedelta) -> bool:
                return self.total_seconds() == other.total_seconds()

        # Arrange
        integers = [np.uint32(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=CustomTimedelta)
                  for i in integers]

        # Assert
        expected = [CustomTimedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int64_to_standard_timedelta_return_type(self):
        # Arrange
        integers = [np.uint64(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=timedelta)
                  for i in integers]

        # Assert
        expected = [timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int64_to_pandas_timedelta_return_type(self):
        # Arrange
        integers = [np.uint64(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=pd.Timedelta)
                  for i in integers]

        # Assert
        expected = [pd.Timedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int64_to_custom_timedelta_return_type(self):
        class CustomTimedelta:
            def __init__(self, seconds: float):
                self.timedelta = timedelta(seconds=seconds)

            def total_seconds(self) -> float:
                return self.timedelta.total_seconds()

            def __eq__(self, other: timedelta) -> bool:
                return self.total_seconds() == other.total_seconds()

        # Arrange
        integers = [np.uint64(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_timedelta(i, return_type=CustomTimedelta)
                  for i in integers]

        # Assert
        expected = [CustomTimedelta(seconds=int(i)) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])


if __name__ == "__main__":
    unittest.main()
