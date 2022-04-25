from datetime import timedelta
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyTimedeltaToFloatMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_timedelta_to_float_scalar(self):
        na_val = None
        expected = np.nan
        result = pdtypes.apply._timedelta_to_float(na_val)
        np.testing.assert_array_equal(result, expected)

    def test_na_timedelta_to_float_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        vec = np.vectorize(pdtypes.apply._timedelta_to_float)
        result = vec(np.array(nones))
        expected = np.array(nans)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_timedelta_to_float_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [np.nan, np.nan, np.nan, np.nan]
        result = pd.Series(nones).apply(pdtypes.apply._timedelta_to_float)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyTimedeltaToFloatAccuracyTests(unittest.TestCase):

    ##################################
    ####    Generic Timedeltas    ####
    ##################################

    def test_timedelta_to_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        for t, f in zip(timedeltas, floats):
            result = pdtypes.apply._timedelta_to_float(t)
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))

    def test_timedelta_to_float_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        vec = np.vectorize(pdtypes.apply._timedelta_to_float)
        result = vec(np.array(timedeltas))
        expected = np.array(floats)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_timedelta_to_float_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        result = pd.Series(timedeltas).apply(pdtypes.apply._timedelta_to_float)
        expected = pd.Series(floats)
        pd.testing.assert_series_equal(result, expected)


class ApplyTimedeltaToFloatReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_pandas_timedelta_to_numpy_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i + random.random())
                  for idx, i in enumerate(integers)]
        timedeltas = [pd.Timedelta(seconds=float(f)) for f in floats]
        for t, f in zip(timedeltas, floats):
            result = pdtypes.apply._timedelta_to_float(t, return_type=type(f))
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))

    def test_standard_timedelta_to_standard_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        timedeltas = [timedelta(seconds=f) for f in floats]
        for t, f in zip(timedeltas, floats):
            result = pdtypes.apply._timedelta_to_float(t, return_type=type(f))
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))

    def test_standard_timedelta_to_numpy_float_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i + random.random())
                  for idx, i in enumerate(integers)]
        timedeltas = [timedelta(seconds=float(f)) for f in floats]
        for t, f in zip(timedeltas, floats):
            result = pdtypes.apply._timedelta_to_float(t, return_type=type(f))
            self.assertEqual(result, f)
            self.assertEqual(type(result), type(f))


if __name__ == "__main__":
    unittest.main()
