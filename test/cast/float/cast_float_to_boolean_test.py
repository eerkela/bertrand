import datetime
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.cast_old_2


random.seed(12345)


class CastFloatToBooleanAccuracyTests(unittest.TestCase):

    #############################################################
    ####    Float Boolean Flags [1.0, 0.0, 1.0, 0.0, ...]    ####
    #############################################################

    def test_float_boolean_flag_to_boolean_no_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_boolean(input_series)

        # Assert
        expected = pd.Series([bool(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_float_boolean_flag_to_boolean_with_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series)

        # Assert
        expected = pd.Series([bool(i) for i in integers] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_float_boolean_flag_to_boolean_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series)

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    #########################################################################
    ####    Generic Floats [..., -2.xx, -1.xx, 0.xx, 1.xx, 2.xx, ...]    ####
    #########################################################################

    def test_generic_float_to_boolean_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(ValueError) as err:
            pdtypes.cast.float_to_boolean(input_series)
        err_msg = (f"[pdtypes.cast.float_to_boolean] could not convert series "
                   f"to boolean without losing information: "
                   f"{list(input_series.head())}")
        self.assertEqual(str(err.exception), err_msg)

    def test_generic_float_to_boolean_forced_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, force=True)

        # Assert
        expected = pd.Series([bool(f) for f in floats])
        pd.testing.assert_series_equal(result, expected)

    def test_generic_float_to_boolean_forced_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, force=True)

        # Assert
        expected = pd.Series([bool(f) for f in floats] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_generic_float_to_boolean_forced_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, force=True)

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_generic_float_to_boolean_rounded_out_of_range_error(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(ValueError) as err:
            pdtypes.cast.float_to_boolean(input_series, round=True)
        err_msg = (f"[pdtypes.cast.float_to_boolean] could not convert series "
                   f"to boolean without losing information: "
                   f"{list(input_series.head())}")
        self.assertEqual(str(err.exception), err_msg)

    def test_generic_float_to_boolean_rounded_forced_no_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 for i in integers]
        # `floats` contains 1 False at index 2 after rounding
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, round=True,
                                               force=True)

        # Assert
        expected = pd.Series([bool(np.round(f)) for f in floats])
        pd.testing.assert_series_equal(result, expected)

    def test_generic_float_to_boolean_rounded_forced_with_na(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 for i in integers]
        # `floats` contains 1 False at index 2 after rounding
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, round=True,
                                               force=True)

        # Assert
        expected = pd.Series([bool(np.round(f)) for f in floats] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_generic_float_to_boolean_rounded_forced_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, round=True,
                                               force=True)

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    ######################################
    ####    Floats Between 0 And 1    ####
    ######################################

    def test_float_between_0_and_1_to_boolean_error(self):
        # Arrange
        floats = [random.random() for _ in range(5)]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(ValueError) as err:
            pdtypes.cast.float_to_boolean(input_series)
        err_msg = (f"[pdtypes.cast.float_to_boolean] could not convert series "
                   f"to boolean without losing information: "
                   f"{list(input_series.head())}")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_between_0_and_1_to_boolean_forced_no_na(self):
        # Arrange
        floats = [random.random() for _ in range(5)]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, force=True)

        # Assert
        expected = pd.Series([bool(f) for f in floats])
        pd.testing.assert_series_equal(result, expected)

    def test_float_between_0_and_1_to_boolean_forced_with_na(self):
        # Arrange
        floats = [random.random() for _ in range(5)]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, force=True)

        # Assert
        expected = pd.Series([bool(f) for f in floats] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_float_between_0_and_1_to_boolean_forced_no_na(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, force=True)

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_float_between_0_and_1_to_boolean_rounded_forced_no_na(self):
        # Arrange
        floats = [random.random() for _ in range(5)]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, round=True,
                                               force=True)

        # Assert
        expected = pd.Series([bool(np.round(f)) for f in floats])
        pd.testing.assert_series_equal(result, expected)

    def test_float_between_0_and_1_to_boolean_rounded_forced_with_na(self):
        # Arrange
        floats = [random.random() for _ in range(5)]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, round=True,
                                               force=True)

        # Assert
        expected = pd.Series([bool(np.round(f)) for f in floats] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_float_between_0_and_1_to_boolean_rounded_forced_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, round=True,
                                               force=True)

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    #######################################
    ####    Floats Within Tolerance    ####
    #######################################

    def test_float_within_tol_to_boolean_no_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [i + 1e-8 if idx % 2 else i - 1e-8
                  for idx, i in enumerate(integers)]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, tol=1e-6)

        # Assert
        expected = pd.Series([bool(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_float_within_tol_to_boolean_with_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [i + 1e-8 if idx % 2 else i - 1e-8
                  for idx, i in enumerate(integers)]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, tol=1e-6)

        # Assert
        expected = pd.Series([bool(i) for i in integers] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_float_within_tol_to_boolean_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, tol=1e-6)

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)


class CastFloatToBooleanOutputTypeTests(unittest.TestCase):

    ###########################################
    ####    Correct - Standard Booleans    ####
    ###########################################

    def test_float_to_boolean_standard_boolean_output_no_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, dtype=bool)

        # Assert
        expected = pd.Series([bool(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_boolean_standard_boolean_output_with_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, dtype=bool)

        # Assert
        expected = pd.Series([bool(i) for i in integers] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_boolean_standard_boolean_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, dtype=bool)

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    #############################################
    ####    Correct - Numpy Boolean Dtype    ####
    #############################################

    def test_float_to_boolean_numpy_boolean_dtype_output_no_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_boolean(input_series,
                                               dtype=np.dtype(bool))

        # Assert
        expected = pd.Series([bool(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_boolean_numpy_boolean_dtype_output_with_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series,
                                               dtype=np.dtype(bool))

        # Assert
        expected = pd.Series([bool(i) for i in integers] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_boolean_numpy_boolean_dtype_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series,
                                               dtype=np.dtype(bool))

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_boolean_numpy_boolean_array_protocol_type_string_output_no_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, dtype="?")

        # Assert
        expected = pd.Series([bool(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_boolean_numpy_boolean_array_protocol_type_string_output_with_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, dtype="?")

        # Assert
        expected = pd.Series([bool(i) for i in integers] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_boolean_numpy_boolean_array_protocol_type_string_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series, dtype="?")

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    ##############################################
    ####    Correct - Pandas Boolean Dtype    ####
    ##############################################

    def test_float_to_boolean_pandas_boolean_output_no_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = pdtypes.cast.float_to_boolean(input_series,
                                               dtype=pd.BooleanDtype())

        # Assert
        expected = pd.Series([bool(i) for i in integers],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_boolean_pandas_boolean_output_with_na(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats + [None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series,
                                               dtype=pd.BooleanDtype())

        # Assert
        expected = pd.Series([bool(i) for i in integers] + [None],
                             dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    def test_float_to_boolean_pandas_boolean_output_na_only(self):
        # Arrange
        input_series = pd.Series([None, None, None])

        # Act
        result = pdtypes.cast.float_to_boolean(input_series,
                                               dtype=pd.BooleanDtype())

        # Assert
        expected = pd.Series([None, None, None], dtype=pd.BooleanDtype())
        pd.testing.assert_series_equal(result, expected)

    #####################################
    ####    Incorrect - Integers    #####
    #####################################

    def test_float_to_boolean_standard_integer_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=int)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {int})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_unsigned_int8_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.uint8)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.uint8})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_unsigned_int8_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.uint8))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.uint8)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_unsigned_int8_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="u1")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: u1)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_unsigned_int16_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.uint16)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.uint16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_unsigned_int16_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.uint16))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.uint16)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_unsigned_int16_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="u2")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: u2)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_unsigned_int32_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.uint32)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.uint32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_unsigned_int32_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.uint32))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.uint32)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_unsigned_int32_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="u4")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: u4)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_unsigned_int64_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.uint64)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.uint64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_unsigned_int64_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.uint64))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.uint64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_unsigned_int64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="u8")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: u8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_signed_int8_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.int8)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.int8})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_signed_int8_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.int8))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.int8)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_signed_int8_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="i1")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: i1)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_signed_int16_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.int16)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.int16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_signed_int16_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.int16))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.int16)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_signed_int16_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="i2")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: i2)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_signed_int32_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.int32)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.int32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_signed_int32_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.int32))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.int32)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_signed_int32_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="i4")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: i4)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_signed_int64_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.int64)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.int64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_signed_int64_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.int64))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.int64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_signed_int64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="i8")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: i8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_pandas_unsigned_int8_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=pd.UInt8Dtype())
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.UInt8Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_pandas_unsigned_int16_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=pd.UInt16Dtype())
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.UInt16Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_pandas_unsigned_int32_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=pd.UInt32Dtype())
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.UInt32Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_pandas_unsigned_int64_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=pd.UInt64Dtype())
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.UInt64Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_pandas_signed_int8_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=pd.Int8Dtype())
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.Int8Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_pandas_signed_int16_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=pd.Int16Dtype())
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.Int16Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_pandas_signed_int32_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=pd.Int32Dtype())
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.Int32Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_pandas_signed_int64_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=pd.Int64Dtype())
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.Int64Dtype()})")
        self.assertEqual(str(err.exception), err_msg)

    ##################################
    ####    Incorrect - Floats    ####
    ##################################

    def test_float_to_boolean_standard_float_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=float)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {float})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_float16_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.float16)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.float16})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_float16_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.float16))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.float16)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_float16_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="f2")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: f2)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_float32_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.float32)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.float32})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_float32_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.float32))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.float32)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_float32_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="f4")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: f4)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_float64_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.float64)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.float64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_float64_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.float64))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.float64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_float64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="f8")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: f8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_float128_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.float128)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.float128})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_float128_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.float128))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.float128)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_float128_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="f16")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: f16)")
        self.assertEqual(str(err.exception), err_msg)

    ###########################################
    ####    Incorrect - Complex Numbers    ####
    ###########################################

    def test_float_to_boolean_standard_complex_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=complex)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {complex})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_complex64_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.complex64)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.complex64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_complex64_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.complex64))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.complex64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_complex64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="c8")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: c8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_complex128_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.complex128)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.complex128})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_complex128_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.complex128))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.complex128)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_complex128_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="c16")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: c16)")
        self.assertEqual(str(err.exception), err_msg)

    ###################################
    ####    Incorrect - Strings    ####
    ###################################

    def test_float_to_boolean_standard_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=str)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {str})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_string_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.dtype(str))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(str)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_string_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="U")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: U)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_pandas_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=pd.StringDtype())
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.StringDtype()})")
        self.assertEqual(str(err.exception), err_msg)

    #####################################
    ####    Incorrect - Datetimes    ####
    #####################################

    def test_float_to_boolean_standard_datetime_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=datetime.datetime)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {datetime.datetime})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_datetime64_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.datetime64)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.datetime64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_datetime64_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.datetime64))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.datetime64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_datetime64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="M8")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: M8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_datetime64_ns_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="M8[ns]")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: M8[ns])")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_pandas_timestamp_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=pd.Timestamp)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.Timestamp})")
        self.assertEqual(str(err.exception), err_msg)

    ######################################
    ####    Incorrect - Timedeltas    ####
    ######################################

    def test_float_to_boolean_standard_timedelta_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=datetime.timedelta)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {datetime.timedelta})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_timedelta64_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.timedelta64)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.timedelta64})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_timedelta64_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series,
                                          dtype=np.dtype(np.timedelta64))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(np.timedelta64)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_timedelta64_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="m8")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: m8)")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_timedelta64_ns_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="m8[ns]")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: m8[ns])")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_pandas_timestamp_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=pd.Timedelta)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {pd.Timedelta})")
        self.assertEqual(str(err.exception), err_msg)

    ###################################
    ####    Incorrect - Objects    ####
    ###################################

    def test_float_to_boolean_standard_object_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=object)
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {object})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_object_dtype_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype=np.dtype(object))
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: {np.dtype(object)})")
        self.assertEqual(str(err.exception), err_msg)

    def test_float_to_boolean_numpy_object_array_protocol_type_string_output_error(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        floats = [float(i) for i in integers]
        input_series = pd.Series(floats)

        # Act - Error
        with self.assertRaises(TypeError) as err:
            pdtypes.cast.float_to_boolean(input_series, dtype="O")
        err_msg = (f"[pdtypes.cast.float_to_boolean] `dtype` must be "
                   f"bool-like (received: O)")
        self.assertEqual(str(err.exception), err_msg)


if __name__ == "__main__":
    unittest.main()
