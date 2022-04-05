from datetime import datetime, timedelta, timezone
import random
import unittest

import pandas as pd
import pytz

if __name__ == "__main__":
    from pathlib import Path
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from datatube.dtype import check_dtype


class TestObj:
    pass


unittest.TestCase.maxDiff = None
SIZE = 3
TEST_DATA = {
    int: {
        "integers":
            [-1 * SIZE // 2 + i + 1 for i in range(SIZE)],
        "whole floats":
            [-1 * SIZE // 2 + i + 1.0 for i in range(SIZE)],
        "real whole complex":
            [complex(-1 * SIZE // 2 + i + 1, 0) for i in range(SIZE)],
    },
    float: {
        "decimal floats":
            [-1 * SIZE // 2 + i + 1 + random.random() for i in range(SIZE)],
        "real decimal complex":
            [complex(-1 * SIZE // 2 + i + 1 + random.random(), 0)
             for i in range(SIZE)],
    },
    complex: {
        "imaginary complex":
            [complex(-1 * SIZE // 2 + i + 1 + random.random(),
                     -1 * SIZE // 2 + i + 1 + random.random())
             for i in range(SIZE)],
    },
    str: {
        "integer strings":
            [str(-1 * SIZE // 2 + i + 1) for i in range(SIZE)],
        "whole float strings":
            [str(-1 * SIZE // 2 + i + 1.0) for i in range(SIZE)],
        "decimal float strings":
            [str(-1 * SIZE // 2 + i + 1 + random.random())
             for i in range(SIZE)],
        "real whole complex strings":
            [str(complex(-1 * SIZE // 2 + i + 1, 0)) for i in range(SIZE)],
        "real decimal complex strings":
            [str(complex(-1 * SIZE // 2 + i + 1 + random.random(), 0))
             for i in range(SIZE)],
        "imaginary complex strings":
            [str(complex(-1 * SIZE // 2 + i + 1 + random.random(),
                         -1 * SIZE // 2 + i + 1 + random.random()))
             for i in range(SIZE)],
        "character strings":
            [chr(i % 26 + ord("a")) for i in range(SIZE)],
        "boolean strings":
            [str(bool((i + 1) % 2)) for i in range(SIZE)],
        "aware datetime strings":
            [str(datetime.fromtimestamp(i, tz=timezone.utc))
             for i in range(SIZE)],
        "aware ISO 8601 strings":
            [datetime.fromtimestamp(i, tz=timezone.utc).isoformat()
             for i in range(SIZE)],
        "naive datetime strings":
            [str(datetime.fromtimestamp(i)) for i in range(SIZE)],
        "naive ISO 8601 strings":
            [datetime.fromtimestamp(i).isoformat() for i in range(SIZE)],
        "aware/naive datetime strings":
            [str(datetime.fromtimestamp(i, tz=timezone.utc)) if i % 2
             else str(datetime.fromtimestamp(i)) for i in range(SIZE)],
        "aware/naive ISO 8601 strings":
            [datetime.fromtimestamp(i, tz=timezone.utc).isoformat() if i % 2
             else datetime.fromtimestamp(i).isoformat()
             for i in range(SIZE)],
        "mixed timezone datetime strings":
            [str(
                datetime.fromtimestamp(
                    i,
                    tz=pytz.timezone(
                        pytz.all_timezones[i % len(pytz.all_timezones)]
                    )
                )
             ) for i in range(SIZE)],
        "mixed timezone ISO 8601 strings":
            [datetime.fromtimestamp(
                i,
                tz=pytz.timezone(
                    pytz.all_timezones[i % len(pytz.all_timezones)]
                )
             ).isoformat() for i in range(SIZE)],
        "timedelta strings":
            [str(timedelta(seconds=i + 1)) for i in range(SIZE)],
        "pd.Timedelta strings":
            [str(pd.Timedelta(timedelta(seconds=i + 1))) for i in range(SIZE)]
    },
    bool: {
       "booleans":
            [bool((i + 1) % 2) for i in range(SIZE)] 
    },
    datetime: {
        "aware datetimes":
            [datetime.fromtimestamp(i, tz=timezone.utc) for i in range(SIZE)],
        "naive datetimes":
            [datetime.fromtimestamp(i) for i in range(SIZE)],
        "aware/naive datetimes":
            [datetime.fromtimestamp(i, tz=timezone.utc) if i % 2
             else datetime.fromtimestamp(i) for i in range(SIZE)],
        "mixed timezone datetimes":
            [datetime.fromtimestamp(
                i,
                tz = pytz.timezone(
                    pytz.all_timezones[i % len(pytz.all_timezones)]
                )
             ) for i in range(SIZE)]
    },
    timedelta: {
        "timedeltas":
            [timedelta(seconds=i + 1) for i in range(SIZE)]
    },
    object: {
        "Nones":
            [None for _ in range(SIZE)],
        "custom objects":
            [TestObj() for _ in range(SIZE)]
    }
}
ALL_DATA = {col_name: data for v in TEST_DATA.values()
            for col_name, data in v.items()}


class CheckDtypeTests(unittest.TestCase):

    def test_check_integers_series_no_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data)
            result = check_dtype(series, int)
            expected = col_name in TEST_DATA[int]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., int) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_integers_series_with_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data + [None])
            result = check_dtype(series, int)
            expected = col_name in TEST_DATA[int]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., int) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_integers_df_no_na(self):
        df = pd.DataFrame(ALL_DATA)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: int})
            expected = col_name in TEST_DATA[int]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: int}}) != "
                           f"{expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_integers_df_with_na(self):
        with_na = {k: v + [None] for k, v in ALL_DATA.items()}
        df = pd.DataFrame(with_na)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: int})
            expected = col_name in TEST_DATA[int]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: int}}) != "
                           f"{expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_floats_series_no_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data)
            result = check_dtype(series, float)
            expected = col_name in TEST_DATA[float]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., float) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_floats_series_with_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data + [None])
            result = check_dtype(series, float)
            expected = col_name in TEST_DATA[float]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., float) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_floats_df_no_na(self):
        df = pd.DataFrame(ALL_DATA)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: float})
            expected = col_name in TEST_DATA[float]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: float}}) != "
                           f"{expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_floats_df_with_na(self):
        with_na = {k: v + [None] for k, v in ALL_DATA.items()}
        df = pd.DataFrame(with_na)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: float})
            expected = col_name in TEST_DATA[float]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: float}}) != "
                           f"{expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_complex_series_no_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data)
            result = check_dtype(series, complex)
            expected = col_name in TEST_DATA[complex]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., complex) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_complex_series_with_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data + [None])
            result = check_dtype(series, complex)
            expected = col_name in TEST_DATA[complex]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., complex) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_complex_df_no_na(self):
        df = pd.DataFrame(ALL_DATA)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: complex})
            expected = col_name in TEST_DATA[complex]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: complex}}) "
                           f"!= {expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_complex_df_with_na(self):
        with_na = {k: v + [None] for k, v in ALL_DATA.items()}
        df = pd.DataFrame(with_na)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: complex})
            expected = col_name in TEST_DATA[complex]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: complex}}) "
                           f"!= {expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_strings_series_no_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data)
            result = check_dtype(series, str)
            expected = col_name in TEST_DATA[str]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., str) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_strings_series_with_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data + [None])
            result = check_dtype(series, str)
            expected = col_name in TEST_DATA[str]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., str) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_strings_df_no_na(self):
        df = pd.DataFrame(ALL_DATA)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: str})
            expected = col_name in TEST_DATA[str]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: str}}) != "
                           f"{expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_strings_df_with_na(self):
        with_na = {k: v + [None] for k, v in ALL_DATA.items()}
        df = pd.DataFrame(with_na)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: str})
            expected = col_name in TEST_DATA[str]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: str}}) != "
                           f"{expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_booleans_series_no_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data)
            result = check_dtype(series, bool)
            expected = col_name in TEST_DATA[bool]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., bool) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_booleans_series_with_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data + [None])
            result = check_dtype(series, bool)
            expected = col_name in TEST_DATA[bool]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., bool) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_booleans_df_no_na(self):
        df = pd.DataFrame(ALL_DATA)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: bool})
            expected = col_name in TEST_DATA[bool]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: bool}}) != "
                           f"{expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_booleans_df_with_na(self):
        with_na = {k: v + [None] for k, v in ALL_DATA.items()}
        df = pd.DataFrame(with_na)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: bool})
            expected = col_name in TEST_DATA[bool]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: bool}}) != "
                           f"{expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_datetimes_series_no_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data)
            result = check_dtype(series, datetime)
            expected = col_name in TEST_DATA[datetime]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., datetime) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_datetimes_series_with_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data + [None])
            result = check_dtype(series, datetime)
            expected = col_name in TEST_DATA[datetime]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., datetime) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_datetimes_df_no_na(self):
        df = pd.DataFrame(ALL_DATA)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: datetime})
            expected = col_name in TEST_DATA[datetime]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: datetime}}) "
                           f"!= {expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_datetimes_df_with_na(self):
        with_na = {k: v + [None] for k, v in ALL_DATA.items()}
        df = pd.DataFrame(with_na)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: datetime})
            expected = col_name in TEST_DATA[datetime]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: datetime}}) "
                           f"!= {expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_timedeltas_series_no_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data)
            result = check_dtype(series, timedelta)
            expected = col_name in TEST_DATA[timedelta]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., timedelta) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_timedeltas_series_with_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data + [None])
            result = check_dtype(series, timedelta)
            expected = col_name in TEST_DATA[timedelta]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., timedelta) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_timedeltas_df_no_na(self):
        df = pd.DataFrame(ALL_DATA)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: timedelta})
            expected = col_name in TEST_DATA[timedelta]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: timedelta}}) "
                           f"!= {expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_timedeltas_df_with_na(self):
        with_na = {k: v + [None] for k, v in ALL_DATA.items()}
        df = pd.DataFrame(with_na)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: timedelta})
            expected = col_name in TEST_DATA[timedelta]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: timedelta}}) "
                           f"!= {expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_objects_series_no_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data)
            result = check_dtype(series, object)
            expected = col_name in TEST_DATA[object]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., object) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_objects_series_with_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data + [None])
            result = check_dtype(series, object)
            expected = col_name in TEST_DATA[object]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({data[:3]}..., object) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_objects_df_no_na(self):
        df = pd.DataFrame(ALL_DATA)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: object})
            expected = col_name in TEST_DATA[object]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: object}}) != "
                           f"{expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_object_df_with_na(self):
        with_na = {k: v + [None] for k, v in ALL_DATA.items()}
        df = pd.DataFrame(with_na)
        failed = []
        for col_name in df.columns:
            result = check_dtype(df, {col_name: object})
            expected = col_name in TEST_DATA[object]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: object}}) != "
                           f"{expected}")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")


