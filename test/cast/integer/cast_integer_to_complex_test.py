import datetime
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.cast


class CastIntegerToComplexAccuracyTests(unittest.TestCase):

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_complex_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_complex_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers + [None])

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers] + [None])
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_complex_only_na(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([None, None, None], dtype=np.complex128)
        pd.testing.assert_series_equal(result, expected)

    ####################################
    ####    Exceeds 64-bit limit    ####
    ####################################

    def test_integer_to_complex_exceeds_64_bit_limit(self):
        # Arrange
        integers = [-2**128, 2**256]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series(integers, dtype=np.complex128)
        pd.testing.assert_series_equal(result, expected)


class CastIntegerToComplexInputTypeTests(unittest.TestCase):

    #######################
    ####    Correct    ####
    #######################

    def test_numpy_unsigned_int8_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=np.uint8)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_numpy_unsigned_int16_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=np.uint16)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_numpy_unsigned_int32_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=np.uint32)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_numpy_unsigned_int64_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=np.uint64)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_numpy_signed_int8_input_type_to_complex(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers, dtype=np.int8)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_numpy_signed_int16_input_type_to_complex(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers, dtype=np.int16)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_numpy_signed_int32_input_type_to_complex(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers, dtype=np.int32)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_numpy_signed_int64_input_type_to_complex(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers, dtype=np.int64)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_pandas_unsigned_int8_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=pd.UInt8Dtype())

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_pandas_unsigned_int16_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=pd.UInt16Dtype())

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_pandas_unsigned_int32_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=pd.UInt32Dtype())

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_pandas_unsigned_int64_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=pd.UInt64Dtype())

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_pandas_signed_int8_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=pd.Int8Dtype())

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_pandas_signed_int16_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=pd.Int16Dtype())

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_pandas_signed_int32_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=pd.Int32Dtype())

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_pandas_signed_int64_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=pd.Int64Dtype())

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_whole_numpy_float16_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=np.float16)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_whole_numpy_float32_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=np.float32)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_whole_numpy_float64_input_type_to_complex(self):
        # Arrange
        integers = [0, 1, 2, 3, 4]
        input_series = pd.Series(integers, dtype=np.float64)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    #########################
    ####    Incorrect    ####
    #########################


class CastIntegerToComplexOutputTypeTests(unittest.TestCase):

    #######################
    ####    Correct    ####
    #######################

    def test_integer_to_complex_standard_complex_output_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series,
                                                 dtype=complex)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers],
                             dtype=complex)
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_complex_numpy_complex64_output_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series,
                                                 dtype=np.complex64)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers],
                             dtype=np.complex64)
        pd.testing.assert_series_equal(result, expected)

    def test_integer_to_complex_numpy_complex128_output_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = pdtypes.cast.integer_to_complex(input_series,
                                                 dtype=np.complex128)

        # Assert
        expected = pd.Series([complex(i, 0) for i in integers],
                             dtype=np.complex128)
        pd.testing.assert_series_equal(result, expected)

    ########################
    ####   Incorrect    ####
    ########################

    def test_integer_to_complex_standard_integer_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=int)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {int})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_numpy_unsigned_int8_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=np.uint8)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {np.uint8})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_numpy_unsigned_int16_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=np.uint16)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {np.uint16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_numpy_unsigned_int32_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=np.uint32)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {np.uint32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_numpy_unsigned_int64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=np.uint64)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {np.uint64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_numpy_signed_int8_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=np.int8)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {np.int8})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_numpy_signed_int16_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=np.int16)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {np.int16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_numpy_signed_int32_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=np.int32)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {np.int32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_numpy_signed_int64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=np.int64)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {np.int64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_pandas_unsigned_int8_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=pd.UInt8Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.UInt8Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_pandas_unsigned_int16_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=pd.UInt16Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.UInt16Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_pandas_unsigned_int32_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=pd.UInt32Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.UInt32Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_pandas_unsigned_int64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=pd.UInt64Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.UInt64Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_pandas_signed_int8_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=pd.Int8Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.Int8Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_pandas_signed_int16_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=pd.Int16Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.Int16Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_pandas_signed_int32_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=pd.Int32Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.Int32Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_pandas_signed_int64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=pd.Int64Dtype())
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.Int64Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_standard_float_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=float)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {float})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_numpy_float16_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=np.float16)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {np.float16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_numpy_float32_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=np.float32)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {np.float32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_numpy_float64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=np.float64)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {np.float64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_standard_string_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=str)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {str})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_pandas_string_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=pd.StringDtype())
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.StringDtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_standard_boolean_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=bool)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {bool})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_pandas_boolean_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=pd.BooleanDtype())
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.BooleanDtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_standard_datetime_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series,
                                          dtype=datetime.datetime)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {datetime.datetime})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_numpy_datetime64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=np.datetime64)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {np.datetime64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_pandas_datetime_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=pd.Timestamp)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.Timestamp})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_standard_timedelta_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series,
                                          dtype=datetime.timedelta)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {datetime.timedelta})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_numpy_timdelta64_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=np.timedelta64)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {np.timedelta64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_pandas_timedelta_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=pd.Timedelta)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.Timedelta})")
        self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_complex_object_output_type_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.integer_to_complex(input_series, dtype=object)
        err_msg = (f"[pdtypes.cast.integer_to_complex] `dtype` must be "
                   f"complex-like (received: {object})")
        self.assertEqual(str(err.exception), err_msg)


if __name__ == "__main__":
    unittest.main()
