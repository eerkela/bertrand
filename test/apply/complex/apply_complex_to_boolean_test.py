import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyComplexToBooleanMissingValueTests(unittest.TestCase):

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


class ApplyComplexToBooleanAccuracyTests(unittest.TestCase):

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

    ####################################
    ####    Generic Real Complex    ####
    ####################################

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


class ApplyComplexToBooleanReturnTypeTests(unittest.TestCase):

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


if __name__ == "__main__":
    unittest.main()
