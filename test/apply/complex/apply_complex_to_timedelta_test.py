from datetime import timedelta
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyComplexToTimedeltaMissingValueTests(unittest.TestCase):

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


class ApplyComplexToTimedeltaAccuracyTests(unittest.TestCase):

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


class ApplyComplexToTimedeltaReturnTypeTests(unittest.TestCase):

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
