import datetime
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.cast_old


class CastIntegerToStringAccuracyTests(unittest.TestCase):

    ##############################
    ####    Basic Integers    ####
    ##############################

    def test_basic_integer_to_string_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_string(input_series)

        # Assert
        expected = pd.Series([str(i) for i in integers], dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_basic_integer_to_string_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_string(input_series)

        # Assert
        expected = pd.Series([str(i) for i in integers] + [None],
                             dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_basic_integer_to_string_only_na(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_string(input_series)

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_string_exceeds_64_bit_limit_no_na(self):
        # Arrange
        integers = [-2**128, 2**256]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_string(input_series)

        # Assert
        expected = pd.Series([str(i) for i in integers], dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_string_exceeds_64_bit_limit_with_na(self):
        # Arrange
        integers = [-2**128, 2**256]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_string(input_series)

        # Assert
        expected = pd.Series([str(i) for i in integers] + [None],
                             dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)


class CastIntegerToStringOutputTypeTests(unittest.TestCase):

    ##########################################
    ####    Correct - Standard Strings    ####
    ##########################################

    def test_integer_to_string_standard_string_output_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_string(input_series, dtype=str)

        # Assert
        expected = pd.Series([str(i) for i in integers], dtype=str)
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_string_standard_string_output_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_string(input_series, dtype=str)

        # Assert
        expected = pd.Series([str(i) for i in integers] + [None],
                             dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_string_standard_string_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_string(input_series, dtype=str)

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    ############################################
    ####    Correct - Numpy String Dtype    ####
    ############################################

    def test_integer_to_string_numpy_string_dtype_output_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_string(input_series,
                                                dtype=np.dtype(str))

        # Assert
        expected = pd.Series([str(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_string_numpy_string_dtype_output_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_string(input_series,
                                                dtype=np.dtype(str))

        # Assert
        expected = pd.Series([str(i) for i in integers] + [None],
                             dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_string_numpy_string_dtype_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_string(input_series,
                                                dtype=np.dtype(str))

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_string_numpy_string_array_protocol_type_string_output_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_string(input_series, dtype="U")

        # Assert
        expected = pd.Series([str(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_string_numpy_string_array_protocol_type_string_output_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_string(input_series, dtype="U")

        # Assert
        expected = pd.Series([str(i) for i in integers] + [None],
                             dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_string_numpy_string_array_protocol_type_string_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_string(input_series, dtype="U")

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    #############################################
    ####    Correct - Pandas String Dtype    ####
    #############################################

    def test_integer_to_string_pandas_string_output(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_string(input_series,
                                                dtype=pd.StringDtype())

        # Assert
        expected = pd.Series([str(i) for i in integers], dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_string_pandas_string_output_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_string(input_series,
                                                dtype=pd.StringDtype())

        # Assert
        expected = pd.Series([str(i) for i in integers] + [None],
                             dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_string_pandas_string_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_string(input_series,
                                                dtype=pd.StringDtype())

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    ###################################
    ####   Incorrect - Integers    ####
    ###################################

    def test_integer_to_string_standard_integer_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=int)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {int})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int8_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.uint8)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.uint8})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int8_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=np.dtype(np.uint8))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.uint8)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int8_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="u1")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: u1)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.uint16)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.uint16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int16_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=np.dtype(np.uint16))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.uint16)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int16_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="u2")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: u2)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.uint32)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.uint32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int32_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=np.dtype(np.uint32))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.uint32)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int32_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="u4")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: u4)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.uint64)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.uint64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=np.dtype(np.uint64))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.uint64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int64_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="u8")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: u8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int8_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.int8)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.int8})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int8_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=np.dtype(np.int8))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.int8)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int8_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="i1")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: i1)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.int16)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.int16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int16_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=np.dtype(np.int16))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.int16)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int16_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="i2")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: i2)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.int32)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.int32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int32_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=np.dtype(np.int32))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.int32)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int32_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="i4")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: i4)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.int64)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.int64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=np.dtype(np.int64))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.int64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int64_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="i8")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: i8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_unsigned_int8_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.UInt8Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.UInt8Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_unsigned_int16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.UInt16Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.UInt16Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_unsigned_int32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.UInt32Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.UInt32Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_unsigned_int64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.UInt64Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.UInt64Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_signed_int8_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.Int8Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.Int8Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_signed_int16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.Int16Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.Int16Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_signed_int32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.Int32Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.Int32Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_signed_int64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.Int64Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.Int64Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    ##################################
    ####    Incorrect - Floats    ####
    ##################################

    def test_integer_to_string_standard_float_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=float)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {float})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_float16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.float16)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.float16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_float16_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                            dtype=np.dtype(np.float16))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.float16)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_float16_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="f2")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: f2)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_float32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.float32)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.float32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_float32_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                            dtype=np.dtype(np.float32))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.float32)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_float32_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="f4")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: f4)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_float64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.float64)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.float64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_float64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                            dtype=np.dtype(np.float64))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.float64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_float64_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="f8")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: f8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_longdouble_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.longdouble)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.longdouble})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_longdouble_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                            dtype=np.dtype(np.longdouble))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.longdouble)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_longdouble_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)
        dtype = np.dtype(np.longdouble)  # platform-specific
        type_string = f"{dtype.kind}{dtype.itemsize}"

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=type_string)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {type_string})")
        self.assertEqual(str(err.exception), err_msg)

    ###########################################
    ####    Incorrect - Complex Numbers    ####
    ###########################################

    def test_integer_to_string_standard_complex_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=complex)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {complex})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_complex64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.complex64)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.complex64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_complex64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=np.dtype(np.complex64))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.complex64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_complex64_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="c8")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: c8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_complex128_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.complex128)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.complex128})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_complex128_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=np.dtype(np.complex128))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.complex128)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_complex128_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="c16")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: c16)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_clongdouble_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.clongdouble)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.clongdouble})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_clongdouble_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=np.dtype(np.clongdouble))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.clongdouble)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_clongdouble_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)
        dtype = np.dtype(np.clongdouble)  # platform-specific
        type_string = f"{dtype.kind}{dtype.itemsize}"

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=type_string)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {type_string})")
        self.assertEqual(str(err.exception), err_msg)

    ####################################
    ####    Incorrect - Booleans    ####
    ####################################

    def test_integer_to_string_standard_boolean_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=bool)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {bool})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_boolean_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.dtype(bool))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(bool)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_boolean_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="?")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: ?)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_boolean_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.BooleanDtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.BooleanDtype()})")
        self.assertEqual(str(err.exception), err_msg)

    #####################################
    ####    Incorrect - Datetimes    ####
    #####################################

    def test_integer_to_string_standard_datetime_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=datetime.datetime)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {datetime.datetime})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_datetime64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.datetime64)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.datetime64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_datetime64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=np.dtype(np.datetime64))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.datetime64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_datetime64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="M8")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: M8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_datetime64_ns_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="M8[ns]")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: M8[ns])")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_datetime_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.Timestamp)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.Timestamp})")
        self.assertEqual(str(err.exception), err_msg)

    ######################################
    ####    Incorrect - Timedeltas    ####
    ######################################

    def test_integer_to_string_standard_timedelta_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=datetime.timedelta)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {datetime.timedelta})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_timdelta64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.timedelta64)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.timedelta64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_timedelta64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series,
                                          dtype=np.dtype(np.timedelta64))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(np.timedelta64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_timedelta64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="m8")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: m8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_timedelta64_ns_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="m8[ns]")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: m8[ns])")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_timedelta_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.Timedelta)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.Timedelta})")
        self.assertEqual(str(err.exception), err_msg)

    ###################################
    ####    Incorrect - Objects    ####
    ###################################

    def test_integer_to_string_object_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=object)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {object})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_object_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.dtype(object))
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.dtype(object)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_object_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype="O")
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: O)")
        self.assertEqual(str(err.exception), err_msg)


if __name__ == "__main__":
    unittest.main()
