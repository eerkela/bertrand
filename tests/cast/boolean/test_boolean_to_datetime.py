import datetime
import itertools
import zoneinfo

import dateutil
import numpy as np
import pandas as pd
import pytest
import pytz

from tests import make_parameters
from tests.cast.datetime import interpret_iso_8601_string

from pdtypes.cast.boolean import BooleanSeries


# TODO: drop NA tests for unit, timezone, epoch?
# -> consolidate into a single test case in each category?


# TODO: move all tests to (kwargs, test_input, test_output) model
# -> tests should accept (target_dtype, kwargs, test_input, test_output)

# TODO: test errors
# - incorrect dtype (default for all conversions)
# - incorrect unit
# - incorrect timezone
# - incorrect epoch

# NOTE: pd.Timestamp objects do not currently support zoneinfo.ZoneInfo
# tzinfo objects.  Official support will be added soon.


####################
####    DATA    ####
####################


def valid_input(expected_dtype):
    interpret = lambda x: interpret_iso_8601_string(x, expected_dtype)
    series_type = None if expected_dtype is pd.Timestamp else "O"

    true = interpret("1970-01-01 00:00:00.000001")
    false = interpret("1970-01-01 00:00:00.000000")

    return [
        (True, pd.Series([true], dtype=series_type)),
        (False, pd.Series([false], dtype=series_type)),
        ([True, False], pd.Series([true, false], dtype=series_type)),
        ((False, True), pd.Series([false, true], dtype=series_type)),
        ((x for x in [True, False, None]), pd.Series([true, false, pd.NaT], dtype=series_type)),
        ([True, False, pd.NA], pd.Series([true, false, pd.NaT], dtype=series_type)),
        ([True, False, np.nan], pd.Series([true, false, pd.NaT], dtype=series_type)),

        # array
        (np.array(True), pd.Series([true], dtype=series_type)),
        (np.array([True, False]), pd.Series([true, false], dtype=series_type)),
        (np.array([True, False], dtype="O"), pd.Series([true, false], dtype=series_type)),
        (np.array([True, False, None], dtype="O"), pd.Series([true, false, pd.NaT], dtype=series_type)),
        (np.array([True, False, pd.NA], dtype="O"), pd.Series([true, false, pd.NaT], dtype=series_type)),
        (np.array([True, False, np.nan], dtype="O"), pd.Series([true, false, pd.NaT], dtype=series_type)),

        # series
        (pd.Series(True), pd.Series([true], dtype=series_type)),
        (pd.Series([True, False]), pd.Series([true, false], dtype=series_type)),
        (pd.Series([True, False], dtype="O"), pd.Series([true, false], dtype=series_type)),
        (pd.Series([True, False, None], dtype="O"), pd.Series([true, false, pd.NaT], dtype=series_type)),
        (pd.Series([True, False, pd.NA], dtype="O"), pd.Series([true, false, pd.NaT], dtype=series_type)),
        (pd.Series([True, False, np.nan], dtype="O"), pd.Series([true, false, pd.NaT], dtype=series_type)),
        (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.Series([true, false, pd.NaT], dtype=series_type))
    ]


