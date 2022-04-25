from __future__ import annotations
from typing import Any
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


class ApplyIntegerToFloatAccuracyTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_integer_to_float_returns_numpy_nan(self):
        # Arrange
        na_vals = [None, np.nan, pd.NA, pd.NaT]

        # Act
        result = [pdtypes.apply.integer_to_float(na) for na in na_vals]

        # Assert
        expected = [np.nan for _ in result]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_float_is_accurate_scalar(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_float(i) for i in integers]

        # Assert
        expected = [float(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_integer_to_float_is_accurate_vector(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_array = np.array(integers)
        int_to_float = np.vectorize(pdtypes.apply.integer_to_float)

        # Act
        result = int_to_float(input_array)

        # Assert
        expected = np.array([float(i) for i in integers])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_to_float_is_accurate_series(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = input_series.apply(pdtypes.apply.integer_to_float)

        # Assert
        expected = pd.Series([float(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToFloatReturnTypeTests(unittest.TestCase):

    #################################
    ####    Standard Integers    ####
    #################################

    def test_standard_integer_to_standard_float_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=float)
                  for i in integers]

        # Assert
        expected = [float(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_integer_to_numpy_float16_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float16)
                  for i in integers]

        # Assert
        expected = [np.float16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_integer_to_numpy_float32_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float32)
                  for i in integers]

        # Assert
        expected = [np.float32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_integer_to_numpy_float64_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float64)
                  for i in integers]

        # Assert
        expected = [np.float64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_integer_to_custom_float_return_type(self):
        class CustomFloat:
            def __init__(self, i: int):
                self.float = float(i)

            def __float__(self) -> float:
                return self.float

            def __eq__(self, other: Any) -> bool:
                return float(self) == float(other)

        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=CustomFloat)
                  for i in integers]

        # Assert
        expected = [CustomFloat(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    #####################################
    ####    Numpy Signed Integers    ####
    #####################################

    def test_numpy_signed_int8_to_standard_float_return_type(self):
        # Arrange
        integers = [np.int8(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=float)
                  for i in integers]

        # Assert
        expected = [float(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int8_to_numpy_float16_return_type(self):
        # Arrange
        integers = [np.int8(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float16)
                  for i in integers]

        # Assert
        expected = [np.float16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int8_to_numpy_float32_return_type(self):
        # Arrange
        integers = [np.int8(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float32)
                  for i in integers]

        # Assert
        expected = [np.float32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int8_to_numpy_float64_return_type(self):
        # Arrange
        integers = [np.int8(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float64)
                  for i in integers]

        # Assert
        expected = [np.float64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int8_to_custom_float_return_type(self):
        class CustomFloat:
            def __init__(self, i: int):
                self.float = float(i)

            def __float__(self) -> float:
                return self.float

            def __eq__(self, other: Any) -> bool:
                return float(self) == float(other)

        # Arrange
        integers = [np.int8(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=CustomFloat)
                  for i in integers]

        # Assert
        expected = [CustomFloat(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int16_to_standard_float_return_type(self):
        # Arrange
        integers = [np.int16(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=float)
                  for i in integers]

        # Assert
        expected = [float(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int16_to_numpy_float16_return_type(self):
        # Arrange
        integers = [np.int16(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float16)
                  for i in integers]

        # Assert
        expected = [np.float16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int16_to_numpy_float32_return_type(self):
        # Arrange
        integers = [np.int16(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float32)
                  for i in integers]

        # Assert
        expected = [np.float32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int16_to_numpy_float64_return_type(self):
        # Arrange
        integers = [np.int16(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float64)
                  for i in integers]

        # Assert
        expected = [np.float64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int16_to_custom_float_return_type(self):
        class CustomFloat:
            def __init__(self, i: int):
                self.float = float(i)

            def __float__(self) -> float:
                return self.float

            def __eq__(self, other: Any) -> bool:
                return float(self) == float(other)

        # Arrange
        integers = [np.int16(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=CustomFloat)
                  for i in integers]

        # Assert
        expected = [CustomFloat(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int32_to_standard_float_return_type(self):
        # Arrange
        integers = [np.int32(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=float)
                  for i in integers]

        # Assert
        expected = [float(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int32_to_numpy_float16_return_type(self):
        # Arrange
        integers = [np.int32(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float16)
                  for i in integers]

        # Assert
        expected = [np.float16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int32_to_numpy_float32_return_type(self):
        # Arrange
        integers = [np.int32(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float32)
                  for i in integers]

        # Assert
        expected = [np.float32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int32_to_numpy_float64_return_type(self):
        # Arrange
        integers = [np.int32(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float64)
                  for i in integers]

        # Assert
        expected = [np.float64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int32_to_custom_float_return_type(self):
        class CustomFloat:
            def __init__(self, i: int):
                self.float = float(i)

            def __float__(self) -> float:
                return self.float

            def __eq__(self, other: Any) -> bool:
                return float(self) == float(other)

        # Arrange
        integers = [np.int32(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=CustomFloat)
                  for i in integers]

        # Assert
        expected = [CustomFloat(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int64_to_standard_float_return_type(self):
        # Arrange
        integers = [np.int64(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=float)
                  for i in integers]

        # Assert
        expected = [float(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int64_to_numpy_float16_return_type(self):
        # Arrange
        integers = [np.int64(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float16)
                  for i in integers]

        # Assert
        expected = [np.float16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int64_to_numpy_float32_return_type(self):
        # Arrange
        integers = [np.int64(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float32)
                  for i in integers]

        # Assert
        expected = [np.float32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int64_to_numpy_float64_return_type(self):
        # Arrange
        integers = [np.int64(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float64)
                  for i in integers]

        # Assert
        expected = [np.float64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int64_to_custom_float_return_type(self):
        class CustomFloat:
            def __init__(self, i: int):
                self.float = float(i)

            def __float__(self) -> float:
                return self.float

            def __eq__(self, other: Any) -> bool:
                return float(self) == float(other)

        # Arrange
        integers = [np.int64(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=CustomFloat)
                  for i in integers]

        # Assert
        expected = [CustomFloat(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    #######################################
    ####    Numpy Unsigned Integers    ####
    #######################################

    def test_numpy_unsigned_int8_to_standard_float_return_type(self):
        # Arrange
        integers = [np.uint8(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=float)
                  for i in integers]

        # Assert
        expected = [float(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int8_to_numpy_float16_return_type(self):
        # Arrange
        integers = [np.uint8(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float16)
                  for i in integers]

        # Assert
        expected = [np.float16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int8_to_numpy_float32_return_type(self):
        # Arrange
        integers = [np.uint8(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float32)
                  for i in integers]

        # Assert
        expected = [np.float32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int8_to_numpy_float64_return_type(self):
        # Arrange
        integers = [np.uint8(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float64)
                  for i in integers]

        # Assert
        expected = [np.float64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int8_to_custom_float_return_type(self):
        class CustomFloat:
            def __init__(self, i: int):
                self.float = float(i)

            def __float__(self) -> float:
                return self.float

            def __eq__(self, other: Any) -> bool:
                return float(self) == float(other)

        # Arrange
        integers = [np.uint8(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=CustomFloat)
                  for i in integers]

        # Assert
        expected = [CustomFloat(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int16_to_standard_float_return_type(self):
        # Arrange
        integers = [np.uint16(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=float)
                  for i in integers]

        # Assert
        expected = [float(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int16_to_numpy_float16_return_type(self):
        # Arrange
        integers = [np.uint16(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float16)
                  for i in integers]

        # Assert
        expected = [np.float16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int16_to_numpy_float32_return_type(self):
        # Arrange
        integers = [np.uint16(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float32)
                  for i in integers]

        # Assert
        expected = [np.float32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int16_to_numpy_float64_return_type(self):
        # Arrange
        integers = [np.uint16(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float64)
                  for i in integers]

        # Assert
        expected = [np.float64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int16_to_custom_float_return_type(self):
        class CustomFloat:
            def __init__(self, i: int):
                self.float = float(i)

            def __float__(self) -> float:
                return self.float

            def __eq__(self, other: Any) -> bool:
                return float(self) == float(other)

        # Arrange
        integers = [np.uint16(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=CustomFloat)
                  for i in integers]

        # Assert
        expected = [CustomFloat(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int32_to_standard_float_return_type(self):
        # Arrange
        integers = [np.uint32(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=float)
                  for i in integers]

        # Assert
        expected = [float(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int32_to_numpy_float16_return_type(self):
        # Arrange
        integers = [np.uint32(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float16)
                  for i in integers]

        # Assert
        expected = [np.float16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int32_to_numpy_float32_return_type(self):
        # Arrange
        integers = [np.uint32(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float32)
                  for i in integers]

        # Assert
        expected = [np.float32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int32_to_numpy_float64_return_type(self):
        # Arrange
        integers = [np.uint32(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float64)
                  for i in integers]

        # Assert
        expected = [np.float64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int32_to_custom_float_return_type(self):
        class CustomFloat:
            def __init__(self, i: int):
                self.float = float(i)

            def __float__(self) -> float:
                return self.float

            def __eq__(self, other: Any) -> bool:
                return float(self) == float(other)

        # Arrange
        integers = [np.uint32(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=CustomFloat)
                  for i in integers]

        # Assert
        expected = [CustomFloat(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int64_to_standard_float_return_type(self):
        # Arrange
        integers = [np.uint64(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=float)
                  for i in integers]

        # Assert
        expected = [float(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int64_to_numpy_float16_return_type(self):
        # Arrange
        integers = [np.uint64(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float16)
                  for i in integers]

        # Assert
        expected = [np.float16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int64_to_numpy_float32_return_type(self):
        # Arrange
        integers = [np.uint64(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float32)
                  for i in integers]

        # Assert
        expected = [np.float32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int64_to_numpy_float64_return_type(self):
        # Arrange
        integers = [np.uint64(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=np.float64)
                  for i in integers]

        # Assert
        expected = [np.float64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int64_to_custom_float_return_type(self):
        class CustomFloat:
            def __init__(self, i: int):
                self.float = float(i)

            def __float__(self) -> float:
                return self.float

            def __eq__(self, other: Any) -> bool:
                return float(self) == float(other)

        # Arrange
        integers = [np.uint64(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_float(i, return_type=CustomFloat)
                  for i in integers]

        # Assert
        expected = [CustomFloat(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])


if __name__ == "__main__":
    unittest.main()