class CheckDtypeArgumentTests(unittest.TestCase):

    def test_check_dtype_series_no_typespec_no_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data)
            result = check_dtype(series)
            lookup = [typespec for typespec, subset in TEST_DATA.items()
                      if col_name in subset]
            self.assertEqual(len(lookup), 1)
            expected = lookup[0]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({list(series.head(2))}) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_dtype_series_no_typespec_with_na(self):
        failed = []
        for col_name, data in ALL_DATA.items():
            series = pd.Series(data + [None])
            result = check_dtype(series)
            lookup = [typespec for typespec, subset in TEST_DATA.items()
                      if col_name in subset]
            self.assertEqual(len(lookup), 1)
            expected = lookup[0]
            try:
                self.assertEqual(result, expected)
            except AssertionError:
                context = f"check_dtype({list(series.head(2))}) != {expected}"
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_dtype_df_no_typespec_no_na(self):
        df = pd.DataFrame(ALL_DATA)
        result = check_dtype(df)
        expected = {}
        for col_name in df.columns:
            lookup = [typespec for typespec, subset in TEST_DATA.items()
                      if col_name in subset]
            self.assertEqual(len(lookup), 1)
            expected[col_name] = lookup[0]
        self.assertEqual(result, expected)

    def test_check_dtype_df_no_typespec_with_na(self):
        with_na = {k: v + [None] for k, v in ALL_DATA.items()}
        df = pd.DataFrame(with_na)
        result = check_dtype(df)
        expected = {}
        for col_name in df.columns:
            lookup = [typespec for typespec, subset in TEST_DATA.items()
                      if col_name in subset]
            self.assertEqual(len(lookup), 1)
            expected[col_name] = lookup[0]
        self.assertEqual(result, expected)

    def test_check_dtype_series_multiple_typespecs_no_na(self):
        failed = []
        all_types = tuple(TEST_DATA)
        types_str = tuple([t.__name__ for t in all_types])
        for data in ALL_DATA.values():
            series = pd.Series(data)
            try:
                self.assertTrue(check_dtype(series, all_types))
            except AssertionError:
                context = (f"check_dtype({list(series.head(2))}, "
                           f"{types_str}) != True")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_dtype_series_multiple_typespecs_with_na(self):
        with_na = {k: v + [None] for k, v in ALL_DATA.items()}
        failed = []
        all_types = tuple(TEST_DATA)
        types_str = tuple([t.__name__ for t in all_types])
        for data in with_na.values():
            series = pd.Series(data)
            try:
                self.assertTrue(check_dtype(series, all_types))
            except AssertionError:
                context = (f"check_dtype({list(series.head(2))}, "
                           f"{types_str}) != True")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_dtype_df_multiple_typespecs_no_na(self):
        df = pd.DataFrame(ALL_DATA)
        failed = []
        all_types = tuple(TEST_DATA)
        types_str = tuple([t.__name__ for t in all_types])
        for col_name in df.columns:
            try:
                self.assertTrue(check_dtype(df, {col_name: all_types}))
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: "
                           f"{types_str}}}) != True")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")

    def test_check_dtype_df_multiple_typespecs_with_na(self):
        with_na = {k: v + [None] for k, v in ALL_DATA.items()}
        df = pd.DataFrame(with_na)
        failed = []
        all_types = tuple(TEST_DATA)
        types_str = tuple([t.__name__ for t in all_types])
        for col_name in df.columns:
            try:
                self.assertTrue(check_dtype(df, {col_name: all_types}))
            except AssertionError:
                context = (f"check_dtype(df, {{{repr(col_name)}: "
                           f"{types_str}}}) != True")
                failed.append(context)
        if len(failed) > 0:
            joined = "\n\t".join(failed)
            raise AssertionError(f"{len(failed)} failed checks:\n\t{joined}")


