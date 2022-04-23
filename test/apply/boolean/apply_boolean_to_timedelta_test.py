from datetime import timedelta
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyBooleanToTimedeltaMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_boolean_to_timedelta_scalar(self):
        na_val = None
        expected = pd.NaT
        result = pdtypes.apply._boolean_to_timedelta(na_val)
        # numpy can't parse pd.NaT, and pd.NaT != pd.NaT, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_boolean_to_timedelta_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        vec = np.vectorize(pdtypes.apply._boolean_to_timedelta)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NaT) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_boolean_to_timedelta_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        result = pd.Series(nones).apply(pdtypes.apply._boolean_to_timedelta)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyBooleanToTimedeltaAccuracyTests(unittest.TestCase):

    ################################
    ####    Generic Booleans    ####
    ################################

    def test_boolean_to_timedelta_scalar(self):
        booleans = [True, False, True, False, True]
        timedeltas = [pd.Timedelta(seconds=float(b)) for b in booleans]
        for b, t in zip(booleans, timedeltas):
            result = pdtypes.apply._boolean_to_timedelta(b)
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_boolean_to_timedelta_vector(self):
        booleans = [True, False, True, False, True]
        timedeltas = [pd.Timedelta(seconds=float(b)) for b in booleans]
        vec = np.vectorize(pdtypes.apply._boolean_to_timedelta)
        result = vec(np.array(booleans))
        expected = np.array(timedeltas)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_boolean_to_timedelta_series(self):
        booleans = [True, False, True, False, True]
        timedeltas = [pd.Timedelta(seconds=float(b)) for b in booleans]
        result = pd.Series(booleans).apply(pdtypes.apply._boolean_to_timedelta)
        expected = pd.Series(timedeltas)
        pd.testing.assert_series_equal(result, expected)


class ApplyBooleanToTimedeltaReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_boolean_to_standard_timedelta_scalar(self):
        booleans = [True, False, True, False, True]
        timedeltas = [timedelta(seconds=b) for b in booleans]
        for b, t in zip(booleans, timedeltas):
            result = pdtypes.apply._boolean_to_timedelta(b, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_standard_boolean_to_custom_timedelta_scalar(self):
        class CustomTimedelta:
            def __init__(self, seconds: bool):
                self.delta = pd.Timedelta(seconds=seconds)

            def to_timedelta(self) -> pd.Timedelta:
                return self.delta

        booleans = [True, False, True, False, True]
        for b in booleans:
            result = pdtypes.apply._float_to_timedelta(b, return_type=CustomTimedelta)
            self.assertEqual(type(result), CustomTimedelta)


if __name__ == "__main__":
    unittest.main()
