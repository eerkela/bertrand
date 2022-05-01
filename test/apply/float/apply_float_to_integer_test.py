import random
from typing import Any
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyFloatToIntegerAccuracyTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_float_to_integer_returns_numpy_nan(self):
        # Arrange
        na_vals = [None, np.nan, pd.NA, pd.NaT]

        # Act
        result = [pdtypes.apply.float_to_integer(na) for na in na_vals]

        # Assert
        expected = [np.nan for _ in result]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ############################
    ####    Whole Floats    ####
    ############################

    def test_whole_float_to_integer_is_accurate_scalar(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f) for f in floats]

        # Assert
        expected = integers
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_whole_float_to_integer_is_accurate_vector(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        input_array = np.array(floats)
        float_to_int = np.vectorize(pdtypes.apply.float_to_integer)

        # Act
        result = float_to_int(input_array)

        # Assert
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_whole_float_to_integer_is_accurate_series(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = input_series.apply(pdtypes.apply.float_to_integer)

        # Assert
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    ##############################
    ####    Decimal Floats    ####
    ##############################

    def test_decimal_float_to_integer_within_ftol_scalar(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + 1e-8 if idx % 2 else i - 1e-8
                  for idx, i in enumerate(integers)]

        # Act
        result = [pdtypes.apply.float_to_integer(f, ftol=1e-6) for f in floats]

        # Assert
        expected = integers
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_decimal_float_to_integer_within_ftol_vector(self):
        # arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + 1e-8 if idx % 2 else i - 1e-8
                  for idx, i in enumerate(integers)]
        input_array = np.array(floats)
        float_to_int = np.vectorize(pdtypes.apply.float_to_integer)

        # Act
        result = float_to_int(input_array, ftol=1e-6)

        # Assert
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_decimal_float_to_integer_within_ftol_series(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + 1e-8 if idx % 2 else i - 1e-8
                  for idx, i in enumerate(integers)]
        input_series = pd.Series(floats)

        # Act
        result = input_series.apply(pdtypes.apply.float_to_integer, ftol=1e-6)

        # Assert
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_decimal_float_to_integer_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]

        # Act - error
        for f in floats:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply.float_to_integer(f)
            err_msg = (f"[pdtypes.apply.float_to_integer] could not convert "
                       f"float to int without losing information: {repr(f)}")
            self.assertEqual(str(err.exception), err_msg)

    def test_decimal_float_to_integer_forced_scalar(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]

        # Act
        result = [pdtypes.apply.float_to_integer(f, force=True) for f in floats]

        # Assert
        expected = integers
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_decimal_float_to_integer_forced_vector(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        input_array = np.array(floats)
        float_to_int = np.vectorize(pdtypes.apply.float_to_integer)

        # Act
        result = float_to_int(input_array, force=True)

        # Assert
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_decimal_float_to_integer_forced_series(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        input_series = pd.Series(floats)

        # Act
        result = input_series.apply(pdtypes.apply.float_to_integer, force=True)

        # Assert
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_decimal_float_to_integer_forced_not_rounded_scalar(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]

        # Act
        result = [pdtypes.apply.float_to_integer(f, force=True, round=False)
                  for f in floats]

        # Assert
        expected = [int(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_decimal_float_to_integer_forced_not_rounded_vector(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        input_array = np.array(floats)
        float_to_int = np.vectorize(pdtypes.apply.float_to_integer)

        # Act
        result = float_to_int(input_array, force=True, round=False)

        # Assert
        expected = np.array([int(f) for f in floats])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_decimal_float_to_integer_forced_not_rounded_series(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        input_series = pd.Series(floats)

        # Act
        result = input_series.apply(pdtypes.apply.float_to_integer, force=True,
                                    round=False)

        # Assert
        expected = pd.Series([int(f) for f in floats])
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToIntegerReturnTypeTests(unittest.TestCase):

    ###############################
    ####    Standard Floats    ####
    ###############################

    def test_standard_float_to_standard_integer_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=int)
                  for f in floats]

        # Assert
        expected = integers
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_float_to_numpy_signed_int8_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int8)
                  for f in floats]

        # Assert
        expected = [np.int8(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_float_to_numpy_signed_int16_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int16)
                  for f in floats]

        # Assert
        expected = [np.int16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_float_to_numpy_signed_int32_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int32)
                  for f in floats]

        # Assert
        expected = [np.int32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_float_to_numpy_signed_int64_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [float(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int64)
                  for f in floats]

        # Assert
        expected = [np.int64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_float_to_numpy_unsigned_int8_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [float(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint8)
                  for f in floats]

        # Assert
        expected = [np.uint8(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_float_to_numpy_unsigned_int16_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [float(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint16)
                  for f in floats]

        # Assert
        expected = [np.uint16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_float_to_numpy_unsigned_int32_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [float(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint32)
                  for f in floats]

        # Assert
        expected = [np.uint32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_float_to_numpy_unsigned_int64_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [float(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint64)
                  for f in floats]

        # Assert
        expected = [np.uint64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_float_to_custom_integer_return_type(self):
        class CustomInteger(int):
            pass

        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [float(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=CustomInteger)
                  for f in floats]

        # Assert
        expected = [CustomInteger(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ###################################
    ####    Numpy 16-bit Floats    ####
    ###################################

    def test_numpy_float16_to_standard_integer_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float16(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=int)
                  for f in floats]

        # Assert
        expected = integers
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float16_to_numpy_signed_int8_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float16(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int8)
                  for f in floats]

        # Assert
        expected = [np.int8(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float16_to_numpy_signed_int16_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float16(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int16)
                  for f in floats]

        # Assert
        expected = [np.int16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float16_to_numpy_signed_int32_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float16(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int32)
                  for f in floats]

        # Assert
        expected = [np.int32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float16_to_numpy_signed_int64_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float16(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int64)
                  for f in floats]

        # Assert
        expected = [np.int64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float16_to_numpy_unsigned_int8_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float16(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint8)
                  for f in floats]

        # Assert
        expected = [np.uint8(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float16_to_numpy_unsigned_int16_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float16(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint16)
                  for f in floats]

        # Assert
        expected = [np.uint16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float16_to_numpy_unsigned_int32_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float16(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint32)
                  for f in floats]

        # Assert
        expected = [np.uint32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float16_to_numpy_unsigned_int64_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float16(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint64)
                  for f in floats]

        # Assert
        expected = [np.uint64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float16_to_custom_integer_return_type(self):
        class CustomInteger(int):
            pass

        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float16(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=CustomInteger)
                  for f in floats]

        # Assert
        expected = [CustomInteger(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ###################################
    ####    Numpy 32-bit Floats    ####
    ###################################

    def test_numpy_float32_to_standard_integer_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float32(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=int)
                  for f in floats]

        # Assert
        expected = integers
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float32_to_numpy_signed_int8_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float32(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int8)
                  for f in floats]

        # Assert
        expected = [np.int8(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float32_to_numpy_signed_int16_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float32(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int16)
                  for f in floats]

        # Assert
        expected = [np.int16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float32_to_numpy_signed_int32_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float32(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int32)
                  for f in floats]

        # Assert
        expected = [np.int32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float32_to_numpy_signed_int64_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float32(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int64)
                  for f in floats]

        # Assert
        expected = [np.int64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float32_to_numpy_unsigned_int8_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float32(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint8)
                  for f in floats]

        # Assert
        expected = [np.uint8(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float32_to_numpy_unsigned_int16_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float32(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint16)
                  for f in floats]

        # Assert
        expected = [np.uint16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float32_to_numpy_unsigned_int32_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float32(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint32)
                  for f in floats]

        # Assert
        expected = [np.uint32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float32_to_numpy_unsigned_int64_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float32(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint64)
                  for f in floats]

        # Assert
        expected = [np.uint64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float32_to_custom_integer_return_type(self):
        class CustomInteger(int):
            pass

        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float32(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=CustomInteger)
                  for f in floats]

        # Assert
        expected = [CustomInteger(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ###################################
    ####    Numpy 64-bit Floats    ####
    ###################################

    def test_numpy_float64_to_standard_integer_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float64(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=int)
                  for f in floats]

        # Assert
        expected = integers
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float64_to_numpy_signed_int8_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float64(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int8)
                  for f in floats]

        # Assert
        expected = [np.int8(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float64_to_numpy_signed_int16_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float64(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int16)
                  for f in floats]

        # Assert
        expected = [np.int16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float64_to_numpy_signed_int32_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float64(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int32)
                  for f in floats]

        # Assert
        expected = [np.int32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float64_to_numpy_signed_int64_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float64(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.int64)
                  for f in floats]

        # Assert
        expected = [np.int64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float64_to_numpy_unsigned_int8_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float64(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint8)
                  for f in floats]

        # Assert
        expected = [np.uint8(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float64_to_numpy_unsigned_int16_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float64(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint16)
                  for f in floats]

        # Assert
        expected = [np.uint16(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float64_to_numpy_unsigned_int32_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float64(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint32)
                  for f in floats]

        # Assert
        expected = [np.uint32(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float64_to_numpy_unsigned_int64_return_type(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float64(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=np.uint64)
                  for f in floats]

        # Assert
        expected = [np.uint64(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float64_to_custom_integer_return_type(self):
        class CustomInteger(int):
            pass

        # Arrange
        integers = [0, 1, 2, 3, 4]
        floats = [np.float64(i) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_integer(f, return_type=CustomInteger)
                  for f in floats]

        # Assert
        expected = [CustomInteger(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])


if __name__ == "__main__":
    unittest.main()
