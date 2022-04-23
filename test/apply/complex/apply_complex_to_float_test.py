import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyComplexToFloatTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_complex_to_float_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._complex_to_float(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_complex_to_float_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._complex_to_float)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_complex_to_float_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._complex_to_float)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyComplexToFloatAccuracyTests(unittest.TestCase):

    ############################
    ####    Real Complex    ####
    ############################

    def test_real_complex_to_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        complexes = [complex(f, 0) for f in floats]
        for c, f in zip(complexes, floats):
            result = pdtypes.apply._complex_to_float(c)
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))

    def test_real_complex_to_float_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        complexes = [complex(f, 0) for f in floats]
        vec = np.vectorize(pdtypes.apply._complex_to_float)
        result = vec(np.array(complexes))
        expected = np.array(floats)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_real_complex_to_float_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        complexes = [complex(f, 0) for f in floats]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_float)
        expected = pd.Series(floats)
        pd.testing.assert_series_equal(result, expected)

    #################################
    ####    Imaginary Complex    ####
    #################################

    def test_imaginary_complex_to_float_within_ftol_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1e-8) if idx % 2
                     else complex(i + random.random(), -1e-8)
                     for idx, i in enumerate(integers)]
        floats = [c.real for c in complexes]
        for c, f in zip(complexes, floats):
            result = pdtypes.apply._complex_to_float(c, ftol=1e-6)
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))

    def test_imaginary_complex_to_float_within_ftol_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1e-8) if idx % 2
                     else complex(i + random.random(), -1e-8)
                     for idx, i in enumerate(integers)]
        floats = [c.real for c in complexes]
        vec = np.vectorize(pdtypes.apply._complex_to_float)
        result = vec(np.array(complexes), ftol=1e-6)
        expected = np.array(floats)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_to_float_within_ftol_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1e-8) if idx % 2
                     else complex(i + random.random(), -1e-8)
                     for idx, i in enumerate(integers)]
        floats = [c.real for c in complexes]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_float,
                                            ftol=1e-6)
        expected = pd.Series(floats)
        pd.testing.assert_series_equal(result, expected)

    def test_imaginary_complex_to_float_error(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1) if idx % 2
                     else complex(i + random.random(), -1)
                     for idx, i in enumerate(integers)]
        err_msg = ("[pdtypes.apply._complex_to_float] could not convert "
                   "complex to float without losing information: ")
        for c in complexes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._complex_to_float(c)
            self.assertEqual(str(err.exception), err_msg + repr(c))

    def test_imaginary_complex_to_float_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random() / 2, 1) if idx % 2
                     else complex(i + random.random() / 2, -1)
                     for idx, i in enumerate(integers)]
        floats = [c.real for c in complexes]
        for c, f in zip(complexes, floats):
            result = pdtypes.apply._complex_to_float(c, force=True)
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))

    def test_imaginary_complex_to_float_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random() / 2, 1) if idx % 2
                     else complex(i + random.random() / 2, -1)
                     for idx, i in enumerate(integers)]
        floats = [c.real for c in complexes]
        vec = np.vectorize(pdtypes.apply._complex_to_float)
        result = vec(np.array(complexes), force=True)
        expected = np.array(floats)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_to_float_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random() / 2, 1) if idx % 2
                     else complex(i + random.random() / 2, -1)
                     for idx, i in enumerate(integers)]
        floats = [c.real for c in complexes]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_float,
                                            force=True)
        expected = pd.Series(floats)
        pd.testing.assert_series_equal(result, expected)


class ApplyComplexToFloatReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_complex_to_numpy_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i, 0) for i in integers]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        for i, f in zip(complexes, floats):
            result = pdtypes.apply._complex_to_float(i, return_type=type(f))
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))

    def test_numpy_complex_to_standard_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                     for idx, i in enumerate(integers)]
        floats = [float(i) for i in integers]
        for c, f in zip(complexes, floats):
            result = pdtypes.apply._complex_to_float(c, return_type=type(f))
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))

    def test_numpy_complex_to_numpy_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                     for idx, i in enumerate(integers)]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        for c, f in zip(complexes, floats):
            result = pdtypes.apply._complex_to_float(c, return_type=type(f))
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))


if __name__ == "__main__":
    unittest.main()
