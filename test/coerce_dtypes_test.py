from datetime import datetime, timedelta, timezone
import random
import unittest

import numpy as np
import pandas as pd
from pandas.testing import assert_frame_equal, assert_series_equal
import pytz

if __name__ == "__main__":
    from pathlib import Path
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from datatube.dtype import coerce_dtypes


unittest.TestCase.maxDiff = None


class CoerceDtypeBasicTests(unittest.TestCase):

    def test_coerce_dtypes_returns_copy(self):
        # series
        in_series = pd.Series([1, 2, 3])
        out_series = coerce_dtypes(in_series, float)
        self.assertNotEqual(id(in_series), id(out_series))

        # dataframe
        in_df = pd.DataFrame({"copy": [1, 2, 3]})
        out_df = coerce_dtypes(in_df, {"copy": float})
        self.assertNotEqual(id(in_df), id(out_df))


class CoerceIntegerDtypeTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        size = 3  # minimum 3
        cls.integers = [-1 * size // 2 + i + 1 for i in range(size)]
        # integers = [..., -1, 0, 1, ...]
        cls.bool_flags = [(i + 1) % 2 for i in range(size)]
        # bool_flags = [1, 0, 1, 0, 1, ...]
        cls.col_name = "integers"

    def test_coerce_from_integer_to_integer_no_na(self):
        in_data = self.integers
        out_data = in_data.copy()

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, int)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: int})
        assert_frame_equal(result, out_df)

    def test_coerce_from_integer_to_integer_with_na(self):
        in_data = self.integers + [None]
        out_data = in_data.copy()

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, int)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: int})
        assert_frame_equal(result, out_df)

    def test_coerce_from_integer_to_float_no_na(self):
        in_data = self.integers
        out_data = [float(i) for i in self.integers]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, float)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: float})
        assert_frame_equal(result, out_df)

    def test_coerce_from_integer_to_float_with_na(self):
        in_data = self.integers + [None]
        out_data = [float(i) for i in self.integers] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, float)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: float})
        assert_frame_equal(result, out_df)

    def test_coerce_from_integer_to_complex_no_na(self):
        in_data = self.integers
        out_data = [complex(i, 0) for i in self.integers]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, complex)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: complex})
        assert_frame_equal(result, out_df)

    def test_coerce_from_integer_to_complex_with_na(self):
        in_data = self.integers + [None]
        out_data = [complex(i, 0) for i in self.integers] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, complex)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: complex})
        assert_frame_equal(result, out_df)

    def test_coerce_from_integer_to_string_no_na(self):
        in_data = self.integers
        out_data = [str(i) for i in self.integers]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, str)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: str})
        assert_frame_equal(result, out_df)

    def test_coerce_from_integer_to_string_with_na(self):
        in_data = self.integers + [None]
        out_data = [str(i) for i in self.integers] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, str)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: str})
        assert_frame_equal(result, out_df)

    def test_coerce_from_generic_integer_to_boolean_no_na(self):
        in_data = self.integers

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_generic_integer_to_boolean_with_na(self):
        in_data = self.integers + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_integer_bool_flag_to_boolean_no_na(self):
        in_data = self.bool_flags
        out_data = [bool(i) for i in self.bool_flags]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, bool)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: bool})
        assert_frame_equal(result, out_df)

    def test_coerce_from_integer_bool_flag_to_boolean_with_na(self):
        in_data = self.bool_flags + [None]
        out_data = [bool(i) for i in self.bool_flags] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, bool)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: bool})
        assert_frame_equal(result, out_df)

    def test_coerce_from_integer_to_datetime_no_na(self):
        in_data = self.integers
        out_data = [datetime.fromtimestamp(i, tz=timezone.utc)
                    for i in self.integers]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)
        
    def test_coerce_from_integer_to_datetime_with_na(self):
        in_data = self.integers + [None]
        out_data = [datetime.fromtimestamp(i, tz=timezone.utc)
                    for i in self.integers] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_integer_to_timedelta_no_na(self):
        in_data = self.integers
        out_data = [timedelta(seconds=i) for i in self.integers]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, timedelta)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: timedelta})
        assert_frame_equal(result, out_df)

    def test_coerce_from_integer_to_timedelta_with_na(self):
        in_data = self.integers + [None]
        out_data = [timedelta(seconds=i) for i in self.integers] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, timedelta)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: timedelta})
        assert_frame_equal(result, out_df)

    def test_coerce_from_integer_to_object_no_na(self):
        in_series = pd.Series(self.integers)
        out_series = in_series.astype(np.dtype("O"))

        # series
        result = coerce_dtypes(in_series, object)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_series})
        out_df = pd.DataFrame({self.col_name: out_series})
        result = coerce_dtypes(in_df, {self.col_name: object})
        assert_frame_equal(result, out_df)

    def test_coerce_from_integer_to_object_with_na(self):
        in_series = pd.Series(self.integers + [None])
        out_series = in_series.astype(np.dtype("O"))

        # series
        result = coerce_dtypes(in_series, object)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_series})
        out_df = pd.DataFrame({self.col_name: out_series})
        result = coerce_dtypes(in_df, {self.col_name: object})
        assert_frame_equal(result, out_df)


class CoerceFloatDtypeTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        random.seed(12345)
        size = 3  # minimum 3
        cls.whole_floats = [-1 * size // 2 + i + 1.0 for i in range(size)]
        # whole_flats = [..., -1.0, 0.0, 1.0, ...]
        cls.decimal_floats = [-1 * size // 2 + i + 1 + random.random()
                              for i in range(size)]
        # decimal_floats = [..., -1.0 + e, 0.0 + e, 1.0 + e, ...]
        cls.decimal_floats_between_0_and_1 = [random.random()
                                              for _ in range(size)]
        # decimal_floats_between_0_and_1 = [0.xxxx, 0.xxxx, 0.xxxx, ...]
        cls.bool_flags = [(i + 1.0) % 2 for i in range(size)]
        # bool_flags = [1.0, 0.0, 1.0, 0.0, 1.0, ...]
        cls.col_name = "floats"

    def test_coerce_from_whole_float_to_integer_no_na(self):
        in_data = self.whole_floats
        out_data = [int(f) for f in self.whole_floats]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, int)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: int})
        assert_frame_equal(result, out_df)

    def test_coerce_from_whole_float_to_integer_with_na(self):
        in_data = self.whole_floats + [None]
        out_data = [int(f) for f in self.whole_floats] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, int)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: int})
        assert_frame_equal(result, out_df)

    def test_coerce_from_decimal_float_to_integer_no_na(self):
        in_data = self.decimal_floats

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {int} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, int)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {int} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: int})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_decimal_float_to_integer_with_na(self):
        in_data = self.decimal_floats + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {int} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, int)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {int} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: int})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_float_to_float_no_na(self):
        in_data = self.decimal_floats
        out_data = in_data.copy()
        
        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, float)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: float})
        assert_frame_equal(result, out_df)

    def test_coerce_from_float_to_float_with_na(self):
        in_data = self.decimal_floats + [None]
        out_data = in_data.copy()

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, float)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: float})
        assert_frame_equal(result, out_df)

    def test_coerce_from_float_to_complex_no_na(self):
        in_data = self.decimal_floats
        out_data = [complex(f, 0) for f in self.decimal_floats]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, complex)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: complex})
        assert_frame_equal(result, out_df)

    def test_coerce_from_float_to_complex_with_na(self):
        in_data = self.decimal_floats + [None]
        out_data = [complex(f, 0) for f in self.decimal_floats] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, complex)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: complex})
        assert_frame_equal(result, out_df)

    def test_coerce_from_float_to_string_no_na(self):
        in_data = self.decimal_floats
        out_data = [str(f) for f in self.decimal_floats]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, str)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: str})
        assert_frame_equal(result, out_df)

    def test_coerce_from_float_to_string_with_na(self):
        in_data = self.decimal_floats + [None]
        out_data = [str(f) for f in self.decimal_floats] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, str)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: str})
        assert_frame_equal(result, out_df)

    def test_coerce_from_generic_float_to_boolean_no_na(self):
        in_data = self.decimal_floats

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_generic_float_to_boolean_with_na(self):
        in_data = self.decimal_floats + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_float_bool_flag_to_boolean_no_na(self):
        in_data = self.bool_flags
        out_data = [bool(f) for f in self.bool_flags]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, bool)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: bool})
        assert_frame_equal(result, out_df)

    def test_coerce_from_float_bool_flag_to_boolean_with_na(self):
        in_data = self.bool_flags + [None]
        out_data = [bool(f) for f in self.bool_flags] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, bool)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: bool})
        assert_frame_equal(result, out_df)

    def test_coerce_from_decimal_float_between_0_and_1_to_boolean_no_na(self):
        in_data = self.decimal_floats_between_0_and_1

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_decimal_float_between_0_and_1_to_boolean_with_na(self):
        in_data = self.decimal_floats_between_0_and_1 + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_float_to_datetime_no_na(self):
        in_data = self.decimal_floats
        out_data = [datetime.fromtimestamp(f, tz=timezone.utc)
                    for f in self.decimal_floats]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_float_to_datetime_with_na(self):
        in_data = self.decimal_floats + [None]
        out_data = [datetime.fromtimestamp(f, tz=timezone.utc)
                    for f in self.decimal_floats] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_float_to_timedelta_no_na(self):
        in_data = self.decimal_floats
        out_data = [timedelta(seconds=f) for f in self.decimal_floats]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, timedelta)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: timedelta})
        assert_frame_equal(result, out_df)

    def test_coerce_from_float_to_timedelta_with_na(self):
        in_data = self.decimal_floats + [None]
        out_data = [timedelta(seconds=f) for f in self.decimal_floats] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, timedelta)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: timedelta})
        assert_frame_equal(result, out_df)

    def test_coerce_from_float_to_object_no_na(self):
        in_series = pd.Series(self.decimal_floats)
        out_series = in_series.astype(np.dtype("O"))

        # series
        result = coerce_dtypes(in_series, object)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_series})
        out_df = pd.DataFrame({self.col_name: out_series})
        result = coerce_dtypes(in_df, {self.col_name: object})
        assert_frame_equal(result, out_df)

    def test_coerce_from_float_to_object_with_na(self):
        in_series = pd.Series(self.decimal_floats + [None])
        out_series = in_series.astype(np.dtype("O"))

        # series
        result = coerce_dtypes(in_series, object)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_series})
        out_df = pd.DataFrame({self.col_name: out_series})
        result = coerce_dtypes(in_df, {self.col_name: object})
        assert_frame_equal(result, out_df)


class CoerceComplexDtypeTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        random.seed(12345)
        size = 3
        cls.real_whole_complex = [complex(-1 * size // 2 + i + 1.0, 0)
                                  for i in range(size)]
        # ^ = [..., complex(-1, 0), complex(0, 0), complex(1, 0), ...]
        cls.real_complex = [complex(-1 * size // 2 + i + 1 + random.random(), 0)
                            for i in range(size)]
        # ^ = [..., complex(-1+e, 0), complex(0+e, 0), complex(1+e, 0), ...]
        cls.real_complex_between_0_and_1 = [complex(random.random(), 0)
                                            for _ in range(size)]
        # ^ = [complex(0.xxxx, 0), complex(0.xxxx, 0), complex(0.xxxx, 0), ...]
        cls.imag_complex = [complex(-1 * size // 2 + i + 1 + random.random(),
                                    -1 * size // 2 + i + 1 + random.random())
                            for i in range(size)]
        # ^ = [..., complex(-1+e,-1+e), complex(0+e,0+e), complex(1+e,1+e), ...]
        cls.bool_flags = [complex((i + 1) % 2, 0) for i in range(size)]
        # ^ = [complex(1, 0), complex(0, 0), complex(1, 0), complex(0, 0), ...]
        cls.col_name = "complex"

    def test_coerce_from_real_whole_complex_to_integer_no_na(self):
        in_data = self.real_whole_complex
        out_data = [int(c.real) for c in self.real_whole_complex]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, int)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: int})
        assert_frame_equal(result, out_df)

    def test_coerce_from_real_whole_complex_to_integer_with_na(self):
        in_data = self.real_whole_complex + [None]
        out_data = [int(c.real) for c in self.real_whole_complex] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, int)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: int})
        assert_frame_equal(result, out_df)

    def test_coerce_from_real_decimal_complex_to_integer_no_na(self):
        in_data = self.real_complex

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {int} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, int)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {int} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: int})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_real_decimal_complex_to_integer_with_na(self):
        in_data = self.real_complex + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {int} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, int)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {int} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: int})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_imaginary_complex_to_integer_no_na(self):
        in_data = self.imag_complex

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {int} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, int)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {int} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: int})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_imaginary_complex_to_integer_with_na(self):
        in_data = self.imag_complex + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {int} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, int)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {int} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: int})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_real_complex_to_float_no_na(self):
        in_data = self.real_complex
        out_data = [c.real for c in self.real_complex]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, float)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: float})
        assert_frame_equal(result, out_df)

    def test_coerce_from_real_complex_to_float_with_na(self):
        in_data = self.real_complex + [None]
        out_data = [c.real for c in self.real_complex] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, float)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: float})
        assert_frame_equal(result, out_df)

    def test_coerce_from_imaginary_complex_to_float_no_na(self):
        in_data = self.imag_complex

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {float} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, float)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {float} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: float})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_imaginary_complex_to_float_with_na(self):
        in_data = self.imag_complex + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {float} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, float)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {float} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: float})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_complex_to_complex_no_na(self):
        in_data = self.imag_complex
        out_data = in_data.copy()

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, complex)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: complex})
        assert_frame_equal(result, out_df)

    def test_coerce_from_complex_to_complex_with_na(self):
        in_data = self.imag_complex + [None]
        out_data = in_data.copy()

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, complex)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: complex})
        assert_frame_equal(result, out_df)

    def test_coerce_from_complex_to_string_no_na(self):
        in_data = self.imag_complex
        out_data = [str(c) for c in self.imag_complex]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, str)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: str})
        assert_frame_equal(result, out_df)

    def test_coerce_from_complex_to_string_with_na(self):
        in_data = self.imag_complex + [None]
        out_data = [str(c) for c in self.imag_complex] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, str)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: str})
        assert_frame_equal(result, out_df)

    def test_coerce_from_complex_bool_flag_to_boolean_no_na(self):
        in_data = self.bool_flags
        out_data = [bool(c.real) for c in self.bool_flags]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, bool)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: bool})
        assert_frame_equal(result, out_df)

    def test_coerce_from_complex_bool_flag_to_boolean_with_na(self):
        in_data = self.bool_flags + [None]
        out_data = [bool(c.real) for c in self.bool_flags] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, bool)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: bool})
        assert_frame_equal(result, out_df)

    def test_coerce_from_real_complex_to_boolean_no_na(self):
        in_data = self.real_complex

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_real_complex_to_boolean_with_na(self):
        in_data = self.real_complex + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_real_whole_complex_to_boolean_no_na(self):
        in_data = self.real_whole_complex

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_real_whole_complex_to_boolean_with_na(self):
        in_data = self.real_whole_complex + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_real_complex_between_0_and_1_to_boolean_no_na(self):
        in_data = self.real_complex_between_0_and_1

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_real_complex_between_0_and_1_to_boolean_with_na(self):
        in_data = self.real_complex_between_0_and_1 + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_imaginary_complex_to_boolean_no_na(self):
        in_data = self.imag_complex

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_imaginary_complex_to_boolean_with_na(self):
        in_data = self.imag_complex + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_real_complex_to_datetime_no_na(self):
        in_data = self.real_complex
        out_data = [datetime.fromtimestamp(c.real, tz=timezone.utc)
                    for c in self.real_complex]
        
        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_real_complex_to_datetime_with_na(self):
        in_data = self.real_complex + [None]
        out_data = [datetime.fromtimestamp(c.real, tz=timezone.utc)
                    for c in self.real_complex] + [None]
        
        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_imaginary_complex_to_datetime_no_na(self):
        in_data = self.imag_complex

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {datetime} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, datetime)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {datetime} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: datetime})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_imaginary_complex_to_datetime_with_na(self):
        in_data = self.imag_complex + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {datetime} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, datetime)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {datetime} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: datetime})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_real_complex_to_timedelta_no_na(self):
        in_data = self.real_complex
        out_data = [timedelta(seconds=c.real) for c in self.real_complex]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, timedelta)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: timedelta})
        assert_frame_equal(result, out_df)

    def test_coerce_from_real_complex_to_timedelta_with_na(self):
        in_data = self.real_complex + [None]
        out_data = ([timedelta(seconds=c.real) for c in self.real_complex] +
                    [None])

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, timedelta)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: timedelta})
        assert_frame_equal(result, out_df)

    def test_coerce_from_imaginary_complex_to_timedelta_no_na(self):
        in_data = self.imag_complex

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {timedelta} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, timedelta)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {timedelta} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: timedelta})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_imaginary_complex_to_timedelta_with_na(self):
        in_data = self.imag_complex + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {timedelta} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, timedelta)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {timedelta} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: timedelta})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_complex_to_object_no_na(self):
        in_series = pd.Series(self.imag_complex)
        out_series = in_series.astype(np.dtype("O"))

        # series
        result = coerce_dtypes(in_series, object)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_series})
        out_df = pd.DataFrame({self.col_name: out_series})
        result = coerce_dtypes(in_df, {self.col_name: object})
        assert_frame_equal(result, out_df)

    def test_coerce_from_complex_to_object_wth_na(self):
        in_series = pd.Series(self.imag_complex + [None])
        out_series = in_series.astype(np.dtype("O"))

        # series
        result = coerce_dtypes(in_series, object)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_series})
        out_df = pd.DataFrame({self.col_name: out_series})
        result = coerce_dtypes(in_df, {self.col_name: object})
        assert_frame_equal(result, out_df)


class CoerceStringDtypeTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        random.seed(12345)
        size = 3
        cls.integers = [-1 * size // 2 + i + 1 for i in range(size)]
        # ^ = [..., -1, 0, 1, ...]
        cls.floats = [i + random.random() for i in cls.integers]
        # ^ = [..., -1+e, 0+e, 1+e, ...]
        cls.complex = [complex(f, f) for f in cls.floats]
        # ^ = [..., complex(-1+e,-1+e), complex(0+e,0+e), complex(1+e,1+e), ...]
        cls.characters = [chr((i % 26) + ord("a")) for i in range(size)]
        # ^ = ["a", "b", "c", ..., "a", "b", "c", ...]
        cls.booleans = [bool((i + 1) % 2) for i in range(size)]
        # ^ = [True, False, True, False, ...]
        cls.naive_datetimes = [datetime.utcfromtimestamp(f) for f in cls.floats]
        # ^ = [..., utc time -1+e, utc time 0+e, utc_time 1+e, ...] (no tz)
        cls.aware_datetimes = [datetime.fromtimestamp(f, tz=timezone.utc)
                               for f in cls.floats]
        # ^ = [..., utc time -1+e, utc time 0+e, utc_time 1+e, ...] (with tz)
        cls.aware_naive_datetimes = []
        for index, f in enumerate(cls.floats):
            if index % 2:  # naive
                cls.aware_naive_datetimes.append(datetime.utcfromtimestamp(f))
            else:  # aware
                val = datetime.fromtimestamp(f, tz=timezone.utc)
                cls.aware_naive_datetimes.append(val)
        # ^ = [aware, naive, aware, naive, aware, ...]
        cls.mixed_timezones = []
        for index, f in enumerate(cls.floats):
            tz_name = pytz.all_timezones[index % len(pytz.all_timezones)]
            tz = pytz.timezone(tz_name)
            val = datetime.fromtimestamp(f, tz=tz)
            cls.mixed_timezones.append(val)
        # ^ = ["Africa/Abidjan", "Africa/Accra", "Africa/Addis_Ababa", ...]
        cls.timedeltas = [timedelta(seconds=f) for f in cls.floats]
        # ^ = [..., -1+e seconds, 0+e seconds, 1+e seconds, ...]
        cls.col_name = "strings"

    def test_coerce_from_integer_string_to_integer_no_na(self):
        in_data = [str(i) for i in self.integers]
        out_data = self.integers

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, int)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: int})
        assert_frame_equal(result, out_df)

    def test_coerce_from_integer_string_to_integer_with_na(self):
        in_data = [str(i) for i in self.integers] + [None]
        out_data = self.integers + [None]
        
        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, int)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: int})
        assert_frame_equal(result, out_df)

    def test_coerce_from_float_string_to_float_no_na(self):
        in_data = [str(f) for f in self.floats]
        out_data = self.floats
        
        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, float)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: float})
        assert_frame_equal(result, out_df)

    def test_coerce_from_float_string_to_float_with_na(self):
        in_data = [str(f) for f in self.floats] + [None]
        out_data = self.floats + [None]
        
        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, float)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: float})
        assert_frame_equal(result, out_df)

    def test_coerce_from_complex_string_to_complex_no_na(self):
        in_data = [str(c) for c in self.complex]
        out_data = self.complex

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, complex)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: complex})
        assert_frame_equal(result, out_df)

    def test_coerce_from_complex_string_to_complex_with_na(self):
        in_data = [str(c) for c in self.complex] + [None]
        out_data = self.complex + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, complex)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: complex})
        assert_frame_equal(result, out_df)

    def test_coerce_from_character_string_to_string_no_na(self):
        in_data = self.characters
        out_data = in_data.copy()

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, str)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: str})
        assert_frame_equal(result, out_df)

    def test_coerce_from_character_string_to_string_with_na(self):
        in_data = self.characters + [None]
        out_data = in_data.copy()

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, str)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: str})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_string_to_boolean_no_na(self):
        in_data = [str(b) for b in self.booleans]
        out_data = self.booleans

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, bool)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: bool})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_string_to_boolean_with_na(self):
        in_data = [str(b) for b in self.booleans] + [None]
        out_data = self.booleans + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, bool)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: bool})
        assert_frame_equal(result, out_df)

    def test_coerce_from_naive_datetime_string_to_datetime_no_na(self):
        in_data = [str(d) for d in self.naive_datetimes]
        out_data = self.naive_datetimes

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_naive_datetime_string_to_datetime_with_na(self):
        in_data = [str(d) for d in self.naive_datetimes] + [None]
        out_data = self.naive_datetimes + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_naive_ISO_8601_string_to_datetime_no_na(self):
        in_data = [d.isoformat() for d in self.naive_datetimes]
        out_data = self.naive_datetimes

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_naive_ISO_8601_string_to_datetime_with_na(self):
        in_data = [d.isoformat() for d in self.naive_datetimes] + [None]
        out_data = self.naive_datetimes + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_aware_datetime_string_to_datetime_no_na(self):
        in_data = [str(d) for d in self.aware_datetimes]
        out_data = self.aware_datetimes

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_aware_datetime_string_to_datetime_with_na(self):
        in_data = [str(d) for d in self.aware_datetimes] + [None]
        out_data = self.aware_datetimes + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_aware_ISO_8601_string_to_datetime_no_na(self):
        in_data = [d.isoformat() for d in self.aware_datetimes]
        out_data = self.aware_datetimes

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_aware_ISO_8601_string_to_datetime_with_na(self):
        in_data = [d.isoformat() for d in self.aware_datetimes] + [None]
        out_data = self.aware_datetimes + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_aware_naive_datetime_string_to_datetime_no_na(self):
        raise NotImplementedError()

    def test_coerce_from_aware_naive_datetime_string_to_datetime_with_na(self):
        raise NotImplementedError()

    def test_coerce_from_aware_naive_ISO_8601_string_to_datetime_no_na(self):
        raise NotImplementedError()

    def test_coerce_from_aware_naive_ISO_8601_string_to_datetime_with_na(self):
        raise NotImplementedError()

    def test_coerce_from_mixed_tz_datetime_string_to_datetime_no_na(self):
        raise NotImplementedError()

    def test_coerce_from_mixed_tz_datetime_string_to_datetime_with_na(self):
        raise NotImplementedError()

    def test_coerce_from_mixed_tz_ISO_8601_string_to_datetime_no_na(self):
        raise NotImplementedError()

    def test_coerce_from_mixed_tz_ISO_8601_string_to_datetime_with_na(self):
        raise NotImplementedError()

    def test_coerce_from_timedelta_string_to_timedelta_no_na(self):
        in_data = [str(t) for t in self.timedeltas]
        out_data = self.timedeltas

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, timedelta)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: timedelta})
        assert_frame_equal(result, out_df)

    def test_coerce_from_timedelta_string_to_timedelta_with_na(self):
        in_data = [str(t) for t in self.timedeltas] + [None]
        out_data = self.timedeltas + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, timedelta)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: timedelta})
        assert_frame_equal(result, out_df)

    def test_coerce_from_string_to_object_no_na(self):
        in_series = pd.Series(self.timedeltas)
        out_series = in_series.astype(np.dtype("O"))

        # series
        result = coerce_dtypes(in_series, object)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_series})
        out_df = pd.DataFrame({self.col_name: out_series})
        result = coerce_dtypes(in_df, {self.col_name: object})
        assert_frame_equal(result, out_df)

    def test_coerce_from_string_to_object_with_na(self):
        in_series = pd.Series(self.timedeltas + [None])
        out_series = in_series.astype(np.dtype("O"))

        # series
        result = coerce_dtypes(in_series, object)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_series})
        out_df = pd.DataFrame({self.col_name: out_series})
        result = coerce_dtypes(in_df, {self.col_name: object})
        assert_frame_equal(result, out_df)


class CoerceBooleanDtypeTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        size = 3
        cls.booleans = [bool((i + 1) % 2) for i in range(size)]
        # ^ = [True, False, True, False, ...]
        cls.col_name = "booleans"

    def test_coerce_from_boolean_to_integer_no_na(self):
        in_data = self.booleans
        out_data = [int(b) for b in self.booleans]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, int)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: int})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_integer_with_na(self):
        in_data = self.booleans + [None]
        out_data = [int(b) for b in self.booleans] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, int)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: int})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_float_no_na(self):
        in_data = self.booleans
        out_data = [float(b) for b in self.booleans]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, float)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: float})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_float_with_na(self):
        in_data = self.booleans + [None]
        out_data = [float(b) for b in self.booleans] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, float)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: float})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_complex_no_na(self):
        in_data = self.booleans
        out_data = [complex(b, 0) for b in self.booleans]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, complex)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: complex})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_complex_with_na(self):
        in_data = self.booleans + [None]
        out_data = [complex(b, 0) for b in self.booleans] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, complex)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: complex})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_string_no_na(self):
        in_data = self.booleans
        out_data = [str(b) for b in self.booleans]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, str)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: str})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_string_with_na(self):
        in_data = self.booleans + [None]
        out_data = [str(b) for b in self.booleans] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, str)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: str})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_boolean_no_na(self):
        in_data = self.booleans
        out_data = in_data.copy()

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, bool)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: bool})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_boolean_with_na(self):
        in_data = self.booleans + [None]
        out_data = in_data.copy()

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, bool)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: bool})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_datetime_no_na(self):
        in_data = self.booleans
        out_data = [datetime.fromtimestamp(b, tz=timezone.utc)
                    for b in self.booleans]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_datetime_with_na(self):
        in_data = self.booleans + [None]
        out_data = [datetime.fromtimestamp(b, tz=timezone.utc)
                    for b in self.booleans] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_timedelta_no_na(self):
        in_data = self.booleans
        out_data = [timedelta(seconds=b) for b in self.booleans]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, timedelta)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: timedelta})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_timedelta_with_na(self):
        in_data = self.booleans + [None]
        out_data = [timedelta(seconds=b) for b in self.booleans] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, timedelta)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: timedelta})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_object_no_na(self):
        in_series = pd.Series(self.booleans)
        out_series = in_series.astype(np.dtype("O"))

        # series
        result = coerce_dtypes(in_series, object)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_series})
        out_df = pd.DataFrame({self.col_name: in_series})
        result = coerce_dtypes(in_df, {self.col_name: object})
        assert_frame_equal(result, out_df)

    def test_coerce_from_boolean_to_object_with_na(self):
        in_series = pd.Series(self.booleans + [None])
        out_series = in_series.astype(np.dtype("O"))

        # series
        result = coerce_dtypes(in_series, object)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_series})
        out_df = pd.DataFrame({self.col_name: in_series})
        result = coerce_dtypes(in_df, {self.col_name: object})
        assert_frame_equal(result, out_df)


class CoerceDatetimeDtypeTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        random.seed(12345)
        size = 3
        integers = [-1 * size // 2 + i + 1 for i in range(size)]
        floats = [i + random.random() for i in integers]
        cls.whole_datetimes = [datetime.fromtimestamp(i, tz=timezone.utc)
                               for i in integers]
        # ^ = [..., utc time -1, utc time 0, utc time 1, ...]
        cls.datetimes_between_0_and_1 = [datetime.fromtimestamp(random.random(),
                                                                tz=timezone.utc)
                                         for _ in range(size)]
        # ^ = [utc time 0+e, utc time 0+e, utc time 0+e, ...]
        cls.bool_flags = [datetime.fromtimestamp((i + 1) % 2, tz=timezone.utc)
                          for i in range(size)]
        # ^ = [utc time 1, utc time 0, utc time 1, utc time 0, ...]
        cls.naive_datetimes = [datetime.utcfromtimestamp(f) for f in floats]
        # ^ = [..., utc time -1+e, utc time 0+e, utc time 1+e, ...] (no tz)
        cls.aware_datetimes = [datetime.fromtimestamp(f, tz=timezone.utc)
                               for f in floats]
        # ^ = [..., utc time -1+e, utc time 0+e, utc_time 1+e, ...] (with tz)
        cls.aware_naive_datetimes = []
        for index, f in enumerate(floats):
            if index % 2:  # naive
                cls.aware_naive_datetimes.append(datetime.utcfromtimestamp(f))
            else:  # aware
                val = datetime.fromtimestamp(f, tz=timezone.utc)
                cls.aware_naive_datetimes.append(val)
        # ^ = [aware, naive, aware, naive, aware, ...]
        cls.mixed_timezones = []
        for index, f in enumerate(floats):
            tz_name = pytz.all_timezones[index % len(pytz.all_timezones)]
            tz = pytz.timezone(tz_name)
            val = datetime.fromtimestamp(f, tz=tz)
            cls.mixed_timezones.append(val)
        # ^ = ["Africa/Abidjan", "Africa/Accra", "Africa/Addis_Ababa", ...]
        cls.col_name = "datetimes"

    def test_coerce_from_whole_datetime_to_integer_no_na(self):
        in_data = self.whole_datetimes
        out_data = [int(d.timestamp()) for d in self.whole_datetimes]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, int)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: int})
        assert_frame_equal(result, out_df)

    def test_coerce_from_whole_datetime_to_integer_with_na(self):
        in_data = self.whole_datetimes + [None]
        out_data = [int(d.timestamp()) for d in self.whole_datetimes] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, int)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: int})
        assert_frame_equal(result, out_df)

    def test_coerce_from_random_datetime_to_integer_no_na(self):
        in_data = self.aware_datetimes

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {int} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, int)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {int} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: int})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_random_datetime_to_integer_with_na(self):
        in_data = self.aware_datetimes + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {int} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, int)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {int} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: int})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_datetime_to_float_no_na(self):
        in_data = self.aware_datetimes
        out_data = [d.timestamp() for d in self.aware_datetimes]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, float)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: float})
        assert_frame_equal(result, out_df)

    def test_coerce_datetime_to_float_with_na(self):
        in_data = self.aware_datetimes + [None]
        out_data = [d.timestamp() for d in self.aware_datetimes] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, float)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: float})
        assert_frame_equal(result, out_df)

    def test_coerce_from_datetime_to_complex_no_na(self):
        in_data = self.aware_datetimes
        out_data = [complex(d.timestamp(), 0) for d in self.aware_datetimes]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, complex)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: complex})
        assert_frame_equal(result, out_df)

    def test_coerce_from_datetime_to_complex_with_na(self):
        in_data = self.aware_datetimes + [None]
        out_data = ([complex(d.timestamp(), 0) for d in self.aware_datetimes] +
                    [None])

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, complex)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: complex})
        assert_frame_equal(result, out_df)

    def test_coerce_from_datetime_to_string_no_na(self):
        in_data = self.aware_datetimes
        out_data = [d.isoformat() for d in self.aware_datetimes]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, str)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: str})
        assert_frame_equal(result, out_df)

    def test_coerce_from_datetime_to_string_with_na(self):
        in_data = self.aware_datetimes + [None]
        out_data = [d.isoformat() for d in self.aware_datetimes] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, str)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: str})
        assert_frame_equal(result, out_df)

    def test_coerce_from_datetime_bool_flag_to_boolean_no_na(self):
        in_data = self.bool_flags
        out_data = [bool(d.timestamp()) for d in self.bool_flags]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, bool)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: bool})
        assert_frame_equal(result, out_df)

    def test_coerce_from_datetime_bool_flag_to_boolean_with_na(self):
        in_data = self.bool_flags + [None]
        out_data = [bool(d.timestamp()) for d in self.bool_flags] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, bool)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: bool})
        assert_frame_equal(result, out_df)

    def test_coerce_from_random_datetime_to_boolean_no_na(self):
        in_data = self.aware_datetimes

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_random_datetime_to_boolean_with_na(self):
        in_data = self.aware_datetimes + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_datetime_between_0_and_1_to_boolean_no_na(self):
        in_data = self.datetimes_between_0_and_1

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_datetime_between_0_and_1_to_boolean_with_na(self):
        in_data = self.datetimes_between_0_and_1 + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_datetime_to_datetime_no_na(self):
        in_data = self.aware_datetimes
        out_data = in_data.copy()

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_datetime_to_datetime_with_na(self):
        in_data = self.aware_datetimes + [None]
        out_data = in_data.copy()

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_datetime_to_timedelta_no_na(self):
        in_data = self.aware_datetimes
        out_data = [timedelta(seconds=d.timestamp())
                    for d in self.aware_datetimes]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, timedelta)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: timedelta})
        assert_frame_equal(result, out_df)

    def test_coerce_from_datetime_to_timedelta_with_na(self):
        in_data = self.aware_datetimes + [None]
        out_data = [timedelta(seconds=d.timestamp())
                    for d in self.aware_datetimes] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, timedelta)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: timedelta})
        assert_frame_equal(result, out_df)

    def test_coerce_from_datetime_to_object_no_na(self):
        in_series = pd.Series(self.aware_datetimes)
        out_series = in_series.astype(np.dtype("O"))

        # series
        result = coerce_dtypes(in_series, object)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_series})
        out_df = pd.DataFrame({self.col_name: out_series})
        result = coerce_dtypes(in_df, {self.col_name: object})
        assert_frame_equal(result, out_df)

    def test_coerce_from_datetime_to_object_with_na(self):
        in_series = pd.Series(self.aware_datetimes + [None])
        out_series = in_series.astype(np.dtype("O"))

        # series
        result = coerce_dtypes(in_series, object)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_series})
        out_df = pd.DataFrame({self.col_name: out_series})
        result = coerce_dtypes(in_df, {self.col_name: object})
        assert_frame_equal(result, out_df)


class CoerceTimedeltaDtypeTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        random.seed(12345)
        size = 3
        integers = [-1 * size // 2 + i + 1 for i in range(size)]
        floats = [i + random.random() for i in integers]
        cls.whole_timedeltas = [timedelta(seconds=i) for i in integers]
        # ^ = [..., timedelta(-1), timedelta(0), timedelta(1), ...]
        cls.timedeltas = [timedelta(seconds=f) for f in floats]
        # ^ = [..., timedelta(-1+e), timedelta(0+e), timedelta(1+e), ...]
        cls.timedeltas_between_0_and_1 = [timedelta(seconds=random.random())
                                          for _ in range(size)]
        # ^ = [timedelta(0+e), timedelta(0+e), timedelta(0+e), ...]
        cls.bool_flags = [timedelta(seconds=(i + 1) % 2) for i in range(size)]
        # ^ = [timedelta(1), timedelta(0), timedelta(1), timedelta(0), ...]
        cls.col_name = "timedeltas"

    def test_coerce_from_whole_timedelta_to_integer_no_na(self):
        in_data = self.whole_timedeltas
        out_data = [int(t.total_seconds()) for t in self.whole_timedeltas]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, int)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: int})
        assert_frame_equal(result, out_df)

    def test_coerce_from_whole_timedelta_to_integer_with_na(self):
        in_data = self.whole_timedeltas + [None]
        out_data = ([int(t.total_seconds()) for t in self.whole_timedeltas] +
                    [None])

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, int)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: int})
        assert_frame_equal(result, out_df)

    def test_coerce_from_random_timedelta_to_integer_no_na(self):
        in_data = self.timedeltas

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {int} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, int)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {int} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: int})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_random_timedelta_to_integer_with_na(self):
        in_data = self.timedeltas + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {int} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, int)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {int} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: int})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_timedelta_to_float_no_na(self):
        in_data = self.timedeltas
        out_data = [t.total_seconds() for t in self.timedeltas]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, float)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: float})
        assert_frame_equal(result, out_df)

    def test_coerce_from_timedelta_to_float_with_na(self):
        in_data = self.timedeltas + [None]
        out_data = [t.total_seconds() for t in self.timedeltas] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, float)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: float})
        assert_frame_equal(result, out_df)

    def test_coerce_from_timedelta_to_complex_no_na(self):
        in_data = self.timedeltas
        out_data = [complex(t.total_seconds(), 0) for t in self.timedeltas]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, complex)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: complex})
        assert_frame_equal(result, out_df)

    def test_coerce_from_timedelta_to_complex_with_na(self):
        in_data = self.timedeltas + [None]
        out_data = ([complex(t.total_seconds(), 0) for t in self.timedeltas] +
                    [None])

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, complex)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: complex})
        assert_frame_equal(result, out_df)

    def test_coerce_from_timedelta_to_string_no_na(self):
        in_data = self.timedeltas
        out_data = [str(pd.Timedelta(t)) for t in self.timedeltas]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, str)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: str})
        assert_frame_equal(result, out_df)

    def test_coerce_from_timedelta_to_string_with_na(self):
        in_data = self.timedeltas + [None]
        out_data = [str(pd.Timedelta(t)) for t in self.timedeltas] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, str)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: str})
        assert_frame_equal(result, out_df)

    def test_coerce_from_timedelta_bool_flag_to_boolean_no_na(self):
        in_data = self.bool_flags
        out_data = [bool(d.total_seconds()) for d in self.bool_flags]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, bool)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: bool})
        assert_frame_equal(result, out_df)

    def test_coerce_from_timedelta_bool_flag_to_boolean_with_na(self):
        in_data = self.bool_flags + [None]
        out_data = [bool(d.total_seconds()) for d in self.bool_flags] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, bool)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: bool})
        assert_frame_equal(result, out_df)

    def test_coerce_from_random_timedelta_to_boolean_no_na(self):
        in_data = self.timedeltas

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_random_timedelta_to_boolean_with_na(self):
        in_data = self.timedeltas + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_timedelta_between_0_and_1_to_boolean_no_na(self):
        in_data = self.timedeltas_between_0_and_1

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_timedelta_between_0_and_1_to_boolean_with_na(self):
        in_data = self.timedeltas_between_0_and_1 + [None]

        # series
        in_series = pd.Series(in_data)
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce series "
                   f"values to {bool} without losing information (head: "
                   f"{list(in_series.head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_series, bool)
        self.assertEqual(str(err.exception), err_msg)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        err_msg = (f"[datatube.dtype.coerce_dtypes] cannot coerce column "
                   f"{repr(self.col_name)} to {bool} without losing "
                   f"information (head: {list(in_df[self.col_name].head())})")
        with self.assertRaises(ValueError) as err:
            coerce_dtypes(in_df, {self.col_name: bool})
        self.assertEqual(str(err.exception), err_msg)

    def test_coerce_from_timedelta_to_datetime_no_na(self):
        in_data = self.timedeltas
        out_data = [datetime.fromtimestamp(t.total_seconds(), tz=timezone.utc)
                    for t in self.timedeltas]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_timedelta_to_datetime_with_na(self):
        in_data = self.timedeltas + [None]
        out_data = [datetime.fromtimestamp(t.total_seconds(), tz=timezone.utc)
                    for t in self.timedeltas] + [None]

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, datetime)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: datetime})
        assert_frame_equal(result, out_df)

    def test_coerce_from_timedelta_to_timedelta_no_na(self):
        in_data = self.timedeltas
        out_data = in_data.copy()

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, timedelta)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: timedelta})
        assert_frame_equal(result, out_df)

    def test_coerce_from_timedelta_to_timedelta_with_na(self):
        in_data = self.timedeltas + [None]
        out_data = in_data.copy()

        # series
        in_series = pd.Series(in_data)
        out_series = pd.Series(out_data)
        result = coerce_dtypes(in_series, timedelta)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_data})
        out_df = pd.DataFrame({self.col_name: out_data})
        result = coerce_dtypes(in_df, {self.col_name: timedelta})
        assert_frame_equal(result, out_df)

    def test_coerce_from_timedelta_to_object_no_na(self):
        in_series = pd.Series(self.timedeltas)
        out_series = in_series.astype(np.dtype("O"))

        # series
        result = coerce_dtypes(in_series, object)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_series})
        out_df = pd.DataFrame({self.col_name: out_series})
        result = coerce_dtypes(in_df, {self.col_name: object})
        assert_frame_equal(result, out_df)

    def test_coerce_from_timedelta_to_object_with_na(self):
        in_series = pd.Series(self.timedeltas + [None])
        out_series = in_series.astype(np.dtype("O"))

        # series
        result = coerce_dtypes(in_series, object)
        assert_series_equal(result, out_series)

        # dataframe
        in_df = pd.DataFrame({self.col_name: in_series})
        out_df = pd.DataFrame({self.col_name: out_series})
        result = coerce_dtypes(in_df, {self.col_name: object})
        assert_frame_equal(result, out_df)


class CoerceObjectDtypeTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        class NonCastableObject:
            pass

        class CastableObject:
            
            def to_datetime(self) -> datetime:
                return datetime.fromtimestamp(random.randint(0, 86400),
                                              tz=timezone.utc)

            def to_timedelta(self) -> timedelta:
                return timedelta(seconds=random.randint(0, 86400))
            
            def __int__(self) -> int:
                return random.randint(0, 10)

            def __float__(self) -> float:
                return random.random()

            def __complex__(self) -> complex:
                return complex(random.random(), random.random())

            def __str__(self) -> str:
                return chr(random.randint(0, 26) + ord("a"))

            def __bool__(self) -> bool:
                return bool(random.randint(0, 1))

        size = 3
        cls.non_castable_objects = [NonCastableObject() for _ in range(size)]
        cls.castable_objects = [CastableObject() for _ in range(size)]
        cls.nones = [None for _ in range(size)]
        cls.col_name = "objects"

    def test_coerce_from_object_to_integer(self):
        pass
        # raise NotImplementedError()

    def test_coerce_from_object_to_float(self):
        pass
        # raise NotImplementedError()

    def test_coerce_from_object_to_complex(self):
        pass
        # raise NotImplementedError()

    def test_coerce_from_object_to_string(self):
        pass
        # raise NotImplementedError()

    def test_coerce_from_object_to_boolean(self):
        pass
        # raise NotImplementedError()

    def test_coerce_from_object_to_datetime(self):
        pass
        # raise NotImplementedError()

    def test_coerce_from_object_to_timedelta(self):
        pass
        # raise NotImplementedError()

    def test_coerce_from_object_to_object(self):
        pass
        # raise NotImplementedError()



    # def test_check_dtypes_datetime_mixed_timezones(self):
    #     test_df = pd.DataFrame({"timestamp": [datetime.now(timezone.utc),
    #                                           datetime.now()]})
    #     self.assertTrue(check_dtypes(test_df, timestamp=datetime))

    # def test_coerce_dtypes_kwargless_error(self):
    #     atomics = [t.__name__ if isinstance(t, type) else str(t)
    #                for t in AVAILABLE_DTYPES]
    #     err_msg = (f"[datatube.stats.coerce_dtypes] `coerce_dtypes` must be "
    #                f"invoked with at least one keyword argument mapping a "
    #                f"column in `data` to an atomic data type: "
    #                f"{tuple(atomics)}")
    #     with self.assertRaises(RuntimeError) as err:
    #         coerce_dtypes(self.no_na)
    #     self.assertEqual(str(err.exception), err_msg)

    # def test_coerce_dtypes_kwargs_no_na_no_errors(self):
    #     for col_name, expected in self.conversions.items():
    #         for conv in expected:
    #             coerce_dtypes(self.no_na, **{col_name: conv})

    # def test_coerce_dtypes_kwargs_with_na_no_errors(self):
    #     for col_name, expected in self.conversions.items():
    #         for conv in expected:
    #             coerce_dtypes(self.with_na, **{col_name: conv})

    # def test_coerce_dtypes_matches_check_dtypes(self):
    #     # This does not work for coercion to <class 'object'> because of the
    #     # automatic convert_dtypes() step of check_dtypes.  These columns will
    #     # always be better represented by some other data type, unless it was
    #     # an object to begin with.
    #     for col_name, expected in self.conversions.items():
    #         for conv in expected:
    #             result = coerce_dtypes(self.no_na, **{col_name: conv})
    #             na_result = coerce_dtypes(self.with_na, **{col_name: conv})
    #             check_result = check_dtypes(result, **{col_name: conv})
    #             check_na_result = check_dtypes(na_result, **{col_name: conv})
    #             if conv != object:
    #                 try:
    #                     self.assertTrue(check_result)
    #                     self.assertTrue(check_na_result)
    #                 except AssertionError as exc:
    #                     err_msg = (f"col_name: {repr(col_name)}, typespec: "
    #                             f"{conv}, expected: {expected}")
    #                     raise AssertionError(err_msg) from exc

    # def test_coerce_dtypes_returns_copy(self):
    #     result = coerce_dtypes(self.with_na, a=float)
    #     self.assertNotEqual(list(result.dtypes), list(self.with_na.dtypes))

    # def test_coerce_dtypes_datetime_preserves_timezone(self):
    #     raise NotImplementedError()


if __name__ == "__main__":
    unittest.main()