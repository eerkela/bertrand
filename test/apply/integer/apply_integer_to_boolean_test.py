from __future__ import annotations
from typing import Any
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


class ApplyIntegerToBooleanAccuracyTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_integer_to_boolean_returns_pandas_na(self):
        # Arrange
        na_vals = [None, np.nan, pd.NA, pd.NaT]

        # Act
        result = [pdtypes.apply.integer_to_boolean(na) for na in na_vals]

        # Assert
        expected = [pd.NA for _ in result]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    #######################################################
    ####    Integer Boolean Flags [1, 0, 1, 0, ...]    ####
    #######################################################

    def test_integer_bool_flag_to_boolean_is_accurate_scalar(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]

        # Act
        result = [pdtypes.apply.integer_to_boolean(i) for i in integers]

        # Assert
        expected = [bool(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_integer_bool_flag_to_boolean_is_accurate_vector(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        input_array = np.array(integers)
        int_to_bool = np.vectorize(pdtypes.apply.integer_to_boolean)

        # Act
        result = int_to_bool(input_array)

        # Assert
        expected = np.array([bool(i) for i in integers])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_bool_flag_to_boolean_is_accurate_series(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]
        input_series = pd.Series(integers)

        # Act
        result = input_series.apply(pdtypes.apply.integer_to_boolean)

        # Assert
        expected = pd.Series([bool(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_boolean_out_of_range_error(self):
        # Arrange
        integers = [-3, -2, -1, 2, 3, 4]

        # Act - error
        for i in integers:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply.integer_to_boolean(i)
            err_msg = (f"[pdtypes.apply.integer_to_boolean] could not convert int "
                       f"to bool without losing information: {repr(i)}")
            self.assertEqual(str(err.exception), err_msg)

    def test_integer_to_boolean_out_of_range_forced_scalar(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]

        # Act
        result = [pdtypes.apply.integer_to_boolean(i, force=True)
                  for i in integers]

        # Assert
        expected = [True, True, False, True, True]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_integer_to_boolean_out_of_range_forced_vector(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_array = np.array(integers)
        int_to_bool = np.vectorize(pdtypes.apply.integer_to_boolean)

        # Act
        result = int_to_bool(input_array, force=True)

        # Assert
        expected = np.array([bool(i) for i in integers])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_to_boolean_out_of_range_forced_series(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        input_series = pd.Series(integers)

        # Act
        result = input_series.apply(pdtypes.apply.integer_to_boolean,
                                    force=True)

        # Assert
        expected = pd.Series([bool(i) for i in integers])
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToBooleanReturnTypeTests(unittest.TestCase):

    #################################
    ####    Standard Integers    ####
    #################################

    def test_standard_integer_to_standard_boolean_return_type(self):
        # Arrange
        integers = [1, 1, 0, 0, 1]

        # Act
        result = [pdtypes.apply.integer_to_boolean(i, return_type=bool)
                  for i in integers]

        # Assert
        expected = [bool(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    #####################################
    ####    Numpy Signed Integers    ####
    #####################################

    def test_numpy_signed_int8_to_standard_boolean_return_type(self):
        # Arrange
        integers = [np.int8(i) for i in [1, 1, 0, 0, 1]]

        # Act
        result = [pdtypes.apply.integer_to_boolean(i, return_type=bool)
                  for i in integers]

        # Assert
        expected = [bool(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int16_to_standard_boolean_return_type(self):
        # Arrange
        integers = [np.int16(i) for i in [1, 1, 0, 0, 1]]

        # Act
        result = [pdtypes.apply.integer_to_boolean(i, return_type=bool)
                  for i in integers]

        # Assert
        expected = [bool(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int32_to_standard_boolean_return_type(self):
        # Arrange
        integers = [np.int32(i) for i in [1, 1, 0, 0, 1]]

        # Act
        result = [pdtypes.apply.integer_to_boolean(i, return_type=bool)
                  for i in integers]

        # Assert
        expected = [bool(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_signed_int64_to_standard_boolean_return_type(self):
        # Arrange
        integers = [np.int64(i) for i in [1, 1, 0, 0, 1]]

        # Act
        result = [pdtypes.apply.integer_to_boolean(i, return_type=bool)
                  for i in integers]

        # Assert
        expected = [bool(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    #######################################
    ####    Numpy Unsigned Integers    ####
    #######################################

    def test_numpy_unsigned_int8_to_standard_boolean_return_type(self):
        # Arrange
        integers = [np.uint8(i) for i in [1, 1, 0, 0, 1]]

        # Act
        result = [pdtypes.apply.integer_to_boolean(i, return_type=bool)
                  for i in integers]

        # Assert
        expected = [bool(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int16_to_standard_boolean_return_type(self):
        # Arrange
        integers = [np.uint16(i) for i in [1, 1, 0, 0, 1]]

        # Act
        result = [pdtypes.apply.integer_to_boolean(i, return_type=bool)
                  for i in integers]

        # Assert
        expected = [bool(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int32_to_standard_boolean_return_type(self):
        # Arrange
        integers = [np.uint32(i) for i in [1, 1, 0, 0, 1]]

        # Act
        result = [pdtypes.apply.integer_to_boolean(i, return_type=bool)
                  for i in integers]

        # Assert
        expected = [bool(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_unsigned_int64_to_standard_boolean_return_type(self):
        # Arrange
        integers = [np.uint64(i) for i in [1, 1, 0, 0, 1]]

        # Act
        result = [pdtypes.apply.integer_to_boolean(i, return_type=bool)
                  for i in integers]

        # Assert
        expected = [bool(i) for i in integers]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])


if __name__ == "__main__":
    unittest.main()
