import datetime
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.cast


class CastIntegerToStringAccuracyTests(unittest.TestCase):

    ##############################
    ####    Basic Integers    ####
    ##############################

    def test_integer_to_string_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_string(input_series)

        # Assert
        expected = pd.Series([str(i) for i in integers], dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_string_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_string(input_series)

        # Assert
        expected = pd.Series([str(i) for i in integers] + [None],
                             dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_string_only_na(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_string(input_series)

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    ####################################
    ####    Exceeds 64 bit limit    ####
    ####################################

    def test_integer_to_string_exceeds_64_bit_limit(self):
        # Arrange
        integers = [-2**128, 2**256]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_string(input_series)

        # Assert
        expected = pd.Series(integers, dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)


class CastIntegerToStringOutputTypeTests(unittest.TestCase):

    #######################
    ####    Correct    ####
    #######################

    def test_integer_to_string_standard_string_output_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_string(input_series, dtype=str)

        # Assert
        expected = pd.Series([str(i) for i in integers], dtype=str)
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_string_pandas_string_output_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_string(input_series,
                                                dtype=pd.StringDtype())

        # Assert
        expected = pd.Series([str(i) for i in integers], dtype=pd.StringDtype())
        pd.testing.assert_series_equal(result, expected)

    #########################
    ####    Incorrect    ####
    #########################

    def test_integer_to_string_standard_integer_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=int)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {int})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int8_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.uint8)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.uint8})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int16_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.uint16)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.uint16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int32_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.uint32)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.uint32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_unsigned_int64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.uint64)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.uint64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int8_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.int8)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.int8})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int16_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.int16)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.int16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int32_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.int32)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.int32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_signed_int64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.int64)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.int64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_unsigned_int8_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.UInt8Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.UInt8Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_unsigned_int16_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.UInt16Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.UInt16Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_unsigned_int32_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.UInt32Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.UInt32Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_unsigned_int64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.UInt64Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.UInt64Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_signed_int8_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.Int8Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.Int8Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_signed_int16_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.Int16Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.Int16Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_signed_int32_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.Int32Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.Int32Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_signed_int64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.Int64Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.Int64Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_standard_float_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=float)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {float})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_float16_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.float16)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.float16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_float32_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.float32)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.float32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_float64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.float64)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.float64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_standard_complex_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=complex)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {complex})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_complex64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.complex64)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.complex64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_numpy_complex128_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.complex128)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.complex128})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_standard_boolean_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=bool)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {bool})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_boolean_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.BooleanDtype())
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.BooleanDtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_standard_datetime_output_type_error(self):
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

    def test_integer_to_string_numpy_datetime64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.datetime64)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.datetime64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_datetime_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.Timestamp)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.Timestamp})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_standard_timedelta_output_type_error(self):
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

    def test_integer_to_string_numpy_timdelta64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=np.timedelta64)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {np.timedelta64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_pandas_timedelta_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=pd.Timedelta)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {pd.Timedelta})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_string_object_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_string(input_series, dtype=object)
        err_msg = (f"[pdtypes.cast.integer_to_string] `dtype` must be "
                   f"string-like (received: {object})")
        self.assertEqual(str(err.exception), err_msg)




if __name__ == "__main__":
    unittest.main()
