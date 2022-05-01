import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyFloatToComplexAccuracyTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_float_to_complex_returns_numpy_nan(self):
        # Arrange
        na_vals = [None, np.nan, pd.NA, pd.NaT]

        # Act
        result = [pdtypes.apply.float_to_complex(na) for na in na_vals]

        # Assert
        expected = [np.nan for _ in result]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ##############################
    ####    Generic Floats    ####
    ##############################

    def test_float_to_complex_is_accurate_scalar(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f) for f in floats]

        # Assert
        expected = [complex(f, 0) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_float_to_complex_is_accurate_vector(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_array = np.array(floats)
        float_to_complex = np.vectorize(pdtypes.apply.float_to_complex)

        # Act
        result = float_to_complex(input_array)

        # Assert
        expected = np.array([complex(f, 0) for f in floats])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_to_complex_is_accurate_series(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        input_series = pd.Series(floats)

        # Act
        result = input_series.apply(pdtypes.apply.float_to_complex)

        # Assert
        expected = pd.Series([complex(f, 0) for f in floats])
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToComplexReturnTypeTests(unittest.TestCase):

    ###############################
    ####    Standard Floats    ####
    ##############################

    def test_standard_float_to_standard_complex_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=complex)
                  for f in floats]

        # Assert
        expected = [complex(f, 0) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_float_to_numpy_complex64_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=np.complex64)
                  for f in floats]

        # Assert
        expected = [np.complex64(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_float_to_numpy_complex128_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=np.complex128)
                  for f in floats]

        # Assert
        expected = [np.complex128(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_standard_float_to_custom_complex_return_type(self):
        class CustomComplex(complex):
            pass

        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=CustomComplex)
                  for f in floats]

        # Assert
        expected = [CustomComplex(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ###################################
    ####    Numpy 16-bit Floats    ####
    ###################################

    def test_numpy_float16_to_standard_complex_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float16(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=complex)
                  for f in floats]

        # Assert
        expected = [complex(f, 0) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float16_to_numpy_complex64_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float16(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=np.complex64)
                  for f in floats]

        # Assert
        expected = [np.complex64(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float16_to_numpy_complex128_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float16(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=np.complex128)
                  for f in floats]

        # Assert
        expected = [np.complex128(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float16_to_custom_complex_return_type(self):
        class CustomComplex(complex):
            pass

        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float16(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=CustomComplex)
                  for f in floats]

        # Assert
        expected = [CustomComplex(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ###################################
    ####    Numpy 32-bit Floats    ####
    ###################################

    def test_numpy_float32_to_standard_complex_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float32(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=complex)
                  for f in floats]

        # Assert
        expected = [complex(f, 0) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float32_to_numpy_complex64_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float32(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=np.complex64)
                  for f in floats]

        # Assert
        expected = [np.complex64(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float32_to_numpy_complex128_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float32(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=np.complex128)
                  for f in floats]

        # Assert
        expected = [np.complex128(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float32_to_custom_complex_return_type(self):
        class CustomComplex(complex):
            pass

        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float32(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=CustomComplex)
                  for f in floats]

        # Assert
        expected = [CustomComplex(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    ###################################
    ####    Numpy 64-bit Floats    ####
    ###################################

    def test_numpy_float64_to_standard_complex_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float64(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=complex)
                  for f in floats]

        # Assert
        expected = [complex(f, 0) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float64_to_numpy_complex64_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float64(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=np.complex64)
                  for f in floats]

        # Assert
        expected = [np.complex64(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float64_to_numpy_complex128_return_type(self):
        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float64(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=np.complex128)
                  for f in floats]

        # Assert
        expected = [np.complex128(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])

    def test_numpy_float64_to_custom_complex_return_type(self):
        class CustomComplex(complex):
            pass

        # Arrange
        integers = [-2, -1, 0, 1, 2]
        floats = [np.float64(i + random.random()) for i in integers]

        # Act
        result = [pdtypes.apply.float_to_complex(f, return_type=CustomComplex)
                  for f in floats]

        # Assert
        expected = [CustomComplex(f) for f in floats]
        self.assertEqual(result, expected)
        self.assertEqual([type(r) for r in result], [type(e) for e in expected])


if __name__ == "__main__":
    unittest.main()
