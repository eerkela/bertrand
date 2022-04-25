from datetime import timedelta
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyTimedeltaToComplexMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_timedelta_to_complex_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._timedelta_to_complex(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_timedelta_to_complex_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._timedelta_to_complex)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_timedelta_to_complex_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._timedelta_to_complex)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyTimedeltaToComplexAccuracyTests(unittest.TestCase):

    ##################################
    ####    Generic Timedeltas    ####
    ##################################

    def test_timedelta_to_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        complexes = [complex(f, 0) for f in floats]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        for t, c in zip(timedeltas, complexes):
            result = pdtypes.apply._timedelta_to_complex(t)
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))

    def test_timedelta_to_complex_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        complexes = [complex(f, 0) for f in floats]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        vec = np.vectorize(pdtypes.apply._timedelta_to_complex)
        result = vec(np.array(timedeltas))
        expected = np.array(complexes)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_timedelta_to_complex_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        complexes = [complex(f, 0) for f in floats]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        result = pd.Series(timedeltas).apply(pdtypes.apply._timedelta_to_complex)
        expected = pd.Series(complexes)
        pd.testing.assert_series_equal(result, expected)


class ApplyTimedeltaToComplexReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_pandas_timedelta_to_numpy_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                     for idx, i in enumerate(integers)]
        timedeltas = [pd.Timedelta(seconds=i) for i in integers]
        for t, c in zip(timedeltas, complexes):
            result = pdtypes.apply._timedelta_to_complex(t, return_type=type(c))
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))

    def test_standard_timedelta_to_standard_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complexes = [complex(i, 0) for i in integers]
        timedeltas = [timedelta(seconds=i) for i in integers]
        for t, c in zip(timedeltas, complexes):
            result = pdtypes.apply._timedelta_to_complex(t, return_type=type(c))
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))

    def test_standard_timedelta_to_numpy_complex_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        complex_types = [np.complex64, np.complex128]
        complexes = [complex_types[idx % len(complex_types)](i)
                     for idx, i in enumerate(integers)]
        timedeltas = [timedelta(seconds=i) for i in integers]
        for t, c in zip(timedeltas, complexes):
            result = pdtypes.apply._timedelta_to_complex(t, return_type=type(c))
            self.assertEqual(result, c)
            self.assertEqual(type(result), type(c))


if __name__ == "__main__":
    unittest.main()
