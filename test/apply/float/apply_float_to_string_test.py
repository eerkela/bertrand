import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyFloatToStringAccuracyTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_float_to_string_returns_none(self):
        # Arrange
        na_vals = [None, np.nan, pd.NA, pd.NaT]

        # Act
        result = [pdtypes.apply.float_to_string(na) for na in na_vals]

        # Assert
        expected = [None for _ in result]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ##############################
    ####    Generic Floats    ####
    ##############################

    def test_float_to_string_is_accurate_scalar(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]

        # Act
        result = [pdtypes.apply.float_to_string(f) for f in floats]

        # Assert
        expected = [str(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_float_to_string_is_accurate_vector(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_array = np.array(floats)
        float_to_str = np.vectorize(pdtypes.apply.float_to_string)
        
        # Act
        result = float_to_str(input_array)
        
        # Assert
        expected = np.array([str(f) for f in floats])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_to_string_is_accurate_series(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = input_series.apply(pdtypes.apply.float_to_string)

        # Assert
        expected = pd.Series([str(f) for f in floats])
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToStringReturnTypeTests(unittest.TestCase):

    ###############################
    ####    Standard Floats    ####
    ###############################

    def test_standard_float_to_standard_string_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]

        # Act
        result = [pdtypes.apply.float_to_string(f, return_type=str)
                  for f in floats]

        # Assert
        expected = [str(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_float_to_custom_string_return_type(self):
        class CustomString(str):
            pass
        
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]

        # Act
        result = [pdtypes.apply.float_to_string(f, return_type=CustomString)
                  for f in floats]

        # Assert
        expected = [CustomString(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ###################################
    ####    Numpy 16-bit Floats    ####
    ###################################

    def test_numpy_float16_to_standard_string_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float16(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_string(f, return_type=str)
                  for f in floats]

        # Assert
        expected = [str(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float16_to_custom_string_return_type(self):
        class CustomString(str):
            pass
        
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float16(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_string(f, return_type=CustomString)
                  for f in floats]

        # Assert
        expected = [CustomString(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ###################################
    ####    Numpy 32-bit Floats    ####
    ###################################

    def test_numpy_float32_to_standard_string_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float32(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_string(f, return_type=str)
                  for f in floats]

        # Assert
        expected = [str(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float32_to_custom_string_return_type(self):
        class CustomString(str):
            pass
        
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float32(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_string(f, return_type=CustomString)
                  for f in floats]

        # Assert
        expected = [CustomString(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ###################################
    ####    Numpy 64-bit Floats    ####
    ###################################

    def test_numpy_float64_to_standard_string_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float64(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_string(f, return_type=str)
                  for f in floats]

        # Assert
        expected = [str(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float64_to_custom_string_return_type(self):
        class CustomString(str):
            pass
        
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float64(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_string(f, return_type=CustomString)
                  for f in floats]

        # Assert
        expected = [CustomString(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])


if __name__ == "__main__":
    unittest.main()
