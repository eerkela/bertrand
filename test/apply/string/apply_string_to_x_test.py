from __future__ import annotations
from datetime import datetime, timedelta, timezone
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyStringToIntegerTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_string_to_integer_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._string_to_integer(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_string_to_integer_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._string_to_integer)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_string_to_integer_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._string_to_integer)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)

    ###############################
    ####    Integer Strings    ####
    ###############################

    def test_integer_string_to_integer_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i) for i in integers]
        for s, i in zip(strings, integers):
            result = pdtypes.apply._string_to_integer(s)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_integer_string_to_integer_vector(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i) for i in integers]
        vec = np.vectorize(pdtypes.apply._string_to_integer)
        result = vec(np.array(strings))
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_integer_string_to_integer_series(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i) for i in integers]
        result = pd.Series(strings).apply(pdtypes.apply._string_to_integer)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    #############################
    ####    Float Strings    ####
    #############################

    def test_float_string_to_integer_within_ftol_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i + 1e-8) if idx % 2 else str(i - 1e-8)
                  for idx, i in enumerate(integers)]
        for s, i in zip(strings, integers):
            result = pdtypes.apply._string_to_integer(s, ftol=1e-6)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_float_string_to_integer_within_ftol_vector(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i + 1e-8) if idx % 2 else str(i - 1e-8)
                  for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._string_to_integer)
        result = vec(np.array(strings), ftol=1e-6)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_string_to_integer_within_ftol_series(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i + 1e-8) if idx % 2 else str(i - 1e-8)
                  for idx, i in enumerate(integers)]
        result = pd.Series(strings).apply(pdtypes.apply._string_to_integer,
                                          ftol=1e-6)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_float_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i + random.random()) for i in integers]
        err_msg = ("[pdtypes.apply._string_to_integer] could not convert str "
                   "to int without losing information: ")
        for s in strings:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._string_to_integer(s)
            self.assertEqual(str(err.exception), err_msg + repr(s))

    def test_float_string_to_integer_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i + random.random() / 2) if idx % 2
                  else str(i - random.random() / 2)
                  for idx, i in enumerate(integers)]
        for s, i in zip(strings, integers):
            result = pdtypes.apply._string_to_integer(s, force=True)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_float_string_to_integer_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i + random.random() / 2) if idx % 2
                  else str(i - random.random() / 2)
                  for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._string_to_integer)
        result = vec(np.array(strings), force=True)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_string_to_integer_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(i + random.random() / 2) if idx % 2
                  else str(i - random.random() / 2)
                  for idx, i in enumerate(integers)]
        result = pd.Series(strings).apply(pdtypes.apply._string_to_integer,
                                          force=True)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_float_string_to_integer_forced_not_rounded(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        strings = [str(f) for f in floats]
        for s, f in zip(strings, floats):
            result = pdtypes.apply._string_to_integer(s, force=True,
                                                      round=False)
            expected = int(f)
            self.assertEqual(result, expected)
            self.assertEqual(type(result), type(expected))

    def test_float_string_to_integer_forced_not_rounded_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        strings = [str(f) for f in floats]
        vec = np.vectorize(pdtypes.apply._string_to_integer)
        result = vec(np.array(strings), force=True, round=False)
        expected = np.array([int(f) for f in floats])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_string_to_integer_forced_not_rounded_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() / 2 if idx % 2
                  else i - random.random() / 2
                  for idx, i in enumerate(integers)]
        strings = [str(f) for f in floats]
        result = pd.Series(strings).apply(pdtypes.apply._string_to_integer,
                                          force=True, round=False)
        expected = pd.Series([int(f) for f in floats])
        pd.testing.assert_series_equal(result, expected)

    ##########################################
    ####    Real Whole Complex Strings    ####
    ##########################################

    def test_real_whole_complex_string_to_integer_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i, 0)) for i in integers]
        for s, i in zip(strings, integers):
            result = pdtypes.apply._string_to_integer(s)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_real_whole_complex_string_to_integer_vector(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i, 0)) for i in integers]
        vec = np.vectorize(pdtypes.apply._string_to_integer)
        result = vec(np.array(strings))
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_real_whole_complex_to_integer_series(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i, 0)) for i in integers]
        result = pd.Series(strings).apply(pdtypes.apply._string_to_integer)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    ############################################
    ####    Real Decimal Complex Strings    ####
    ############################################

    def test_real_decimal_complex_string_to_integer_within_ftol_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i + 1e-8, 0)) if idx % 2
                   else str(complex(i - 1e-8, 0))
                   for idx, i in enumerate(integers)]
        for s, i in zip(strings, integers):
            result = pdtypes.apply._string_to_integer(s, ftol=1e-6)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_real_decimal_complex_string_to_integer_within_ftol_vector(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i + 1e-8, 0)) if idx % 2
                   else str(complex(i - 1e-8, 0))
                   for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._string_to_integer)
        result = vec(np.array(strings), ftol=1e-6)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_real_decimal_complex_string_to_integer_within_ftol_series(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i + 1e-8, 0)) if idx % 2
                   else str(complex(i - 1e-8, 0))
                   for idx, i in enumerate(integers)]
        result = pd.Series(strings).apply(pdtypes.apply._string_to_integer,
                                          ftol=1e-6)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_real_decimal_complex_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i + 1 / 2 * random.random(), 0)) if idx % 2
                   else str(complex(i - 1 / 2 * random.random(), 0))
                   for idx, i in enumerate(integers)]
        err_msg = ("[pdtypes.apply._string_to_integer] could not convert "
                   "str to int without losing information: ")
        for s in strings:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._string_to_integer(s)
            self.assertEqual(str(err.exception), err_msg + repr(s))

    def test_real_decimal_complex_string_to_integer_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i + 1 / 2 * random.random(), 0)) if idx % 2
                   else str(complex(i - 1 / 2 * random.random(), 0))
                   for idx, i in enumerate(integers)]
        for s, i in zip(strings, integers):
            result = pdtypes.apply._string_to_integer(s, force=True)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_real_decimal_complex_string_to_integer_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i + 1 / 2 * random.random(), 0)) if idx % 2
                   else str(complex(i - 1 / 2 * random.random(), 0))
                   for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._string_to_integer)
        result = vec(np.array(strings), force=True)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_real_decimal_complex_string_to_integer_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i + 1 / 2 * random.random(), 0)) if idx % 2
                   else str(complex(i - 1 / 2 * random.random(), 0))
                   for idx, i in enumerate(integers)]
        result = pd.Series(strings).apply(pdtypes.apply._string_to_integer,
                                          force=True)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_real_decimal_complex_string_to_integer_forced_not_rounded_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1 / 2 * random.random(), 0) if idx % 2
                     else complex(i - 1 / 2 * random.random(), 0)
                     for idx, i in enumerate(integers)]
        strings = [str(c) for c in complexes]
        for s, c in zip(strings, complexes):
            result = pdtypes.apply._string_to_integer(s, round=False,
                                                      force=True)
            expected = int(c.real)
            self.assertEqual(result, expected)
            self.assertEqual(type(result), type(expected))

    def test_real_decimal_complex_string_to_integer_forced_not_rounded_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1 / 2 * random.random(), 0) if idx % 2
                     else complex(i - 1 / 2 * random.random(), 0)
                     for idx, i in enumerate(integers)]
        strings = [str(c) for c in complexes]
        vec = np.vectorize(pdtypes.apply._string_to_integer)
        result = vec(np.array(strings), round=False, force=True)
        expected = np.array([int(c.real) for c in complexes])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_real_decimal_complex_string_to_integer_forced_not_rounded_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + 1 / 2 * random.random(), 0) if idx % 2
                     else complex(i - 1 / 2 * random.random(), 0)
                     for idx, i in enumerate(integers)]
        strings = [str(c) for c in complexes]
        result = pd.Series(strings).apply(pdtypes.apply._string_to_integer,
                                          round=False, force=True)
        expected = pd.Series([int(c.real) for c in complexes])
        pd.testing.assert_series_equal(result, expected)

    #################################
    ####    Imaginary Complex    ####
    #################################

    def test_imaginary_complex_string_to_integer_within_ftol_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i + 1e-8, 1e-8)) if idx % 2
                   else str(complex(i - 1e-8, -1e-8))
                   for idx, i in enumerate(integers)]
        for s, i in zip(strings, integers):
            result = pdtypes.apply._string_to_integer(s, ftol=1e-6)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_imaginary_complex_string_to_integer_within_ftol_vector(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i + 1e-8, 1e-8)) if idx % 2
                   else str(complex(i - 1e-8, -1e-8))
                   for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._string_to_integer)
        result = vec(np.array(strings), ftol=1e-6)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_string_to_integer_within_ftol_series(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i + 1e-8, 1e-8)) if idx % 2
                   else str(complex(i - 1e-8, -1e-8))
                   for idx, i in enumerate(integers)]
        result = pd.Series(strings).apply(pdtypes.apply._string_to_integer,
                                          ftol=1e-6)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_imaginary_complex_string_to_integer_error(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i + random.random() / 2, random.random() / 2))
                   if idx % 2 else
                   str(complex(i - random.random() / 2,
                               -1 * random.random() / 2))
                   for idx, i in enumerate(integers)]
        err_msg = ("[pdtypes.apply._string_to_integer] could not convert "
                   "str to int without losing information: ")
        for s in strings:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._string_to_integer(s)
            self.assertEqual(str(err.exception), err_msg + repr(s))

    def test_imaginary_complex_string_to_integer_forced_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i + random.random() / 2, random.random() / 2))
                   if idx % 2 else
                   str(complex(i - random.random() / 2,
                               -1 * random.random() / 2))
                   for idx, i in enumerate(integers)]
        for s, i in zip(strings, integers):
            result = pdtypes.apply._string_to_integer(s, force=True)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_imaginary_complex_string_to_integer_forced_vector(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i + random.random() / 2, random.random() / 2))
                   if idx % 2 else
                   str(complex(i - random.random() / 2,
                               -1 * random.random() / 2))
                   for idx, i in enumerate(integers)]
        vec = np.vectorize(pdtypes.apply._string_to_integer)
        result = vec(np.array(strings), force=True)
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_string_to_integer_forced_series(self):
        integers = [-2, -1, 0, 1, 2]
        strings = [str(complex(i + random.random() / 2, random.random() / 2))
                   if idx % 2 else
                   str(complex(i - random.random() / 2,
                               -1 * random.random() / 2))
                   for idx, i in enumerate(integers)]
        result = pd.Series(strings).apply(pdtypes.apply._string_to_integer,
                                          force=True)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    def test_imaginary_complex_string_to_integer_forced_not_rounded_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random() / 2, random.random() / 2)
                     if idx % 2 else
                     complex(i - random.random() / 2, -1 * random.random() / 2)
                     for idx, i in enumerate(integers)]
        strings = [str(c) for c in complexes]
        for s, c in zip(strings, complexes):
            result = pdtypes.apply._string_to_integer(s, round=False,
                                                      force=True)
            expected = int(c.real)
            self.assertEqual(result, expected)
            self.assertEqual(type(result), type(expected))

    def test_imaginary_complex_string_to_integer_forced_not_rounded_vector(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random() / 2, random.random() / 2)
                     if idx % 2 else
                     complex(i - random.random() / 2, -1 * random.random() / 2)
                     for idx, i in enumerate(integers)]
        strings = [str(c) for c in complexes]
        vec = np.vectorize(pdtypes.apply._string_to_integer)
        result = vec(np.array(strings), round=False, force=True)
        expected = np.array([int(c.real) for c in complexes])
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_imaginary_complex_string_to_integer_forced_not_rounded_series(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i + random.random() / 2, random.random() / 2)
                     if idx % 2 else
                     complex(i - random.random() / 2, -1 * random.random() / 2)
                     for idx, i in enumerate(integers)]
        strings = [str(c) for c in complexes]
        result = pd.Series(strings).apply(pdtypes.apply._string_to_integer,
                                          round=False, force=True)
        expected = pd.Series([int(c.real) for c in complexes])
        pd.testing.assert_series_equal(result, expected)

    #################################
    ####    Character Strings    ####
    #################################

    def test_character_string_to_integer_error(self):
        strings = [chr(i % 26 + ord("a")) for i in range(5)]
        err_msg = ("[pdtypes.apply._string_to_integer] could not convert "
                   "str to int without losing information: ")
        for s in strings:
            with self.assertRaises(ValueError) as err:
                pdtypes.apply._string_to_integer(s)
            self.assertEqual(str(err.exception), err_msg + repr(s))

    ###############################
    ####    Boolean Strings    ####
    ###############################

    def test_boolean_string_to_integer_scalar(self):
        integers = [1, 0, 1, 0, 1]
        strings = [str(bool(i)) for i in integers]
        for s, i in zip(strings, integers):
            result = pdtypes.apply._string_to_integer(s)
            self.assertEqual(result, i)
            self.assertEqual(type(result), type(i))

    def test_boolean_string_to_integer_vector(self):
        integers = [1, 0, 1, 0, 1]
        strings = [str(bool(i)) for i in integers]
        vec = np.vectorize(pdtypes.apply._string_to_integer)
        result = vec(np.array(strings))
        expected = np.array(integers)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_boolean_string_to_integer_series(self):
        integers = [1, 0, 1, 0, 1]
        strings = [str(bool(i)) for i in integers]
        result = pd.Series(strings).apply(pdtypes.apply._string_to_integer)
        expected = pd.Series(integers)
        pd.testing.assert_series_equal(result, expected)

    ######################################################
    ####    Whole Timestamp Naive Datetime Strings    ####
    ######################################################

    ######################################################
    ####    Whole Timestamp Aware Datetime Strings    ####
    ######################################################

    ##################################################################
    ####    Whole Timestamp Mixed Aware/Naive Datetime Strings    ####
    ##################################################################

    ###############################################################
    ####    Whole Timestamp Mixed Timezone Datetime Strings    ####
    ###############################################################

    ##############################################
    ####    Generic Naive Datetime Strings    ####
    ##############################################

    ##############################################
    ####    Generic Aware Datetime Strings    ####
    ##############################################

    ##########################################################
    ####    Generic Mixed Aware/Naive Datetime Strings    ####
    ##########################################################

    #######################################################
    ####    Generic Mixed Timezone Datetime Strings    ####
    #######################################################

    #######################################
    ####    Whole Timedelta Strings    ####
    #######################################

    #########################################
    ####    Generic Timedelta Strings    ####
    #########################################

    #########################################
    ####    Non-standard Return Types    ####
    #########################################




if __name__ == "__main__":
    unittest.main()
