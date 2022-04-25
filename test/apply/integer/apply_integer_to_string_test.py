from __future__ import annotations
from typing import Any
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


class ApplyIntegerToStringMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_integer_to_string_returns_none(self):
        # Arrange
        na_vals = [None, np.nan, pd.NA, pd.NaT]

        # Act
        result = [pdtypes.apply.integer_to_string(na) for na in na_vals]

        # Assert
        expected = [None for _ in result]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ################################
    ####    Generic Integers    ####
    ################################   

    def test_integer_to_string_is_accurate_scalar(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_string(i) for i in integers]

        # Assert
        expected = [str(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_integer_to_string_is_accurate_vector(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_array = np.array(integers)
        int_to_str = np.vectorize(pdtypes.apply.integer_to_string)

        # Act
        result = int_to_str(input_array)

        # Assert
        expected = np.array([str(i) for i in integers])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_to_string_is_accurate_series(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = input_series.apply(pdtypes.apply.integer_to_string)

        # Assert
        expected = pd.Series([str(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToStringReturnTypeTests(unittest.TestCase):

    #################################
    ####    Standard Integers    ####
    #################################

    def test_standard_integer_to_standard_string_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=str)
                  for i in integers]

        # Assert
        expected = [str(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_integer_to_custom_string_return_type(self):
        class CustomString:
            def __init__(self, i: int):
                self.string = str(i)

            def __str__(self) -> str:
                return self.string

            def __eq__(self, other: Any) -> bool:
                return str(self) == str(other)

        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=CustomString)
                  for i in integers]

        # Assert
        expected = [CustomString(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    #####################################
    ####    Numpy Signed Integers    ####
    #####################################

    def test_numpy_signed_int8_to_standard_string_return_type(self):
        # Arrange
        integers = [np.int8(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=str)
                  for i in integers]

        # Assert
        expected = [str(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int8_to_custom_string_return_type(self):
        class CustomString:
            def __init__(self, i: int):
                self.string = str(i)

            def __str__(self) -> str:
                return self.string

            def __eq__(self, other: Any) -> bool:
                return str(self) == str(other)

        # Arrange
        integers = [np.int8(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=CustomString)
                  for i in integers]

        # Assert
        expected = [CustomString(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int16_to_standard_string_return_type(self):
        # Arrange
        integers = [np.int16(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=str)
                  for i in integers]

        # Assert
        expected = [str(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int16_to_custom_string_return_type(self):
        class CustomString:
            def __init__(self, i: int):
                self.string = str(i)

            def __str__(self) -> str:
                return self.string

            def __eq__(self, other: Any) -> bool:
                return str(self) == str(other)

        # Arrange
        integers = [np.int16(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=CustomString)
                  for i in integers]

        # Assert
        expected = [CustomString(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int32_to_standard_string_return_type(self):
        # Arrange
        integers = [np.int32(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=str)
                  for i in integers]

        # Assert
        expected = [str(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int32_to_custom_string_return_type(self):
        class CustomString:
            def __init__(self, i: int):
                self.string = str(i)

            def __str__(self) -> str:
                return self.string

            def __eq__(self, other: Any) -> bool:
                return str(self) == str(other)

        # Arrange
        integers = [np.int32(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=CustomString)
                  for i in integers]

        # Assert
        expected = [CustomString(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int64_to_standard_string_return_type(self):
        # Arrange
        integers = [np.int64(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=str)
                  for i in integers]

        # Assert
        expected = [str(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int64_to_custom_string_return_type(self):
        class CustomString:
            def __init__(self, i: int):
                self.string = str(i)

            def __str__(self) -> str:
                return self.string

            def __eq__(self, other: Any) -> bool:
                return str(self) == str(other)

        # Arrange
        integers = [np.int64(i) for i in [-2, -1, 0, 1, 2]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=CustomString)
                  for i in integers]

        # Assert
        expected = [CustomString(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    #######################################
    ####    Numpy Unsigned Integers    ####
    #######################################

    def test_numpy_unsgined_int8_to_standard_string_return_type(self):
        # Arrange
        integers = [np.uint8(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=str)
                  for i in integers]

        # Assert
        expected = [str(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int8_to_custom_string_return_type(self):
        class CustomString:
            def __init__(self, i: int):
                self.string = str(i)

            def __str__(self) -> str:
                return self.string

            def __eq__(self, other: Any) -> bool:
                return str(self) == str(other)

        # Arrange
        integers = [np.uint8(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=CustomString)
                  for i in integers]

        # Assert
        expected = [CustomString(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int16_to_standard_string_return_type(self):
        # Arrange
        integers = [np.uint16(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=str)
                  for i in integers]

        # Assert
        expected = [str(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int16_to_custom_string_return_type(self):
        class CustomString:
            def __init__(self, i: int):
                self.string = str(i)

            def __str__(self) -> str:
                return self.string

            def __eq__(self, other: Any) -> bool:
                return str(self) == str(other)

        # Arrange
        integers = [np.uint16(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=CustomString)
                  for i in integers]

        # Assert
        expected = [CustomString(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int32_to_standard_string_return_type(self):
        # Arrange
        integers = [np.uint32(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=str)
                  for i in integers]

        # Assert
        expected = [str(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int32_to_custom_string_return_type(self):
        class CustomString:
            def __init__(self, i: int):
                self.string = str(i)

            def __str__(self) -> str:
                return self.string

            def __eq__(self, other: Any) -> bool:
                return str(self) == str(other)

        # Arrange
        integers = [np.uint32(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=CustomString)
                  for i in integers]

        # Assert
        expected = [CustomString(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int64_to_standard_string_return_type(self):
        # Arrange
        integers = [np.uint64(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=str)
                  for i in integers]

        # Assert
        expected = [str(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int64_to_custom_string_return_type(self):
        class CustomString:
            def __init__(self, i: int):
                self.string = str(i)

            def __str__(self) -> str:
                return self.string

            def __eq__(self, other: Any) -> bool:
                return str(self) == str(other)

        # Arrange
        integers = [np.uint64(i) for i in [0, 1, 2, 3, 4]]

        # Act
        result = [pdtypes.apply.integer_to_string(i, return_type=CustomString)
                  for i in integers]

        # Assert
        expected = [CustomString(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])


if __name__ == "__main__":
    unittest.main()
