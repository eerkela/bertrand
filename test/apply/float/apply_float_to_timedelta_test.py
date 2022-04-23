from datetime import timedelta
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyFloatToTimedeltaMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_float_to_timedelta_scalar(self):
        na_val = None
        expected = pd.NaT
        result = pdtypes.apply._float_to_timedelta(na_val)
        # numpy can't parse pd.NaT, and pd.NaT != pd.NaT, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_float_to_timedelta_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        vec = np.vectorize(pdtypes.apply._float_to_timedelta)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NaT) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_float_to_timedelta_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        result = pd.Series(nones).apply(pdtypes.apply._float_to_timedelta)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToTimedeltaAccuracyTests(unittest.TestCase):

    ##############################
    ####    Generic Floats    ####
    ##############################

    def test_float_to_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        for f, t in zip(floats, timedeltas):
            result = pdtypes.apply._float_to_timedelta(f)
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_float_to_timedelta_vector(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        vec = np.vectorize(pdtypes.apply._float_to_timedelta)
        result = vec(np.array(floats))
        expected = np.array(timedeltas)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_to_timedelta_series(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        result = pd.Series(floats).apply(pdtypes.apply._float_to_timedelta)
        expected = pd.Series(timedeltas)
        pd.testing.assert_series_equal(result, expected)


class ApplyFloatToTimedeltaReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_float_to_standard_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        timedeltas = [timedelta(seconds=f) for f in floats]
        for f, t in zip(floats, timedeltas):
            result = pdtypes.apply._float_to_timedelta(f, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_standard_float_to_custom_timedelta_scalar(self):
        class CustomTimedelta:
            def __init__(self, seconds: float):
                self.delta = pd.Timedelta(seconds=seconds)

            def to_timedelta(self) -> pd.Timedelta:
                return self.delta

        integers = [-2, -1, 0, 1, 2]
        floats = [i + random.random() for i in integers]
        for f in floats:
            result = pdtypes.apply._float_to_timedelta(f, return_type=CustomTimedelta)
            self.assertEqual(type(result), CustomTimedelta)

    def test_numpy_float_to_standard_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        timedeltas = [pd.Timedelta(seconds=f) for f in floats]
        for f, t in zip(floats, timedeltas):
            result = pdtypes.apply._float_to_timedelta(f, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_numpy_float_to_standard_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        float_types = [np.float16, np.float32, np.float64]
        floats = [float_types[idx % len(float_types)](i)
                  for idx, i in enumerate(integers)]
        timedeltas = [timedelta(seconds=float(f)) for f in floats]
        for f, t in zip(floats, timedeltas):
            result = pdtypes.apply._float_to_timedelta(f, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))


if __name__ == "__main__":
    unittest.main()
