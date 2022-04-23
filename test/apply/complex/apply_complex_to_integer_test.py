import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyComplexToIntegerMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_complex_to_integer_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._complex_to_integer(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_complex_to_integer_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._complex_to_integer)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_complex_to_integer_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._complex_to_integer)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyComplexToIntegerAccuracyTests(unittest.TestCase):

    #################################################################
    ####    Real Whole Complex [-1 + 0j, 0 + 0j, 1 + 0j, ...]    ####
    #################################################################

    def test_real_whole_complex_to_integer_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i, 0) for i in integers]
        for c, i in zip(complexes, integers):
            result = pdtypes.apply._complex_to_integer(c)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_real_whole_complex_to_integer_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i, 0) for i in integers]
        vec = np.vectorize(pdtypes.apply._complex_to_integer)
        result = vec(np.array(complexes))
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_real_whole_complex_to_integer_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i, 0) for i in integers]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_integer)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    ####################################
    ####    Real Decimal Complex    ####
    ####################################

    def test_real_decimal_complex_to_integer_within_ftol_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1e-8, 0) if idx % 2 else complex(i - 1e-8, 0)
                     for idx, i in enumerate(integers)]
        for c, i in zip(complexes, integers):
            result = pdtypes.apply._complex_to_integer(c, ftol=1e-6)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_real_decimal_complex_to_integer_within_ftol_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1e-8, 0) if idx % 2 else complex(i - 1e-8, 0)
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._complex_to_integer)
        result = vec(np.array(complexes), ftol=1e-6)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_real_decimal_complex_to_integer_within_ftol_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1e-8, 0) if idx % 2 else complex(i - 1e-8, 0)
                     for idx, i in enumerate(integers)]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_integer,
                                            ftol=1e-6)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_real_decimal_complex_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1 / 2 * random.random(), 0) if idx % 2
                     else complex(i - 1 / 2 * random.random(), 0)
                     for idx, i in enumerate(integers)]
        err_msg = ("[pdtypes.apply._complex_to_integer] could not convert "
                   "complex to int without losing information: ")
        for c in complexes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._complex_to_integer(c)
            self.assertEqual(str(err.exception), err_msg + repr(c))

    def test_real_decimal_complex_to_integer_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1 / 2 * random.random(), 0) if idx % 2
                     else complex(i - 1 / 2 * random.random(), 0)
                     for idx, i in enumerate(integers)]
        for c, i in zip(complexes, integers):
            result = pdtypes.apply._complex_to_integer(c, force=True)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_real_decimal_complex_to_integer_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1 / 2 * random.random(), 0) if idx % 2
                     else complex(i - 1 / 2 * random.random(), 0)
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._complex_to_integer)
        result = vec(np.array(complexes), force=True)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_real_decimal_complex_to_integer_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1 / 2 * random.random(), 0) if idx % 2
                     else complex(i - 1 / 2 * random.random(), 0)
                     for idx, i in enumerate(integers)]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_integer,
                                            force=True)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_real_decimal_complex_to_integer_forced_not_rounded_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1 / 2 * random.random(), 0) if idx % 2
                     else complex(i - 1 / 2 * random.random(), 0)
                     for idx, i in enumerate(integers)]
        for c in complexes:
            result = pdtypes.apply._complex_to_integer(c, round=False,
                                                       force=True)
            expected = int(c.real)
            self.assertEqual(result, expected)
            self.assertEqual(type(result), type(expected))

    def test_real_decimal_complex_to_integer_forced_not_rounded_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1 / 2 * random.random(), 0) if idx % 2
                     else complex(i - 1 / 2 * random.random(), 0)
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._complex_to_integer)
        result = vec(np.array(complexes), round=False, force=True)
        expected = np.array([int(c.real) for c in complexes])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_real_decimal_complex_to_integer_forced_not_rounded_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1 / 2 * random.random(), 0) if idx % 2
                     else complex(i - 1 / 2 * random.random(), 0)
                     for idx, i in enumerate(integers)]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_integer,
                                            round=False, force=True)
        expected = pd.Series([int(c.real) for c in complexes])
        pd.testing.assert_series_equal(result, expected)

    #################################
    ####    Imaginary Complex    ####
    #################################

    def test_imaginary_complex_to_integer_within_ftol_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1e-8, 1e-8) if idx % 2
                     else complex(i - 1e-8, -1e-8)
                     for idx, i in enumerate(integers)]
        for c, i in zip(complexes, integers):
            result = pdtypes.apply._complex_to_integer(c, ftol=1e-6)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_imaginary_complex_to_integer_within_ftol_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1e-8, 1e-8) if idx % 2
                     else complex(i - 1e-8, -1e-8)
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._complex_to_integer)
        result = vec(np.array(complexes), ftol=1e-6)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_to_integer_within_ftol_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1e-8, 1e-8) if idx % 2
                     else complex(i - 1e-8, -1e-8)
                     for idx, i in enumerate(integers)]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_integer,
                                            ftol=1e-6)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_imaginary_complex_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random() / 2, random.random() / 2)
                     if idx % 2 else
                     complex(i - random.random() / 2, -1 * random.random() / 2)
                     for idx, i in enumerate(integers)]
        err_msg = ("[pdtypes.apply._complex_to_integer] could not convert "
                   "complex to int without losing information: ")
        for c in complexes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._complex_to_integer(c)
            self.assertEqual(str(err.exception), err_msg + repr(c))

    def test_imaginary_complex_to_integer_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random() / 2, random.random() / 2)
                     if idx % 2 else
                     complex(i - random.random() / 2, -1 * random.random() / 2)
                     for idx, i in enumerate(integers)]
        for c, i in zip(complexes, integers):
            result = pdtypes.apply._complex_to_integer(c, force=True)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_imaginary_complex_to_integer_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random() / 2, random.random() / 2)
                     if idx % 2 else
                     complex(i - random.random() / 2, -1 * random.random() / 2)
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._complex_to_integer)
        result = vec(np.array(complexes), force=True)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_to_integer_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random() / 2, random.random() / 2)
                     if idx % 2 else
                     complex(i - random.random() / 2, -1 * random.random() / 2)
                     for idx, i in enumerate(integers)]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_integer,
                                            force=True)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_imaginary_complex_to_integer_forced_not_rounded_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random() / 2, random.random() / 2)
                     if idx % 2 else
                     complex(i - random.random() / 2, -1 * random.random() / 2)
                     for idx, i in enumerate(integers)]
        for c in complexes:
            result = pdtypes.apply._complex_to_integer(c, round=False,
                                                       force=True)
            expected = int(c.real)
            self.assertEqual(result, expected)
            self.assertEqual(type(result), type(expected))

    def test_imaginary_complex_to_integer_forced_not_rounded_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random() / 2, random.random() / 2)
                     if idx % 2 else
                     complex(i - random.random() / 2, -1 * random.random() / 2)
                     for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._complex_to_integer)
        result = vec(np.array(complexes), round=False, force=True)
        expected = np.array([int(c.real) for c in complexes])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_to_integer_forced_not_rounded_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random() / 2, random.random() / 2)
                     if idx % 2 else
                     complex(i - random.random() / 2, -1 * random.random() / 2)
                     for idx, i in enumerate(integers)]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_integer,
                                            round=False, force=True)
        expected = pd.Series([int(c.real) for c in complexes])
        pd.testing.assert_series_equal(result, expected)


class ApplyComplexToIntegerReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard return types    ####
    #########################################

    def test_standard_complex_to_numpy_signed_integer_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        complexes = [complex(i, 0) for i in integers]
        for c, i in zip(complexes, integers):
            result = pdtypes.apply._complex_to_integer(c, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_standard_complex_to_numpy_unsigned_integer_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        complexes = [complex(i, 0) for i in integers]
        for c, i in zip(complexes, integers):
            result = pdtypes.apply._complex_to_integer(c, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_numpy_complex_to_standard_integer_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                  for idx, i in enumerate(integers)]
        for c, i in zip(complexes, integers):
            result = pdtypes.apply._complex_to_integer(c, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_numpy_complex_to_numpy_signed_integer_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                  for idx, i in enumerate(integers)]
        for c, i in zip(complexes, integers):
            result = pdtypes.apply._complex_to_integer(c, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_numpy_complex_to_numpy_unsigned_integer_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                  for idx, i in enumerate(integers)]
        for c, i in zip(complexes, integers):
            result = pdtypes.apply._complex_to_integer(c, return_type=type(i))
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))


if __name__ == "__main__":
    unittest.main()
