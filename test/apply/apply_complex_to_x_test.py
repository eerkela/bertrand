from __future__ import annotations
from datetime import datetime, timedelta, timezone
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyComplexToIntegerTests(unittest.TestCase):

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


class ApplyComplexToStringTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_complex_to_string_scalar(self):
        na_val = None
        expected = pd.NA
        result = pdtypes.apply._complex_to_string(na_val)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_complex_to_string_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        vec = np.vectorize(pdtypes.apply._complex_to_string)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NA) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_complex_to_string_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        result = pd.Series(nones).apply(pdtypes.apply._complex_to_string)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    #######################################
    ####    Generic Complex Numbers    ####
    #######################################

    def test_complex_to_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), i + random.random())
                     for i in integers]
        strings = [str(c) for c in complexes]
        for c, s in zip(complexes, strings):
            result = pdtypes.apply._complex_to_string(c)
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_complex_to_string_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), i + random.random())
                     for i in integers]
        strings = [str(c) for c in complexes]
        vec = np.vectorize(pdtypes.apply._complex_to_string)
        result = vec(np.array(complexes))
        expected = np.array(strings)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_complex_to_string_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), i + random.random())
                     for i in integers]
        strings = [str(c) for c in complexes]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_string)
        expected = pd.Series(strings)
        pd.testing.assert_series_equal(result, expected)

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_numpy_complex_type_to_string_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)]
                     (complex(i + random.random(), i + random.random()))
                     for idx, i in enumerate(integers)]
        strings = [str(c) for c in complexes]
        for c, s in zip(complexes, strings):
            result = pdtypes.apply._complex_to_string(c, return_type=type(s))
            self.assertEqual(result, s)
            self.assertEqual(type(result), type(s))

    def test_complex_to_custom_string_class_scalar(self):
        class CustomString:
            def __init__(self, c: complex):
                self.string = str(c)

            def __str__(self) -> str:
                return self.string

        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), i + random.random())
                     for i in integers]
        for c in complexes:
            result = pdtypes.apply._complex_to_string(c, return_type=CustomString)
            self.assertEqual(type(result), CustomString)


class ApplyComplexToBooleanTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_complex_to_boolean_scalar(self):
        na_val = None
        expected = pd.NA
        result = pdtypes.apply._complex_to_boolean(na_val)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_complex_to_boolean_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        vec = np.vectorize(pdtypes.apply._complex_to_boolean)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NA) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_complex_to_boolean_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NA, pd.NA, pd.NA, pd.NA]
        result = pd.Series(nones).apply(pdtypes.apply._complex_to_boolean)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ###################################################################
    ####    Complex Boolean Flags [1+0j, 0+0j, 1+0j, 0+0j, ...]    ####
    ###################################################################

    def test_complex_bool_flag_to_boolean_scalar(self):
        integers = [1, 0, 1, 0, 1]
        complexes = [complex(i, 0) for i in integers]
        booleans = [bool(i) for i in integers]
        for c, b in zip(complexes, booleans):
            result = pdtypes.apply._complex_to_boolean(b)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_complex_bool_flag_to_boolean_vector(self):
        integers = [1, 0, 1, 0, 1]
        complexes = [complex(i, 0) for i in integers]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._complex_to_boolean)
        result = vec(np.array(complexes))
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_complex_bool_flag_to_boolean_series(self):
        integers = [1, 0, 1, 0, 1]
        complexes = [complex(i, 0) for i in integers]
        booleans = [bool(i) for i in integers]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_boolean)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    ############################
    ####    Real Complex    ####
    ############################

    def test_real_complex_to_boolean_within_ftol_scalar(self):
        integers = [1, 1, 0, 0, 1]
        complexes = [complex(i + 1e-8, 0) if idx % 2 else complex(i - 1e-8, 0)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        for c, b in zip(complexes, booleans):
            result = pdtypes.apply._complex_to_boolean(c, ftol=1e-6)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_real_complex_to_boolean_within_ftol_vector(self):
        integers = [1, 1, 0, 0, 1]
        complexes = [complex(i + 1e-8, 0) if idx % 2 else complex(i - 1e-8, 0)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._complex_to_boolean)
        result = vec(np.array(complexes), ftol=1e-6)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_real_complex_to_boolean_within_ftol_series(self):
        integers = [1, 1, 0, 0, 1]
        complexes = [complex(i + 1e-8, 0) if idx % 2 else complex(i - 1e-8, 0)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_boolean,
                                            ftol=1e-6)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    def test_real_complex_to_boolean_out_of_range_error(self):
        integers = [-3, -2, -1, 2, 3, 4]
        complexes = [complex(i, 0) for i in integers]
        err_msg = ("[pdtypes.apply._complex_to_boolean] could not convert "
                   "complex to bool without losing information: ")
        for c in complexes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._complex_to_boolean(c)
            self.assertEqual(str(err.exception), err_msg + repr(c))

    def test_real_complex_to_boolean_out_of_range_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i, 0) for i in integers]
        booleans = [bool(i) for i in integers]
        for c, b in zip(complexes, booleans):
            result = pdtypes.apply._complex_to_boolean(c, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_real_complex_to_boolean_out_of_range_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i, 0) for i in integers]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._complex_to_boolean)
        result = vec(np.array(complexes), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_real_complex_to_boolean_out_of_range_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i, 0) for i in integers]
        booleans = [bool(i) for i in integers]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_boolean,
                                            force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    def test_real_complex_to_boolean_decimals_between_0_and_1_error(self):
        complexes = [complex(random.random(), 0) for _ in range(5)]
        err_msg = ("[pdtypes.apply._complex_to_boolean] could not convert "
                   "complex to bool without losing information: ")
        for c in complexes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._complex_to_boolean(c)
            self.assertEqual(str(err.exception), err_msg + repr(c))

    def test_real_complex_to_boolean_decimals_between_0_and_1_forced_scalar(self):
        complexes = [complex(random.random(), 0) for _ in range(5)]
        booleans = [True for _ in range(5)]
        for c, b in zip(complexes, booleans):
            result = pdtypes.apply._complex_to_boolean(c, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_real_complex_to_boolean_decimals_between_0_and_1_forced_vector(self):
        complexes = [complex(random.random(), 0) for _ in range(5)]
        booleans = [True for _ in range(5)]
        vec = np.vectorize(pdtypes.apply._complex_to_boolean)
        result = vec(np.array(complexes), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_real_complex_to_boolean_decimals_between_0_and_1_forced_series(self):
        complexes = [complex(random.random(), 0) for _ in range(5)]
        booleans = [True for _ in range(5)]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_boolean,
                                            force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    #################################
    ####    Imaginary Complex    ####
    #################################

    def test_imaginary_complex_to_boolean_within_ftol_scalar(self):
        integers = [1, 1, 0, 0, 1]
        complexes = [complex(i + 1e-8, 1e-8) if idx % 2
                     else complex(i - 1e-8, -1e-8)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        for c, b in zip(complexes, booleans):
            result = pdtypes.apply._complex_to_boolean(c, ftol=1e-6)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_imaginary_complex_to_boolean_within_ftol_vector(self):
        integers = [1, 1, 0, 0, 1]
        complexes = [complex(i + 1e-8, 1e-8) if idx % 2
                     else complex(i - 1e-8, -1e-8)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._complex_to_boolean)
        result = vec(np.array(complexes), ftol=1e-6)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_to_boolean_within_ftol_series(self):
        integers = [1, 1, 0, 0, 1]
        complexes = [complex(i + 1e-8, 1e-8) if idx % 2
                     else complex(i - 1e-8, -1e-8)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_boolean,
                                            ftol=1e-6)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    def test_imaginary_complex_to_boolean_out_of_range_error(self):
        integers = [-3, -2, -1, 2, 3, 4]
        complexes = [complex(i, 1) for i in integers]
        err_msg = ("[pdtypes.apply._complex_to_boolean] could not convert "
                   "complex to bool without losing information: ")
        for c in complexes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._complex_to_boolean(c)
            self.assertEqual(str(err.exception), err_msg + repr(c))

    def test_imaginary_complex_to_boolean_out_of_range_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i, 1) for i in integers]
        booleans = [bool(i) for i in integers]
        for c, b in zip(complexes, booleans):
            result = pdtypes.apply._complex_to_boolean(c, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_imaginary_complex_to_boolean_out_of_range_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i, 1) for i in integers]
        booleans = [bool(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._complex_to_boolean)
        result = vec(np.array(complexes), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_to_boolean_out_of_range_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i, 1) for i in integers]
        booleans = [bool(i) for i in integers]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_boolean,
                                            force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    def test_imaginary_complex_to_boolean_decimals_between_0_and_1_error(self):
        complexes = [complex(random.random(), 1) for _ in range(5)]
        err_msg = ("[pdtypes.apply._complex_to_boolean] could not convert "
                   "complex to bool without losing information: ")
        for c in complexes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._complex_to_boolean(c)
            self.assertEqual(str(err.exception), err_msg + repr(c))

    def test_imaginary_complex_to_boolean_decimals_between_0_and_1_forced_scalar(self):
        complexes = [complex(random.random(), 1) for _ in range(5)]
        booleans = [True for _ in range(5)]
        for c, b in zip(complexes, booleans):
            result = pdtypes.apply._complex_to_boolean(c, force=True)
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))

    def test_imaginary_complex_to_boolean_decimals_between_0_and_1_forced_vector(self):
        complexes = [complex(random.random(), 1) for _ in range(5)]
        booleans = [True for _ in range(5)]
        vec = np.vectorize(pdtypes.apply._complex_to_boolean)
        result = vec(np.array(complexes), force=True)
        expected = np.array(booleans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_to_boolean_decimals_between_0_and_1_forced_series(self):
        complexes = [complex(random.random(), 1) for _ in range(5)]
        booleans = [True for _ in range(5)]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_boolean,
                                            force=True)
        expected = pd.Series(booleans)
        pd.testing.assert_series_equal(result, expected)

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_complex_to_custom_boolean_class_scalar(self):
        class CustomBoolean:
            def __init__(self, c: complex):
                self.boolean = bool(c.real)

            def __bool__(self) -> bool:
                return self.boolean

            def __sub__(self, other) -> int:
                return self.boolean - other

        integers = [1, 1, 0, 0, 1]
        complexes = [complex(i, 0) for i in integers]
        for c in complexes:
            result = pdtypes.apply._complex_to_boolean(c, return_type=CustomBoolean)
            self.assertEqual(type(result), CustomBoolean)

    def test_numpy_complex_type_to_boolean_scalar(self):
        integers = [1, 0, 1, 0, 1]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                     for idx, i in enumerate(integers)]
        booleans = [bool(i) for i in integers]
        for c, b in zip(complexes, booleans):
            result = pdtypes.apply._complex_to_boolean(c, return_type=type(b))
            self.assertEqual(result, b)
            self.assertEqual(type(result), type(b))


class ApplyComplexToDatetimeTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_complex_to_datetime_scalar(self):
        na_val = None
        expected = pd.NaT
        result = pdtypes.apply._complex_to_datetime(na_val)
        # numpy can't parse pd.NaT, and pd.NaT != pd.NaT, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_complex_to_datetime_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        vec = np.vectorize(pdtypes.apply._complex_to_datetime)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NaT) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_complex_to_datetime_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        result = pd.Series(nones).apply(pdtypes.apply._complex_to_datetime)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ############################
    ####    Real Complex    ####
    ############################

    def test_real_complex_to_datetime_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 0) for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(c.real, "UTC")
                     for c in complexes]
        for c, d in zip(complexes, datetimes):
            result = pdtypes.apply._complex_to_datetime(c)
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_real_complex_to_datetime_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 0) for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(c.real, "UTC")
                     for c in complexes]
        vec = np.vectorize(pdtypes.apply._complex_to_datetime)
        result = vec(np.array(complexes))
        expected = np.array(datetimes)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_real_complex_to_datetime_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 0) for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(c.real, "UTC")
                     for c in complexes]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_datetime)
        expected = pd.Series(datetimes)
        pd.testing.assert_series_equal(result, expected)
 
    #################################
    ####    Imaginary Complex    ####
    #################################

    def test_imaginary_complex_to_datetime_within_ftol_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1e-8) if idx % 2
                     else complex(i + random.random(), -1e-8)
                     for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(c.real, "UTC")
                     for c in complexes]
        for c, d in zip(complexes, datetimes):
            result = pdtypes.apply._complex_to_datetime(c, ftol=1e-6)
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_imaginary_complex_to_datetime_within_ftol_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1e-8) if idx % 2
                     else complex(i + random.random(), -1e-8)
                     for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(c.real, "UTC")
                     for c in complexes]
        vec = np.vectorize(pdtypes.apply._complex_to_datetime)
        result = vec(np.array(complexes), ftol=1e-6)
        expected = np.array(datetimes)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_to_datetime_within_ftol_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1e-8) if idx % 2
                     else complex(i + random.random(), -1e-8)
                     for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(c.real, "UTC")
                     for c in complexes]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_datetime,
                                            ftol=1e-6)
        expected = pd.Series(datetimes)
        pd.testing.assert_series_equal(result, expected)

    def test_imaginary_complex_to_datetime_error(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1) for i in integers]
        err_msg = ("[pdtypes.apply._complex_to_datetime] could not convert "
                   "complex to datetime without losing information: ")
        for c in complexes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._complex_to_datetime(c)
            self.assertEqual(str(err.exception), err_msg + repr(c))

    def test_imaginary_complex_to_datetime_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1) for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(c.real, "UTC")
                     for c in complexes]
        for c, d in zip(complexes, datetimes):
            result = pdtypes.apply._complex_to_datetime(c, force=True)
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_imaginary_complex_to_datetime_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1) for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(c.real, "UTC")
                     for c in complexes]
        vec = np.vectorize(pdtypes.apply._complex_to_datetime)
        result = vec(np.array(complexes), force=True)
        expected = np.array(datetimes)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_to_datetime_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1) for i in integers]
        datetimes = [pd.Timestamp.fromtimestamp(c.real, "UTC")
                     for c in complexes]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_datetime,
                                            force=True)
        expected = pd.Series(datetimes)
        pd.testing.assert_series_equal(result, expected)

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_complex_to_standard_datetime_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 0) for i in integers]
        datetimes = [datetime.fromtimestamp(c.real, timezone.utc)
                     for c in complexes]
        for c, d in zip(complexes, datetimes):
            result = pdtypes.apply._complex_to_datetime(c, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_standard_complex_to_custom_datetime_class_scalar(self):
        class CustomDatetime:
            def __init__(self, c: complex, tz):
                self.timestamp = pd.Timestamp.fromtimestamp(c.real, tz)

            @classmethod
            def fromtimestamp(cls, c: complex, tz) -> CustomDatetime:
                return cls(c, tz)

            def to_datetime(self) -> pd.Timestamp:
                return self.timestamp

        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 0) for i in integers]
        for c in complexes:
            result = pdtypes.apply._complex_to_datetime(c, return_type=CustomDatetime)
            self.assertEqual(type(result), CustomDatetime)

    def test_numpy_complex_to_pandas_timestamp_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                     for idx, i in enumerate(integers)]
        datetimes = [pd.Timestamp.fromtimestamp(float(c.real), "UTC")
                     for c in complexes]
        for c, d in zip(complexes, datetimes):
            result = pdtypes.apply._complex_to_datetime(c, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))

    def test_numpy_complex_to_standard_datetime_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                     for idx, i in enumerate(integers)]
        datetimes = [datetime.fromtimestamp(float(c.real), timezone.utc)
                     for c in complexes]
        for c, d in zip(complexes, datetimes):
            result = pdtypes.apply._complex_to_datetime(c, return_type=type(d))
            self.assertEqual(result, d)
            self.assertEqual(type(result), type(d))


class ApplyComplexToTimedeltaTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_complex_to_timedelta_scalar(self):
        na_val = None
        expected = pd.NaT
        result = pdtypes.apply._complex_to_timedelta(na_val)
        # numpy can't parse pd.NaT, and pd.NaT != pd.NaT, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_complex_to_timedelta_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        vec = np.vectorize(pdtypes.apply._complex_to_timedelta)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NaT) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_complex_to_timedelta_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        result = pd.Series(nones).apply(pdtypes.apply._complex_to_timedelta)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ############################
    ####    Real Complex    ####
    ############################

    def test_real_complex_to_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 0) for i in integers]
        timedeltas = [pd.Timedelta(seconds=c.real) for c in complexes]
        for c, t in zip(complexes, timedeltas):
            result = pdtypes.apply._complex_to_timedelta(c)
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_real_complex_to_timedelta_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 0) for i in integers]
        timedeltas = [pd.Timedelta(seconds=c.real) for c in complexes]
        vec = np.vectorize(pdtypes.apply._complex_to_timedelta)
        result = vec(np.array(complexes))
        expected = np.array(timedeltas)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_real_complex_to_timedelta_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 0) for i in integers]
        timedeltas = [pd.Timedelta(seconds=c.real) for c in complexes]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_timedelta)
        expected = pd.Series(timedeltas)
        pd.testing.assert_series_equal(result, expected)
 
    #################################
    ####    Imaginary Complex    ####
    #################################

    def test_imaginary_complex_to_timedelta_within_ftol_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1e-8) if idx % 2
                     else complex(i + random.random(), -1e-8)
                     for idx, i in enumerate(integers)]
        timedeltas = [pd.Timedelta(seconds=c.real) for c in complexes]
        for c, t in zip(complexes, timedeltas):
            result = pdtypes.apply._complex_to_timedelta(c, ftol=1e-6)
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_imaginary_complex_to_timedelta_within_ftol_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1e-8) if idx % 2
                     else complex(i + random.random(), -1e-8)
                     for idx, i in enumerate(integers)]
        timedeltas = [pd.Timedelta(seconds=c.real) for c in complexes]
        vec = np.vectorize(pdtypes.apply._complex_to_timedelta)
        result = vec(np.array(complexes), ftol=1e-6)
        expected = np.array(timedeltas)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_to_timedelta_within_ftol_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1e-8) if idx % 2
                     else complex(i + random.random(), -1e-8)
                     for idx, i in enumerate(integers)]
        timedeltas = [pd.Timedelta(seconds=c.real) for c in complexes]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_timedelta,
                                            ftol=1e-6)
        expected = pd.Series(timedeltas)
        pd.testing.assert_series_equal(result, expected)

    def test_imaginary_complex_to_timedelta_error(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1) for i in integers]
        err_msg = ("[pdtypes.apply._complex_to_timedelta] could not convert "
                   "complex to timedelta without losing information: ")
        for c in complexes:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._complex_to_timedelta(c)
            self.assertEqual(str(err.exception), err_msg + repr(c))

    def test_imaginary_complex_to_timedelta_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1) for i in integers]
        timedeltas = [pd.Timedelta(seconds=c.real) for c in complexes]
        for c, t in zip(complexes, timedeltas):
            result = pdtypes.apply._complex_to_timedelta(c, force=True)
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_imaginary_complex_to_timedelta_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1) for i in integers]
        timedeltas = [pd.Timedelta(seconds=c.real) for c in complexes]
        vec = np.vectorize(pdtypes.apply._complex_to_timedelta)
        result = vec(np.array(complexes), force=True)
        expected = np.array(timedeltas)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_to_timedelta_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 1) for i in integers]
        timedeltas = [pd.Timedelta(seconds=c.real) for c in complexes]
        result = pd.Series(complexes).apply(pdtypes.apply._complex_to_timedelta,
                                            force=True)
        expected = pd.Series(timedeltas)
        pd.testing.assert_series_equal(result, expected)

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_complex_to_standard_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 0) for i in integers]
        timedeltas = [timedelta(seconds=c.real) for c in complexes]
        for c, t in zip(complexes, timedeltas):
            result = pdtypes.apply._complex_to_timedelta(c, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_standard_complex_to_custom_timedelta_class_scalar(self):
        class CustomTimedelta:
            def __init__(self, seconds: float):
                self.delta = pd.Timedelta(seconds=seconds)

            def to_timedelta(self) -> pd.Timedelta:
                return self.delta

        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random(), 0) for i in integers]
        for c in complexes:
            result = pdtypes.apply._complex_to_timedelta(c, return_type=CustomTimedelta)
            self.assertEqual(type(result), CustomTimedelta)

    def test_numpy_complex_to_pandas_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                     for idx, i in enumerate(integers)]
        timedeltas = [pd.Timedelta(seconds=c.real) for c in complexes]
        for c, t in zip(complexes, timedeltas):
            result = pdtypes.apply._complex_to_timedelta(c, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_numpy_complex_to_standard_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                     for idx, i in enumerate(integers)]
        timedeltas = [timedelta(seconds=float(c.real)) for c in complexes]
        for c, t in zip(complexes, timedeltas):
            result = pdtypes.apply._complex_to_timedelta(c, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))


if __name__ == "__main__":
    unittest.main()