class CheckDtypeErrorTests(unittest.TestCase):

    def test_check_dtype_series_bad_typespec_type(self):
        series = pd.Series([1, 2, 3])
        with self.assertRaises(TypeError) as err:
            check_dtype(series, {"shouldn't be a dictionary": int})
        err_msg = ("[datatube.dtype.check_dtype] when used on a series, "
                   "`typespec` must be an atomic data type, sequence of atomic "
                   "data types, or None (received object of type: <class "
                   "'dict'>)")
        self.assertEqual(str(err.exception), err_msg)

    def test_check_dtype_df_bad_typespec_type(self):
        df = pd.DataFrame({"column": [1, 2, 3]})
        with self.assertRaises(TypeError) as err:
            check_dtype(df, "bad typespec")
        err_msg = ("[datatube.dtype.check_dtype] when used on a dataframe, "
                   "`typespec` must be an atomic data type, sequence of atomic "
                   "data types, map of column names and atomic data types, or "
                   "None (received object of type: <class 'str'>)")
        self.assertEqual(str(err.exception), err_msg)

    def test_check_dtype_bad_data_type(self):
        data = [1, 2, 3]
        with self.assertRaises(TypeError) as err:
            check_dtype(data, int)
        err_msg = ("[datatube.dtype.check_dtype] `data` must be either a "
                    "pandas.Series or pandas.DataFrame instance (received "
                    "object of type: <class 'list'>)")
        self.assertEqual(str(err.exception), err_msg)


if __name__ == "__main__":
    unittest.main()