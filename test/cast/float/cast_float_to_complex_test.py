import datetime
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.cast


random.seed(12345)


"""
TODO: np.complex256 -> np.clongdouble
"""



class CastFloatToComplexAccuracyTests(unittest.TestCase):

    ############################
    ####  Generic Floats    ####
    ############################

    def test_float_to_complex_is_accurate_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats])
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_is_accurate_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series)

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats] + [None])
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_is_accurate_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series)

        # Assert
        expected = pd.Series([None, None, None], dtype=np.complex128)
        pd.testing.assert_series_equal(result, expected)


class CastFloatToComplexOutputTypeTests(unittest.TestCase):

    ##########################################
    ####    Correct - Standard Complex    ####
    ##########################################

    def test_float_to_complex_standard_complex_output_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype=complex)

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats])
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_standard_complex_output_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype=complex)

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats] + [None])
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_standard_complex_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype=complex)

        # Assert
        expected = pd.Series([None, None, None], dtype=np.complex128)
        pd.testing.assert_series_equal(result, expected)

    ######################################
    ####    Correct - np.complex64    ####
    ######################################

    def test_float_to_complex_numpy_complex64_output_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype=np.complex64)

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats],
                             dtype=np.complex64)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex64_output_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype=np.complex64)

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats] + [None],
                             dtype=np.complex64)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex64_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype=np.complex64)

        # Assert
        expected = pd.Series([None, None, None], dtype=np.complex64)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex64_dtype_output_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.dtype(np.complex64))

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats],
                             dtype=np.complex64)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex64_dtype_output_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.dtype(np.complex64))

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats] + [None],
                             dtype=np.complex64)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex64_dtype_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.dtype(np.complex64))

        # Assert
        expected = pd.Series([None, None, None], dtype=np.complex64)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex64_array_protocol_type_string_output_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype="c8")

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats],
                             dtype=np.complex64)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex64_array_protocol_type_string_output_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype="c8")

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats] + [None],
                             dtype=np.complex64)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex64_array_protocol_type_string_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype="c8")

        # Assert
        expected = pd.Series([None, None, None], dtype=np.complex64)
        pd.testing.assert_series_equal(result, expected)

    #######################################
    ####    Correct - np.complex128    ####
    #######################################

    def test_float_to_complex_numpy_complex128_output_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.complex128)

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats],
                             dtype=np.complex128)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex128_output_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.complex128)

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats] + [None],
                             dtype=np.complex128)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex128_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.complex128)

        # Assert
        expected = pd.Series([None, None, None], dtype=np.complex128)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex128_dtype_output_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.dtype(np.complex128))

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats],
                             dtype=np.complex128)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex128_dtype_output_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.dtype(np.complex128))

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats] + [None],
                             dtype=np.complex128)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex128_dtype_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.dtype(np.complex128))

        # Assert
        expected = pd.Series([None, None, None], dtype=np.complex128)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex128_array_protocol_type_string_output_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype="c16")

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats],
                             dtype=np.complex128)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex128_array_protocol_type_string_output_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype="c16")

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats] + [None],
                             dtype=np.complex128)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex128_array_protocol_type_string_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype="c16")

        # Assert
        expected = pd.Series([None, None, None], dtype=np.complex128)
        pd.testing.assert_series_equal(result, expected)

    #######################################
    ####    Correct - np.complex256    ####
    #######################################

    def test_float_to_complex_numpy_complex256_output_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.complex256)

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats],
                             dtype=np.complex256)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex256_output_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.complex256)

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats] + [None],
                             dtype=np.complex256)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex256_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.complex256)

        # Assert
        expected = pd.Series([None, None, None], dtype=np.complex256)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex256_dtype_output_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.dtype(np.complex256))

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats],
                             dtype=np.complex256)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex256_dtype_output_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.dtype(np.complex256))

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats] + [None],
                             dtype=np.complex256)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex256_dtype_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series,
                                               dtype=np.dtype(np.complex256))

        # Assert
        expected = pd.Series([None, None, None], dtype=np.complex256)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex256_array_protocol_type_string_output_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype="c32")

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats],
                             dtype=np.complex256)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex256_array_protocol_type_string_output_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype="c32")

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats] + [None],
                             dtype=np.complex256)
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_complex_numpy_complex256_array_protocol_type_string_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_complex(input_series, dtype="c32")

        # Assert
        expected = pd.Series([None, None, None], dtype=np.complex256)
        pd.testing.assert_series_equal(result, expected)

    #####################################
    ####    Incorrect - Integers    #####
    #####################################

    def test_float_to_complex_standard_integer_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=int)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {int})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_unsigned_int8_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.uint8)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.uint8})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_unsigned_int8_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series,
                                          dtype=np.dtype(np.uint8))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(np.uint8)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_unsigned_int8_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="u1")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: u1)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_unsigned_int16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.uint16)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.uint16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_unsigned_int16_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series,
                                          dtype=np.dtype(np.uint16))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(np.uint16)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_unsigned_int16_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="u2")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: u2)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_unsigned_int32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.uint32)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.uint32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_unsigned_int32_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series,
                                          dtype=np.dtype(np.uint32))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(np.uint32)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_unsigned_int32_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="u4")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: u4)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_unsigned_int64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.uint64)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.uint64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_unsigned_int64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series,
                                          dtype=np.dtype(np.uint64))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(np.uint64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_unsigned_int64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="u8")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: u8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_signed_int8_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.int8)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.int8})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_signed_int8_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series,
                                          dtype=np.dtype(np.int8))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(np.int8)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_signed_int8_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="i1")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: i1)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_signed_int16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.int16)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.int16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_signed_int16_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series,
                                          dtype=np.dtype(np.int16))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(np.int16)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_signed_int16_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="i2")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: i2)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_signed_int32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.int32)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.int32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_signed_int32_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series,
                                          dtype=np.dtype(np.int32))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(np.int32)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_signed_int32_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="i4")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: i4)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_signed_int64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.int64)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.int64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_signed_int64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series,
                                          dtype=np.dtype(np.int64))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(np.int64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_signed_int64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="i8")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: i8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_pandas_unsigned_int8_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=pd.UInt8Dtype())
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.UInt8Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_pandas_unsigned_int16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=pd.UInt16Dtype())
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.UInt16Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_pandas_unsigned_int32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=pd.UInt32Dtype())
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.UInt32Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_pandas_unsigned_int64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=pd.UInt64Dtype())
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.UInt64Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_pandas_signed_int8_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=pd.Int8Dtype())
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.Int8Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_pandas_signed_int16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=pd.Int16Dtype())
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.Int16Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_pandas_signed_int32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=pd.Int32Dtype())
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.Int32Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_pandas_signed_int64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=pd.Int64Dtype())
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.Int64Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    ##################################
    ####    Incorrect - Floats    ####
    ##################################

    def test_float_to_complex_standard_float_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=float)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {float})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_float16_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.float16)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.float16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_float16_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series,
                                          dtype=np.dtype(np.float16))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(np.float16)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_float16_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="f2")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: f2)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_float32_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.float32)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.float32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_float32_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series,
                                          dtype=np.dtype(np.float32))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(np.float32)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_float32_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="f4")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: f4)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_float64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.float64)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.float64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_float64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series,
                                          dtype=np.dtype(np.float64))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(np.float64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_float64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="f8")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: f8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_float128_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.float128)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.float128})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_float128_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series,
                                          dtype=np.dtype(np.float128))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(np.float128)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_float128_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="f16")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: f16)")
        self.assertEqual(str(err.exception), err_msg)

    ###################################
    ####    Incorrect - Strings    ####
    ###################################

    def test_float_to_complex_standard_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=str)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {str})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_string_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.dtype(str))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(str)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_string_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="U")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: U)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_pandas_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=pd.StringDtype())
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.StringDtype()})")
        self.assertEqual(str(err.exception), err_msg)

    ####################################
    ####    Incorrect - Booleans    ####
    ####################################

    def test_float_to_complex_standard_boolean_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=bool)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {bool})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_boolean_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.dtype(bool))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(bool)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_boolean_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="?")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: ?)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_pandas_boolean_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=pd.BooleanDtype())
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.BooleanDtype()})")
        self.assertEqual(str(err.exception), err_msg)

    #####################################
    ####    Incorrect - Datetimes    ####
    #####################################

    def test_float_to_complex_standard_datetime_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=datetime.datetime)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {datetime.datetime})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_datetime64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.datetime64)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.datetime64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_datetime64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series,
                                          dtype=np.dtype(np.datetime64))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(np.datetime64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_datetime64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="M8")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: M8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_datetime64_ns_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="M8[ns]")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: M8[ns])")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_pandas_timestamp_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=pd.Timestamp)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.Timestamp})")
        self.assertEqual(str(err.exception), err_msg)

    ######################################
    ####    Incorrect - Timedeltas    ####
    ######################################

    def test_float_to_complex_standard_timedelta_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=datetime.timedelta)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {datetime.timedelta})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_timedelta64_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.timedelta64)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.timedelta64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_timedelta64_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series,
                                          dtype=np.dtype(np.timedelta64))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(np.timedelta64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_timedelta64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="m8")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: m8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_timedelta64_ns_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="m8[ns]")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: m8[ns])")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_pandas_timestamp_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=pd.Timedelta)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {pd.Timedelta})")
        self.assertEqual(str(err.exception), err_msg)

    ###################################
    ####    Incorrect - Objects    ####
    ###################################

    def test_float_to_complex_standard_object_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=object)
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {object})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_object_dtype_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype=np.dtype(object))
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: {np.dtype(object)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_complex_numpy_object_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_complex(input_series, dtype="O")
        err_msg = (f"[pdtypes.cast.float_to_complex] `dtype` must be "
                   f"complex-like (received: O)")
        self.assertEqual(str(err.exception), err_msg)


if __name__ == "__main__":
    unittest.main()
