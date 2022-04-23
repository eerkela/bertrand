from datetime import timedelta
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyIntegerToTimedeltaMissingValueTests(unittest.TestCase):

    ##############################
    ####    Missing Values    ####
    ##############################

    def test_na_integer_to_timedelta_scalar(self):
        na_val = None
        expected = pd.NaT
        result = pdtypes.apply._integer_to_timedelta(na_val)
        # numpy can't parse pd.NaT, and pd.NaT != pd.NaT, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(type(result) == type(expected))

    def test_na_integer_to_timedelta_vector(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        vec = np.vectorize(pdtypes.apply._integer_to_timedelta)
        result = vec(np.array(nones))
        expected = np.array(nans)
        # numpy can't parse pd.NA, and pd.NA != pd.NA, at least not directly
        # np.testing.assert_array_equal(result, expected)
        self.assertTrue(all(type(n) == type(pd.NaT) for n in result))
        self.assertEqual(result.dtype, expected.dtype)

    def test_na_integer_to_timedelta_series(self):
        nones = [None, np.nan, pd.NA, pd.NaT]
        nans = [pd.NaT, pd.NaT, pd.NaT, pd.NaT]
        result = pd.Series(nones).apply(pdtypes.apply._integer_to_timedelta)
        expected = pd.Series(nans)
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToTimedeltaAccuracyTests(unittest.TestCase):

    ################################
    ####    Generic Integers    ####
    ################################

    def test_integer_to_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        timedeltas = [pd.Timedelta(seconds=i) for i in integers]
        for i, t in zip(integers, timedeltas):
            result = pdtypes.apply._integer_to_timedelta(i)
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_integer_to_timedelta_vector(self):
        integers = [-2, -1, 0, 1, 2]
        timedeltas = [pd.Timedelta(seconds=i) for i in integers]
        vec = np.vectorize(pdtypes.apply._integer_to_timedelta)
        result = vec(np.array(integers))
        expected = np.array(timedeltas)
        np.testing.assert_array_equal(result, expected)
        self.assertEqual(result.dtype, expected.dtype)

    def test_float_to_timedelta_series(self):
        integers = [-2, -1, 0, 1, 2]
        timedeltas = [pd.Timedelta(seconds=i) for i in integers]
        result = pd.Series(integers).apply(pdtypes.apply._integer_to_timedelta)
        expected = pd.Series(timedeltas)
        pd.testing.assert_series_equal(result, expected)


class ApplyIntegerToTimedeltaReturnTypeTests(unittest.TestCase):

    #########################################
    ####    Non-standard Return Types    ####
    #########################################

    def test_standard_integer_to_standard_timedelta_scalar(self):
        integers = [-2, -1, 0, 1, 2]
        timedeltas = [timedelta(seconds=i) for i in integers]
        for i, t in zip(integers, timedeltas):
            result = pdtypes.apply._integer_to_timedelta(i, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_standard_integer_to_custom_timedelta_class_scalar(self):
        class CustomTimedelta:
            def __init__(self, seconds: int):
                self.delta = pd.Timedelta(seconds=seconds)

            def to_timedelta(self) -> pd.Timedelta:
                return self.delta

        integers = [-2, -1, 0, 1, 2]
        for i in integers:
            result = pdtypes.apply._integer_to_timedelta(i, return_type=CustomTimedelta)
            self.assertEqual(type(result), CustomTimedelta)

    def test_numpy_signed_integer_to_pandas_timedelta_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        timedeltas = [pd.Timedelta(seconds=i) for i in integers]
        for i, t in zip(integers, timedeltas):
            result = pdtypes.apply._integer_to_timedelta(i, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_numpy_signed_integer_to_standard_timedelta_scalar(self):
        integer_types = [np.int8, np.int16, np.int32, np.int64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([-2, -1, 0, 1, 2])]
        timedeltas = [timedelta(seconds=int(i)) for i in integers]
        for i, t in zip(integers, timedeltas):
            result = pdtypes.apply._integer_to_timedelta(i, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_numpy_unsigned_integer_to_pandas_timedelta_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        timedeltas = [pd.Timedelta(seconds=i) for i in integers]
        for i, t in zip(integers, timedeltas):
            result = pdtypes.apply._integer_to_timedelta(i, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))

    def test_numpy_unsigned_integer_to_standard_timedelta_scalar(self):
        integer_types = [np.uint8, np.uint16, np.uint32, np.uint64]
        integers = [integer_types[idx % len(integer_types)](i)
                    for idx, i in enumerate([0, 1, 2, 3, 4])]
        timedeltas = [timedelta(seconds=int(i)) for i in integers]
        for i, t in zip(integers, timedeltas):
            result = pdtypes.apply._integer_to_timedelta(i, return_type=type(t))
            self.assertEqual(result, t)
            self.assertEqual(type(result), type(t))


if __name__ == "__main__":
    unittest.main()
