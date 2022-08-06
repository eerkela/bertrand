import datetime
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.cast_old_2


class CastIntegerToBooleanAccuracyTests(unittest.TestCase):

    ####################################################
    ####    Bitwise Integers (1, 0, 1, 0, ....)     ####
    ####################################################

    def test_integer_bool_flag_to_boolean_no_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series)

        # Assert
        expected = pd.Series([bool(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_integer_bool_flag_to_boolean_with_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series)

        # Assert
        expected = pd.Series([bool(i) for i in integers] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_bool_flag_to_boolean_only_na(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series)

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_boolean_out_of_range_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] could not convert "
                   f"series to boolean without losing information: "
                   f"{list(input_series.head())}")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_out_of_range_forced_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series, force=True)

        # Assert
        expected = pd.Series([bool(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_boolean_out_of_range_forced_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series, force=True)

        # Assert
        expected = pd.Series([bool(i) for i in integers] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_boolean_out_of_range_forced_only_na(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series, force=True)

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)


class CastIntegertoBooleanOutputTypeTests(unittest.TestCase):

    ###########################################
    ####    Correct - Standard Booleans    ####
    ###########################################

    def test_integer_to_boolean_standard_boolean_output_no_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series, dtype=bool)

        # Assert
        expected = pd.Series([bool(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_boolean_standard_boolean_output_with_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series, dtype=bool)

        # Assert
        expected = pd.Series([bool(i) for i in integers] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_boolean_standard_boolean_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series, dtype=bool)

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    #############################################
    ####    Correct - Numpy Boolean Dtype    ####
    #############################################

    def test_integer_to_boolean_numpy_boolean_dtype_output_no_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series,
                                                 dtype=np.dtype(bool))

        # Assert
        expected = pd.Series([bool(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_boolean_numpy_boolean_dtype_output_with_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series,
                                                 dtype=np.dtype(bool))

        # Assert
        expected = pd.Series([bool(i) for i in integers] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_boolean_numpy_boolean_dtype_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series,
                                                 dtype=np.dtype(bool))

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_boolean_numpy_boolean_array_protocol_type_string_output_no_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series, dtype="?")

        # Assert
        expected = pd.Series([bool(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_boolean_numpy_boolean_array_protocol_type_string_output_with_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series, dtype="?")

        # Assert
        expected = pd.Series([bool(i) for i in integers] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_boolean_numpy_boolean_array_protocol_type_string_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series, dtype="?")

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    ##############################################
    ####    Correct - Pandas Boolean Dtype    ####
    ##############################################

    def test_integer_to_boolean_pandas_boolean_output_no_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series,
                                                 dtype=pd.BooleanDtype())

        # Assert
        expected = pd.Series([bool(i) for i in integers],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_boolean_pandas_boolean_output_with_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series,
                                                 dtype=pd.BooleanDtype())

        # Assert
        expected = pd.Series([bool(i) for i in integers] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_boolean_pandas_boolean_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_boolean(input_series,
                                                 dtype=pd.BooleanDtype())

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    ####################################
    ####    Incorrect - Integers    ####
    ####################################

    def test_integer_to_boolean_standard_integer_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=int)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {int})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_unsigned_int8_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.uint8)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.uint8})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_unsigned_int8_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                          dtype=np.dtype(np.uint8))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.uint8)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_unsigned_int8_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="u1")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: u1)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_unsigned_int16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.uint16)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.uint16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_unsigned_int16_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                          dtype=np.dtype(np.uint16))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.uint16)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_unsigned_int16_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="u2")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: u2)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_unsigned_int32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.uint32)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.uint32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_unsigned_int32_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                          dtype=np.dtype(np.uint32))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.uint32)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_unsigned_int32_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="u4")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: u4)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_unsigned_int64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.uint64)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.uint64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_unsigned_int64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                          dtype=np.dtype(np.uint64))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.uint64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_unsigned_int64_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="u8")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: u8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_signed_int8_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.int8)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.int8})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_signed_int8_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                          dtype=np.dtype(np.int8))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.int8)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_signed_int8_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="i1")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: i1)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_signed_int16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.int16)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.int16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_signed_int16_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                          dtype=np.dtype(np.int16))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.int16)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_signed_int16_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="i2")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: i2)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_signed_int32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.int32)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.int32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_signed_int32_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                          dtype=np.dtype(np.int32))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.int32)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_signed_int32_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="i4")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: i4)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_signed_int64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.int64)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.int64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_signed_int64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                          dtype=np.dtype(np.int64))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.int64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_signed_int64_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="i8")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: i8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_pandas_unsigned_int8_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=pd.UInt8Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.UInt8Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_pandas_unsigned_int16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=pd.UInt16Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.UInt16Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_pandas_unsigned_int32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=pd.UInt32Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.UInt32Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_pandas_unsigned_int64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=pd.UInt64Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.UInt64Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_pandas_signed_int8_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=pd.Int8Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.Int8Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_pandas_signed_int16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=pd.Int16Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.Int16Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_pandas_signed_int32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=pd.Int32Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.Int32Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_pandas_signed_int64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=pd.Int64Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.Int64Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    ##################################
    ####    Incorrect - Floats    ####
    ##################################

    def test_integer_to_boolean_standard_float_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=float)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {float})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_float16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.float16)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.float16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_float16_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                            dtype=np.dtype(np.float16))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.float16)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_float16_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="f2")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: f2)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_float32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.float32)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.float32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_float32_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                            dtype=np.dtype(np.float32))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.float32)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_float32_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="f4")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: f4)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_float64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.float64)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.float64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_float64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                            dtype=np.dtype(np.float64))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.float64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_float64_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="f8")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: f8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_longdouble_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.longdouble)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.longdouble})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_longdouble_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                            dtype=np.dtype(np.longdouble))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.longdouble)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_longdouble_array_protocol_type_string_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)
        dtype = np.dtype(np.longdouble)  # platform-specific
        type_string = f"{dtype.kind}{dtype.itemsize}"

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=type_string)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {type_string})")
        self.assertEqual(str(err.exception), err_msg)

    ###################################
    ####    Incorrect - Strings    ####
    ###################################

    def test_integer_to_boolean_standard_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=str)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {str})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_string_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.dtype(str))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(str)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_string_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="U")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: U)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_pandas_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=pd.StringDtype())
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.StringDtype()})")
        self.assertEqual(str(err.exception), err_msg)

    #####################################
    ####    Incorrect - Datetimes    ####
    #####################################

    def test_integer_to_boolean_standard_datetime_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                          dtype=datetime.datetime)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {datetime.datetime})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_datetime64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.datetime64)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.datetime64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_datetime64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                          dtype=np.dtype(np.datetime64))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.datetime64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_datetime64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="M8")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: M8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_datetime64_ns_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="M8[ns]")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: M8[ns])")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_pandas_datetime_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=pd.Timestamp)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.Timestamp})")
        self.assertEqual(str(err.exception), err_msg)

    ######################################
    ####    Incorrect - Timedeltas    ####
    ######################################

    def test_integer_to_boolean_standard_timedelta_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                          dtype=datetime.timedelta)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {datetime.timedelta})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_timdelta64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.timedelta64)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.timedelta64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_timedelta64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series,
                                          dtype=np.dtype(np.timedelta64))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.timedelta64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_timedelta64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="m8")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: m8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_numpy_timedelta64_ns_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="m8[ns]")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: m8[ns])")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_pandas_timedelta_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=pd.Timedelta)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.Timedelta})")
        self.assertEqual(str(err.exception), err_msg)

    ###################################
    ####    Incorrect - Objects    ####
    ###################################

    def test_integer_to_boolean_object_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=object)
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {object})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_object_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype=np.dtype(object))
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(object)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_object_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_boolean(input_series, dtype="O")
        err_msg = (f"[pdtypes.cast.integer_to_boolean] `dtype` must be "
                   f"bool-like (received: O)")
        self.assertEqual(str(err.exception), err_msg)


if __name__ == "__main__":
    unittest.main()