def unit_input(expected_dtype):
    interpret = lambda x: interpret_iso_8601_string(x, expected_dtype)
    series_type = None if expected_dtype is pd.Timestamp else "O"

    return [  # target unit, test input, test output
        ({"unit": "ns"}, [True, False], pd.Series([
            interpret("1970-01-01 00:00:00.000000001"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "ns"}, [True, False, None], pd.Series([
            interpret("1970-01-01 00:00:00.000000001"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),
        ({"unit": "us"}, [True, False], pd.Series([
            interpret("1970-01-01 00:00:00.000001000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "us"}, [True, False, None], pd.Series([
            interpret("1970-01-01 00:00:00.000001000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),
        ({"unit": "ms"}, [True, False], pd.Series([
            interpret("1970-01-01 00:00:00.001000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "ms"}, [True, False, None], pd.Series([
            interpret("1970-01-01 00:00:00.001000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),
        ({"unit": "s"}, [True, False], pd.Series([
            interpret("1970-01-01 00:00:01.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "s"}, [True, False, None], pd.Series([
            interpret("1970-01-01 00:00:01.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),
        ({"unit": "m"}, [True, False], pd.Series([
            interpret("1970-01-01 00:01:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "m"}, [True, False, None], pd.Series([
            interpret("1970-01-01 00:01:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),
        ({"unit": "h"}, [True, False], pd.Series([
            interpret("1970-01-01 01:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "h"}, [True, False, None], pd.Series([
            interpret("1970-01-01 01:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),
        ({"unit": "D"}, [True, False], pd.Series([
            interpret("1970-01-02 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "D"}, [True, False, None], pd.Series([
            interpret("1970-01-02 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),
        ({"unit": "W"}, [True, False], pd.Series([
            interpret("1970-01-08 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "W"}, [True, False, None], pd.Series([
            interpret("1970-01-08 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),
        ({"unit": "M"}, [True, False], pd.Series([
            interpret("1970-02-01 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "M"}, [True, False, None], pd.Series([
            interpret("1970-02-01 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),
        ({"unit": "Y"}, [True, False], pd.Series([
            interpret("1971-01-01 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "Y"}, [True, False, None], pd.Series([
            interpret("1971-01-01 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),
    ]


def timezone_input(expected_dtype):
    interpret = lambda x, tz: interpret_iso_8601_string(x, expected_dtype, tz)
    series_type = None if expected_dtype is pd.Timestamp else "O"

    return [  # target unit, target timezone, test input, test output
        ({"unit": "us", "tz": None}, [True, False], pd.Series([
            interpret("1970-01-01 00:00:00.000001000", None),
            interpret("1970-01-01 00:00:00.000000000", None)
        ], dtype=series_type)),
        ({"unit": "us", "tz": None}, [True, False, None], pd.Series([
            interpret("1970-01-01 00:00:00.000001000", None),
            interpret("1970-01-01 00:00:00.000000000", None),
            pd.NaT
        ], dtype=series_type)),
        ({"unit": "us", "tz": "US/Pacific"}, [True, False], pd.Series([
            interpret("1969-12-31 16:00:00.000001000", "US/Pacific"),
            interpret("1969-12-31 16:00:00.000000000", "US/Pacific")
        ], dtype=series_type)),
        ({"unit": "us", "tz": "US/Pacific"}, [True, False, None], pd.Series([
            interpret("1969-12-31 16:00:00.000001000", "US/Pacific"),
            interpret("1969-12-31 16:00:00.000000000", "US/Pacific"),
            pd.NaT
        ], dtype=series_type)),
        ({"unit": "us", "tz": "Europe/Berlin"}, [True, False], pd.Series([
            interpret("1970-01-01 01:00:00.000001000", "Europe/Berlin"),
            interpret("1970-01-01 01:00:00.000000000", "Europe/Berlin")
        ], dtype=series_type)),
        ({"unit": "us", "tz": "Europe/Berlin"}, [True, False, None],
        pd.Series([
            interpret("1970-01-01 01:00:00.000001000", "Europe/Berlin"),
            interpret("1970-01-01 01:00:00.000000000", "Europe/Berlin"),
            pd.NaT
        ], dtype=series_type)),
        ({"unit": "us", "tz": pytz.timezone("Asia/Hong_Kong")}, [True, False],
        pd.Series([
            interpret("1970-01-01 08:00:00.000001000", pytz.timezone("Asia/Hong_Kong")),
            interpret("1970-01-01 08:00:00.000000000", pytz.timezone("Asia/Hong_Kong"))
        ], dtype=series_type)),
        ({"unit": "us", "tz": pytz.timezone("Asia/Hong_Kong")}, [True, False, None],
        pd.Series([
            interpret("1970-01-01 08:00:00.000001000", pytz.timezone("Asia/Hong_Kong")),
            interpret("1970-01-01 08:00:00.000000000", pytz.timezone("Asia/Hong_Kong")),
            pd.NaT
        ], dtype=series_type)),
        # ({"unit": "us", "tz": zoneinfo.ZoneInfo("America/Sao_Paulo")}, [True, False],
        # pd.Series([
        #     interpret("1969-12-31 21:00:00.000001000", zoneinfo.ZoneInfo("America/Sao_Paulo")),
        #     interpret("1969-12-31 21:00:00.000000000", zoneinfo.ZoneInfo("America/Sao_Paulo"))
        # ], dtype=series_type)),
        # ({"unit": "us", "tz": zoneinfo.ZoneInfo("America/Sao_Paulo")}, [True, False, None],
        # pd.Series([
        #     interpret("1969-12-31 21:00:00.000001000", zoneinfo.ZoneInfo("America/Sao_Paulo")),
        #     interpret("1969-12-31 21:00:00.000000000", zoneinfo.ZoneInfo("America/Sao_Paulo")),
        #     pd.NaT
        # ], dtype=series_type)),
        ({"unit": "us", "tz": dateutil.tz.gettz("Asia/Istanbul")}, [True, False],
        pd.Series([
            interpret("1970-01-01 02:00:00.000001000", dateutil.tz.gettz("Asia/Istanbul")),
            interpret("1970-01-01 02:00:00.000000000", dateutil.tz.gettz("Asia/Istanbul"))
        ], dtype=series_type)),
        ({"unit": "us", "tz": dateutil.tz.gettz("Asia/Istanbul")}, [True, False, None],
        pd.Series([
            interpret("1970-01-01 02:00:00.000001000", dateutil.tz.gettz("Asia/Istanbul")),
            interpret("1970-01-01 02:00:00.000000000", dateutil.tz.gettz("Asia/Istanbul")),
            pd.NaT
        ], dtype=series_type)),
    ]


def epoch_input(expected_dtype):
    interpret = lambda x: interpret_iso_8601_string(x, expected_dtype)
    series_type = None if expected_dtype is pd.Timestamp else "O"

    return [
        # named epochs
        ({"unit": "us", "since": "UTC"}, [True, False],
        pd.Series([
            interpret("1970-01-01 00:00:00.000001000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "us", "since": "UTC"}, [True, False, None],
        pd.Series([
            interpret("1970-01-01 00:00:00.000001000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),
        ({"unit": "us", "since": "reduced julian"}, [True, False],
        pd.Series([
            interpret("1858-11-16 12:00:00.000001000"),
            interpret("1858-11-16 12:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "us", "since": "reduced julian"}, [True, False, None],
        pd.Series([
            interpret("1858-11-16 12:00:00.000001000"),
            interpret("1858-11-16 12:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),

        # ISO 8601 epochs
        ({"unit": "us", "since": "1941-12-07 08:00:00-1030"}, [True, False],
        pd.Series([
            interpret("1941-12-07 18:30:00.000001000"),
            interpret("1941-12-07 18:30:00.000000000")
        ], dtype=series_type)),
        ({"unit": "us", "since": "1941-12-07 08:00:00-1030"}, [True, False, None],
        pd.Series([
            interpret("1941-12-07 18:30:00.000001000"),
            interpret("1941-12-07 18:30:00.000000000"),
            pd.NaT
        ], dtype=series_type)),

        # parsed epochs
        ({"unit": "us", "since": "November 22, 1963 at 12:30 PM"}, [True, False],
        pd.Series([
            interpret("1963-11-22 12:30:00.000001000"),
            interpret("1963-11-22 12:30:00.000000000")
        ], dtype=series_type)),
        ({"unit": "us", "since": "November 22, 1963 at 12:30 PM"}, [True, False, None],
        pd.Series([
            interpret("1963-11-22 12:30:00.000001000"),
            interpret("1963-11-22 12:30:00.000000000"),
            pd.NaT
        ], dtype=series_type)),

        # pd.Timestamp
        ({"unit": "us", "since": pd.Timestamp("1989-11-09 19:00:00", tz="Europe/Berlin")}, [True, False],
        pd.Series([
            interpret("1989-11-09 18:00:00.000001000"),
            interpret("1989-11-09 18:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "us", "since": pd.Timestamp("1989-11-09 19:00:00", tz="Europe/Berlin")}, [True, False, None],
        pd.Series([
            interpret("1989-11-09 18:00:00.000001000"),
            interpret("1989-11-09 18:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),

        # datetime.datetime
        ({"unit": "us", "since": datetime.datetime.fromisoformat("1918-11-11 11:00:00")}, [True, False],
        pd.Series([
            interpret("1918-11-11 11:00:00.000001000"),
            interpret("1918-11-11 11:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "us", "since": datetime.datetime.fromisoformat("1918-11-11 11:00:00")}, [True, False, None],
        pd.Series([
            interpret("1918-11-11 11:00:00.000001000"),
            interpret("1918-11-11 11:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),

        # np.datetime64
        ({"unit": "us", "since": np.datetime64("1789-07-14 20:00:00")}, [True, False],
        pd.Series([
            interpret("1789-07-14 20:00:00.000001000"),
            interpret("1789-07-14 20:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "us", "since": np.datetime64("1789-07-14 20:00:00")}, [True, False, None],
        pd.Series([
            interpret("1789-07-14 20:00:00.000001000"),
            interpret("1789-07-14 20:00:00.000000000"),
            pd.NaT
        ], dtype=series_type)),

        # irregular month lengths
        ({"unit": "M", "since": "1970-02-01 00:00:00"}, [True, False],
        pd.Series([
            interpret("1970-03-01 00:00:00.000000000"),
            interpret("1970-02-01 00:00:00.000000000")
        ], dtype=series_type)),
        ({"unit": "M", "since": "1968-02-01 00:00:00"}, [True, False],
        pd.Series([
            interpret("1968-03-01 00:00:00.000000000"),
            interpret("1968-02-01 00:00:00.000000000")
        ], dtype=series_type)),

        # leap years
        ({"unit": "Y", "since": "1968-01-01 00:00:00"}, [True, False],
        pd.Series([
            interpret("1969-01-01 00:00:00.000000000"),
            interpret("1968-01-01 00:00:00.000000000")
        ], dtype=series_type)),
    ]


def M8_input():
    return [
        ("M8[ns]", [True, False], pd.Series([
            np.datetime64("1971-01-01 00:00:00.000000000"),
            np.datetime64("1970-01-01 00:00:00.000000000")
        ], dtype="O")),
        ("datetime64[ns]", [True, False, None], pd.Series([
            np.datetime64("1971-01-01 00:00:00.000000000"),
            np.datetime64("1970-01-01 00:00:00.000000000"),
            pd.NaT
        ], dtype="O")),
        ("datetime64[25us]", [True, False], pd.Series([
            np.datetime64("1971-01-01 00:00:00.000000", "25us"),
            np.datetime64("1970-01-01 00:00:00.000000", "25us")
        ], dtype="O")),
        ("M8[25us]", [True, False, None], pd.Series([
            np.datetime64("1971-01-01 00:00:00.000000", "25us"),
            np.datetime64("1970-01-01 00:00:00.000000", "25us"),
            pd.NaT
        ], dtype="O")),
        ("M8[ms]", [True, False], pd.Series([
            np.datetime64("1971-01-01 00:00:00.000"),
            np.datetime64("1970-01-01 00:00:00.000")
        ], dtype="O")),
        ("datetime64[ms]", [True, False, None], pd.Series([
            np.datetime64("1971-01-01 00:00:00.000"),
            np.datetime64("1970-01-01 00:00:00.000"),
            pd.NaT
        ], dtype="O")),
        ("datetime64[10s]", [True, False], pd.Series([
            np.datetime64("1971-01-01 00:00:00", "10s"),
            np.datetime64("1970-01-01 00:00:00", "10s")
        ], dtype="O")),
        ("M8[10s]", [True, False, None], pd.Series([
            np.datetime64("1971-01-01 00:00:00", "10s"),
            np.datetime64("1970-01-01 00:00:00", "10s"),
            pd.NaT
        ], dtype="O")),
        ("M8[60m]", [True, False], pd.Series([
            np.datetime64("1971-01-01 00:00", "60m"),
            np.datetime64("1970-01-01 00:00", "60m")
        ], dtype="O")),
        ("datetime64[m]", [True, False, None], pd.Series([
            np.datetime64("1971-01-01 00:00"),
            np.datetime64("1970-01-01 00:00"),
            pd.NaT
        ], dtype="O")),
        ("datetime64[h]", [True, False], pd.Series([
            np.datetime64("1971-01-01 00"),
            np.datetime64("1970-01-01 00")
        ], dtype="O")),
        ("M8[h]", [True, False, None], pd.Series([
            np.datetime64("1971-01-01 00"),
            np.datetime64("1970-01-01 00"),
            pd.NaT
        ], dtype="O")),
        ("M8[D]", [True, False], pd.Series([
            np.datetime64("1971-01-01"),
            np.datetime64("1970-01-01")
        ], dtype="O")),
        ("datetime64[D]", [True, False, None], pd.Series([
            np.datetime64("1971-01-01"),
            np.datetime64("1970-01-01"),
            pd.NaT
        ], dtype="O")),
        ("datetime64[W]", [True, False], pd.Series([
            np.datetime64("1971-01-01", "W"),
            np.datetime64("1970-01-01", "W")
        ], dtype="O")),
        ("M8[W]", [True, False, None], pd.Series([
            np.datetime64("1971-01-01", "W"),
            np.datetime64("1970-01-01", "W"),
            pd.NaT
        ], dtype="O")),
        ("M8[3M]", [True, False], pd.Series([
            np.datetime64("1971-01", "3M"),
            np.datetime64("1970-01", "3M")
        ], dtype="O")),
        ("datetime64[3M]", [True, False, None], pd.Series([
            np.datetime64("1971-01", "3M"),
            np.datetime64("1970-01", "3M"),
            pd.NaT
        ], dtype="O")),
        ("datetime64[Y]", [True, False], pd.Series([
            np.datetime64("1971"),
            np.datetime64("1970")
        ], dtype="O")),
        ("M8[Y]", [True, False, None], pd.Series([
            np.datetime64("1971"),
            np.datetime64("1970"),
            pd.NaT
        ], dtype="O")),
    ]


def unit_promotion_input():
    return [
        # pd.Timestamp -> datetime.datetime
        ("datetime", "us", "2262-04-11 23:47:16.854774", [True, False],
         pd.Series([
            pd.Timestamp("2262-04-11 23:47:16.854775"),
            pd.Timestamp("2262-04-11 23:47:16.854774")
         ])),
        ("datetime", "us", "2262-04-11 23:47:16.854774", [True, False, None],
         pd.Series([
            pd.Timestamp("2262-04-11 23:47:16.854775"),
            pd.Timestamp("2262-04-11 23:47:16.854774"),
            pd.NaT
         ])),
        ("datetime", "us", "2262-04-11 23:47:16.854775", [True, False],
         pd.Series([
            datetime.datetime.fromisoformat("2262-04-11 23:47:16.854776"),
            datetime.datetime.fromisoformat("2262-04-11 23:47:16.854775")
         ], dtype="O")),
        ("datetime", "us", "2262-04-11 23:47:16.854775", [True, False, None],
         pd.Series([
            datetime.datetime.fromisoformat("2262-04-11 23:47:16.854776"),
            datetime.datetime.fromisoformat("2262-04-11 23:47:16.854775"),
            pd.NaT
         ], dtype="O")),

        # M8[ns] -> M8[us]
        ("M8", "us", "2262-04-11 23:47:16.854774", [True, False],
         pd.Series([
            np.datetime64("2262-04-11 23:47:16.854775000", "ns"),
            np.datetime64("2262-04-11 23:47:16.854774000", "ns")
         ], dtype="O")),
        ("datetime64", "us", "2262-04-11 23:47:16.854774", [True, False, None],
         pd.Series([
            np.datetime64("2262-04-11 23:47:16.854775000", "ns"),
            np.datetime64("2262-04-11 23:47:16.854774000", "ns"),
            pd.NaT
         ], dtype="O")),
        ("datetime64", "us", "2262-04-11 23:47:16.854775", [True, False],
         pd.Series([
            np.datetime64("2262-04-11 23:47:16.854776", "us"),
            np.datetime64("2262-04-11 23:47:16.854775", "us")
         ], dtype="O")),
        ("M8", "us", "2262-04-11 23:47:16.854775", [True, False, None],
         pd.Series([
            np.datetime64("2262-04-11 23:47:16.854776", "us"),
            np.datetime64("2262-04-11 23:47:16.854775", "us"),
            pd.NaT
         ], dtype="O")),

        # datetime.datetime -> M8[us]
        ("datetime", "us", "9999-12-31 23:59:59.999998", [True, False],
         pd.Series([
            datetime.datetime.fromisoformat("9999-12-31 23:59:59.999999"),
            datetime.datetime.fromisoformat("9999-12-31 23:59:59.999998")
         ], dtype="O")),
        ("datetime", "us", "9999-12-31 23:59:59.999998", [True, False, None],
         pd.Series([
            datetime.datetime.fromisoformat("9999-12-31 23:59:59.999999"),
            datetime.datetime.fromisoformat("9999-12-31 23:59:59.999998"),
            pd.NaT
         ], dtype="O")),
        ("datetime", "us", "9999-12-31 23:59:59.999999", [True, False],
         pd.Series([
            np.datetime64("10000-01-01 00:00:00.000000", "us"),
            np.datetime64("9999-12-31 23:59:59.999999", "us")
         ], dtype="O")),
        ("datetime", "us", "9999-12-31 23:59:59.999999", [True, False, None],
         pd.Series([
            np.datetime64("10000-01-01 00:00:00.000000", "us"),
            np.datetime64("9999-12-31 23:59:59.999999", "us"),
            pd.NaT
         ], dtype="O")),

        # M8[us] -> M8[ms]
        ("datetime", "ms", "294247-01-10 04:00:54.774", [True, False],
         pd.Series([
            np.datetime64("294247-01-10 04:00:54.775000", "us"),
            np.datetime64("294247-01-10 04:00:54.774000", "us")
         ], dtype="O")),
        ("M8", "ms", "294247-01-10 04:00:54.774", [True, False, None],
         pd.Series([
            np.datetime64("294247-01-10 04:00:54.775000", "us"),
            np.datetime64("294247-01-10 04:00:54.774000", "us"),
            pd.NaT
         ], dtype="O")),
        ("datetime64", "ms", "294247-01-10 04:00:54.775", [True, False],
         pd.Series([
            np.datetime64("294247-01-10 04:00:54.776", "ms"),
            np.datetime64("294247-01-10 04:00:54.775", "ms")
         ], dtype="O")),
        ("datetime", "ms", "294247-01-10 04:00:54.775", [True, False, None],
         pd.Series([
            np.datetime64("294247-01-10 04:00:54.776", "ms"),
            np.datetime64("294247-01-10 04:00:54.775", "ms"),
            pd.NaT
         ], dtype="O")),

        # M8[ms] -> M8[s]
        ("datetime", "s", "292278994-08-17 07:12:54", [True, False],
         pd.Series([
            np.datetime64("292278994-08-17 07:12:55.000", "ms"),
            np.datetime64("292278994-08-17 07:12:54.000", "ms")
         ], dtype="O")),
        ("M8", "s", "292278994-08-17 07:12:54", [True, False, None],
         pd.Series([
            np.datetime64("292278994-08-17 07:12:55.000", "ms"),
            np.datetime64("292278994-08-17 07:12:54.000", "ms"),
            pd.NaT
         ], dtype="O")),
        ("datetime64", "s", "292278994-08-17 07:12:55", [True, False],
         pd.Series([
            np.datetime64("292278994-08-17 07:12:56", "s"),
            np.datetime64("292278994-08-17 07:12:55", "s")
         ], dtype="O")),
        ("datetime", "s", "292278994-08-17 07:12:55", [True, False, None],
         pd.Series([
            np.datetime64("292278994-08-17 07:12:56", "s"),
            np.datetime64("292278994-08-17 07:12:55", "s"),
            pd.NaT
         ], dtype="O")),

        # M8[s] -> M8[m]
        ("datetime64", "m", "292277026596-12-04 15:29", [True, False],
         pd.Series([
            np.datetime64("292277026596-12-04 15:30:00", "s"),
            np.datetime64("292277026596-12-04 15:29:00", "s")
         ], dtype="O")),
        ("datetime", "m", "292277026596-12-04 15:29", [True, False, None],
         pd.Series([
            np.datetime64("292277026596-12-04 15:30:00", "s"),
            np.datetime64("292277026596-12-04 15:29:00", "s"),
            pd.NaT
         ], dtype="O")),
        ("datetime", "m", "292277026596-12-04 15:30", [True, False],
         pd.Series([
            np.datetime64("292277026596-12-04 15:31", "m"),
            np.datetime64("292277026596-12-04 15:30", "m")
         ], dtype="O")),
        ("M8", "m", "292277026596-12-04 15:30", [True, False, None],
         pd.Series([
            np.datetime64("292277026596-12-04 15:31", "m"),
            np.datetime64("292277026596-12-04 15:30", "m"),
            pd.NaT
         ], dtype="O")),

        # M8[m] -> M8[h]
        ("M8", "h", "17536621479585-08-30 17:00", [True, False],
         pd.Series([
            np.datetime64("17536621479585-08-30 18:00", "m"),
            np.datetime64("17536621479585-08-30 17:00", "m")
         ], dtype="O")),
        ("datetime", "h", "17536621479585-08-30 17:00", [True, False, None],
         pd.Series([
            np.datetime64("17536621479585-08-30 18:00", "m"),
            np.datetime64("17536621479585-08-30 17:00", "m"),
            pd.NaT
         ], dtype="O")),
        ("datetime64", "h", "17536621479585-08-30 18:00", [True, False],
         pd.Series([
            np.datetime64("17536621479585-08-30 19", "h"),
            np.datetime64("17536621479585-08-30 18", "h")
         ], dtype="O")),
        ("datetime", "h", "17536621479585-08-30 18:00", [True, False, None],
         pd.Series([
            np.datetime64("17536621479585-08-30 19", "h"),
            np.datetime64("17536621479585-08-30 18", "h"),
            pd.NaT
         ], dtype="O")),

        # M8[h] -> M8[D]
        ("datetime", "D", "1052197288658909-10-09", [True, False],
         pd.Series([
            np.datetime64("1052197288658909-10-10 00:00", "h"),
            np.datetime64("1052197288658909-10-09 00:00", "h")
         ], dtype="O")),
        ("datetime64", "D", "1052197288658909-10-09", [True, False, None],
         pd.Series([
            np.datetime64("1052197288658909-10-10 00:00", "h"),
            np.datetime64("1052197288658909-10-09 00:00", "h"),
            pd.NaT
         ], dtype="O")),
        ("datetime", "D", "1052197288658909-10-10", [True, False],
         pd.Series([
            np.datetime64("1052197288658909-10-11", "D"),
            np.datetime64("1052197288658909-10-10", "D")
         ], dtype="O")),
        ("datetime64", "D", "1052197288658909-10-10", [True, False, None],
         pd.Series([
            np.datetime64("1052197288658909-10-11", "D"),
            np.datetime64("1052197288658909-10-10", "D"),
            pd.NaT
         ], dtype="O")),

        # M8[D] -> M8[M]
        ("M8", "M", "25252734927768524-06", [True, False],
         pd.Series([
            np.datetime64("25252734927768524-07-01", "D"),
            np.datetime64("25252734927768524-06-01", "D")
         ], dtype="O")),
        ("datetime", "M", "25252734927768524-06", [True, False, None],
         pd.Series([
            np.datetime64("25252734927768524-07-01", "D"),
            np.datetime64("25252734927768524-06-01", "D"),
            pd.NaT
         ], dtype="O")),
        ("datetime", "M", "25252734927768524-07", [True, False],
         pd.Series([
            np.datetime64("25252734927768524-08", "M"),
            np.datetime64("25252734927768524-07", "M")
         ], dtype="O")),
        ("datetime64", "M", "25252734927768524-07", [True, False, None],
         pd.Series([
            np.datetime64("25252734927768524-08", "M"),
            np.datetime64("25252734927768524-07", "M"),
            pd.NaT
         ], dtype="O")),

        # M8[M] -> M8[Y]
        ("datetime", "Y", "768614336404566619", [True, False],
         pd.Series([
            np.datetime64("768614336404566620-01", "M"),
            np.datetime64("768614336404566619-01", "M")
         ], dtype="O")),
        ("datetime64", "Y", "768614336404566619", [True, False, None],
         pd.Series([
            np.datetime64("768614336404566620-01", "M"),
            np.datetime64("768614336404566619-01", "M"),
            pd.NaT
         ], dtype="O")),
        ("M8", "Y", "768614336404566620", [True, False],
         pd.Series([
            np.datetime64("768614336404566621", "Y"),
            np.datetime64("768614336404566620", "Y")
         ], dtype="O")),
        ("datetime", "Y", "768614336404566620", [True, False, None],
         pd.Series([
            np.datetime64("768614336404566621", "Y"),
            np.datetime64("768614336404566620", "Y"),
            pd.NaT
         ], dtype="O")),
    ]


def timezone_promotion_input():
    return [
        # pd.Timestamp -> datetime.datetime
        ("datetime", "us", None, "2262-04-11 23:47:16.854774", [True, False],
        pd.Series([
            pd.Timestamp("2262-04-11 23:47:16.854775"),
            pd.Timestamp("2262-04-11 23:47:16.854774")
        ])),
        ("datetime", "us", None, "2262-04-11 23:47:16.854774", [True, False, None],
        pd.Series([
            pd.Timestamp("2262-04-11 23:47:16.854775"),
            pd.Timestamp("2262-04-11 23:47:16.854774"),
            pd.NaT
        ])),
        ("datetime", "us", "UTC", "2262-04-11 23:47:16.854774", [True, False],
        pd.Series([
            pd.Timestamp("2262-04-11 23:47:16.854775", tz="UTC"),
            pd.Timestamp("2262-04-11 23:47:16.854774", tz="UTC")
        ])),
        ("datetime", "us", "UTC", "2262-04-11 23:47:16.854774", [True, False, None],
        pd.Series([
            pd.Timestamp("2262-04-11 23:47:16.854775", tz="UTC"),
            pd.Timestamp("2262-04-11 23:47:16.854774", tz="UTC"),
            pd.NaT
        ])),
        ("datetime", "us", "Europe/Berlin", "2262-04-11 23:47:16.854774", [True, False],
        pd.Series([
            pd.Timestamp("2262-04-11 23:47:16.854775", tz="Europe/Berlin"),
            pd.Timestamp("2262-04-11 23:47:16.854774", tz="Europe/Berlin")
        ])),
        ("datetime", "us", "Europe/Berlin", "2262-04-11 23:47:16.854774", [True, False, None],
        pd.Series([
            pd.Timestamp("2262-04-11 23:47:16.854775", tz="Europe/Berlin"),
            pd.Timestamp("2262-04-11 23:47:16.854774", tz="Europe/Berlin"),
            pd.NaT
        ])),
        ("datetime", "us", "US/Pacific", "2262-04-11 23:47:16.854774", [True, False],
        pd.Series([
            pytz.timezone("US/Pacific").localize(datetime.datetime.fromisoformat("2262-04-11 23:47:16.854775")),
            pytz.timezone("US/Pacific").localize(datetime.datetime.fromisoformat("2262-04-11 23:47:16.854774"))
        ], dtype="O")),
        ("datetime", "us", "US/Pacific", "2262-04-11 23:47:16.854774", [True, False, None],
        pd.Series([
            pytz.timezone("US/Pacific").localize(datetime.datetime.fromisoformat("2262-04-11 23:47:16.854775")),
            pytz.timezone("US/Pacific").localize(datetime.datetime.fromisoformat("2262-04-11 23:47:16.854774")),
            pd.NaT
        ], dtype="O")),

    ]

# def error_input()


#####################
####    TESTS    ####
#####################


@pytest.mark.parametrize(
    "target_dtype, test_input, expected_result",
    list(itertools.chain(*[
        make_parameters("datetime", valid_input(pd.Timestamp)),
        make_parameters("datetime[pandas]", valid_input(pd.Timestamp)),
        make_parameters("datetime[python]", valid_input(datetime.datetime)),
        make_parameters("datetime[numpy]", valid_input(np.datetime64)),
    ]))
)
def test_boolean_to_datetime_accepts_all_valid_input(
    target_dtype, test_input, expected_result
):
    result = BooleanSeries(test_input).to_datetime(target_dtype, unit="us")
    assert result.equals(expected_result), (
        f"BooleanSeries.to_datetime(dtype='datetime') failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{expected_result}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize(
    "target_dtype, kwargs, test_input, test_output",
    list(itertools.chain(*[
        make_parameters("datetime", unit_input(pd.Timestamp)),
        make_parameters("datetime[pandas]", unit_input(pd.Timestamp)),
        make_parameters("datetime[python]", unit_input(datetime.datetime)),
        make_parameters("datetime[numpy]", unit_input(np.datetime64)),
    ]))
)
def test_boolean_to_datetime_handles_all_units(
    target_dtype, kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_datetime(target_dtype, **kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_datetime(dtype={repr(target_dtype)}, {fmt_kwargs}) "
        f"failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize(
    "target_dtype, kwargs, test_input, test_output",
    list(itertools.chain(*[
        make_parameters("datetime", timezone_input(pd.Timestamp)),
        make_parameters("datetime[pandas]", timezone_input(pd.Timestamp)),
        make_parameters("datetime[python]", timezone_input(datetime.datetime)),
    ]))
)
def test_boolean_to_datetime_handles_all_timezones(
    target_dtype, kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_datetime(target_dtype, **kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_datetime(dtype={repr(target_dtype)}, {fmt_kwargs})"
        f"failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize(
    "target_dtype, kwargs, test_input, test_output",
    list(itertools.chain(*[
        make_parameters("datetime", epoch_input(pd.Timestamp)),
        make_parameters("datetime[pandas]", epoch_input(pd.Timestamp)),
        make_parameters("datetime[python]", epoch_input(datetime.datetime)),
        make_parameters("datetime[numpy]", epoch_input(np.datetime64))
    ]))
)
def test_boolean_to_datetime_handles_all_epochs(
    target_dtype, kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_datetime(target_dtype, **kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_datetime(dtype={repr(target_dtype)}, {fmt_kwargs})"
        f"failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize(
    "target_dtype, test_input, expected_result",
    M8_input()
)
def test_boolean_to_datetime_handles_numpy_M8_dtypes_with_units_and_step_size(
    target_dtype, test_input, expected_result
):
    result = BooleanSeries(test_input).to_datetime(target_dtype, unit="Y")
    assert result.equals(expected_result), (
        f"BooleanSeries.to_datetime(dtype={repr(target_dtype)}) failed with "
        f"input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{expected_result}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize(
    "target_dtype, test_unit, test_epoch, test_input, expected_result",
    unit_promotion_input()
)
def test_boolean_to_datetime_unit_promotion(
    target_dtype, test_unit, test_epoch, test_input, expected_result
):
    series = BooleanSeries(test_input)
    result = series.to_datetime(target_dtype, unit=test_unit, since=test_epoch)
    assert result.equals(expected_result), (
        f"BooleanSeries.to_datetime(dtype={repr(target_dtype)}, "
        f"unit={repr(test_unit)}, since={repr(test_epoch)}) failed with "
        f"input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{expected_result}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize(
    "target_dtype, test_unit, test_tz, test_epoch, test_input, expected_result",
    timezone_promotion_input()
)
def test_boolean_to_datetime_timezone_promotion(
    target_dtype, test_unit, test_tz, test_epoch, test_input, expected_result
):
    series = BooleanSeries(test_input)
    result = series.to_datetime(
        target_dtype,
        unit=test_unit,
        since=test_epoch,
        tz=test_tz
    )
    assert result.equals(expected_result), (
        f"BooleanSeries.to_datetime(dtype={repr(target_dtype)}, "
        f"unit={repr(test_unit)}, tz={repr(test_tz)}, "
        f"since={repr(test_epoch)}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{expected_result}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("target_dtype", [
    "datetime",
    "datetime[pandas]",
    "datetime[python]",
    "datetime[numpy]"
])
def test_boolean_to_datetime_preserves_index(target_dtype):
    expected_dtype = {
        "datetime": pd.Timestamp,
        "datetime[pandas]": pd.Timestamp,
        "datetime[python]": datetime.datetime,
        "datetime[numpy]": np.datetime64
    }
    interpret = lambda x: interpret_iso_8601_string(
        x,
        expected_dtype[target_dtype]
    )

    # arrange
    val = pd.Series(
        [True, False, pd.NA],
        index=[4, 5, 6],
        dtype=pd.BooleanDtype()
    )
    expected = pd.Series(
        [
            interpret("1970-01-01 00:00:01.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
        ],
        index=[4, 5, 6],
        dtype=None if target_dtype in ("datetime", "datetime[pandas]") else "O"
    )

    # act
    result = BooleanSeries(val).to_datetime(target_dtype, unit="s")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_datetime(dtype={repr(target_dtype)}) does not "
        f"preserve index\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )
