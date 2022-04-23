from __future__ import annotations
from datetime import datetime, timezone
import random
import unittest

import numpy as np
import pandas as pd

from context import pdtypes
import pdtypes.apply


random.seed(12345)


class ApplyComplexToDatetimeMissingValueTests(unittest.TestCase):

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


class ApplyComplexToDatetimeAccuracyTests(unittest.TestCase):

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


class ApplyComplexToDatetimeReturnTypeTests(unittest.TestCase):

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


if __name__ == "__main__":
    unittest.main()
