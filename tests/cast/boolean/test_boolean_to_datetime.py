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
        ("ns", [True, False], pd.Series([
            interpret("1970-01-01 00:00:00.000000001"),
            interpret("1970-01-01 00:00:00.000000000")
            ], dtype=series_type)
        ),
        ("ns", [True, False, None], pd.Series([
            interpret("1970-01-01 00:00:00.000000001"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
            ], dtype=series_type)
        ),
        ("us", [True, False], pd.Series([
            interpret("1970-01-01 00:00:00.000001000"),
            interpret("1970-01-01 00:00:00.000000000")
            ], dtype=series_type)
        ),
        ("us", [True, False, None], pd.Series([
            interpret("1970-01-01 00:00:00.000001000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
            ], dtype=series_type)
        ),
        ("ms", [True, False], pd.Series([
            interpret("1970-01-01 00:00:00.001000000"),
            interpret("1970-01-01 00:00:00.000000000")
            ], dtype=series_type)
        ),
        ("ms", [True, False, None], pd.Series([
            interpret("1970-01-01 00:00:00.001000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
            ], dtype=series_type)
        ),
        ("s", [True, False], pd.Series([
            interpret("1970-01-01 00:00:01.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
            ], dtype=series_type)
        ),
        ("s", [True, False, None], pd.Series([
            interpret("1970-01-01 00:00:01.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
            ], dtype=series_type)
        ),
        ("m", [True, False], pd.Series([
            interpret("1970-01-01 00:01:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
            ], dtype=series_type)
        ),
        ("m", [True, False, None], pd.Series([
            interpret("1970-01-01 00:01:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
            ], dtype=series_type)
        ),
        ("h", [True, False], pd.Series([
            interpret("1970-01-01 01:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
            ], dtype=series_type)
        ),
        ("h", [True, False, None], pd.Series([
            interpret("1970-01-01 01:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
            ], dtype=series_type)
        ),
        ("D", [True, False], pd.Series([
            interpret("1970-01-02 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
            ], dtype=series_type)
        ),
        ("D", [True, False, None], pd.Series([
            interpret("1970-01-02 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
            ], dtype=series_type)
        ),
        ("W", [True, False], pd.Series([
            interpret("1970-01-08 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
            ], dtype=series_type)
        ),
        ("W", [True, False, None], pd.Series([
            interpret("1970-01-08 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
            ], dtype=series_type)
        ),
        ("M", [True, False], pd.Series([
            interpret("1970-02-01 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
            ], dtype=series_type)
        ),
        ("M", [True, False, None], pd.Series([
            interpret("1970-02-01 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
            ], dtype=series_type)
        ),
        ("Y", [True, False], pd.Series([
            interpret("1971-01-01 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
            ], dtype=series_type)
        ),
        ("Y", [True, False, None], pd.Series([
            interpret("1971-01-01 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000"),
            pd.NaT
            ], dtype=series_type)
        ),
    ]


def timezone_input(expected_dtype):
    interpret = lambda x, tz: interpret_iso_8601_string(x, expected_dtype, tz)
    series_type = None if expected_dtype is pd.Timestamp else "O"

    return [  # target unit, target timezone, test input, test output
        ("us", None, [True, False], pd.Series([
            interpret("1970-01-01 00:00:00.000001000", None),
            interpret("1970-01-01 00:00:00.000000000", None)
            ], dtype=series_type)
        ),
        ("us", None, [True, False, None], pd.Series([
            interpret("1970-01-01 00:00:00.000001000", None),
            interpret("1970-01-01 00:00:00.000000000", None),
            pd.NaT
            ], dtype=series_type)
        ),
        ("us", "US/Pacific", [True, False], pd.Series([
            interpret("1969-12-31 16:00:00.000001000", "US/Pacific"),
            interpret("1969-12-31 16:00:00.000000000", "US/Pacific")
            ], dtype=series_type)
        ),
        ("us", "US/Pacific", [True, False, None], pd.Series([
            interpret("1969-12-31 16:00:00.000001000", "US/Pacific"),
            interpret("1969-12-31 16:00:00.000000000", "US/Pacific"),
            pd.NaT
            ], dtype=series_type)
        ),
        ("us", "Europe/Berlin", [True, False], pd.Series([
            interpret("1970-01-01 01:00:00.000001000", "Europe/Berlin"),
            interpret("1970-01-01 01:00:00.000000000", "Europe/Berlin")
            ], dtype=series_type)
        ),
        ("us", "Europe/Berlin", [True, False, None], pd.Series([
            interpret("1970-01-01 01:00:00.000001000", "Europe/Berlin"),
            interpret("1970-01-01 01:00:00.000000000", "Europe/Berlin"),
            pd.NaT
            ], dtype=series_type)
        ),
        ("us", pytz.timezone("Asia/Hong_Kong"), [True, False], pd.Series([
            interpret("1970-01-01 08:00:00.000001000", pytz.timezone("Asia/Hong_Kong")),
            interpret("1970-01-01 08:00:00.000000000", pytz.timezone("Asia/Hong_Kong"))
            ], dtype=series_type)
        ),
        ("us", pytz.timezone("Asia/Hong_Kong"), [True, False, None], pd.Series([
            interpret("1970-01-01 08:00:00.000001000", pytz.timezone("Asia/Hong_Kong")),
            interpret("1970-01-01 08:00:00.000000000", pytz.timezone("Asia/Hong_Kong")),
            pd.NaT
            ], dtype=series_type)
        ),
        # ("us", zoneinfo.ZoneInfo("America/Sao_Paulo"), [True, False], pd.Series([
        #     interpret("1969-12-31 21:00:00.000001000", zoneinfo.ZoneInfo("America/Sao_Paulo")),
        #     interpret("1969-12-31 21:00:00.000000000", zoneinfo.ZoneInfo("America/Sao_Paulo"))
        #     ], dtype=series_type)
        # ),
        # ("us", zoneinfo.ZoneInfo("America/Sao_Paulo"), [True, False, None], pd.Series([
        #     interpret("1969-12-31 21:00:00.000001000", zoneinfo.ZoneInfo("America/Sao_Paulo")),
        #     interpret("1969-12-31 21:00:00.000000000", zoneinfo.ZoneInfo("America/Sao_Paulo")),
        #     pd.NaT
        #     ], dtype=series_type)
        # ),
        ("us", dateutil.tz.gettz("Asia/Istanbul"), [True, False], pd.Series([
            interpret("1970-01-01 02:00:00.000001000", dateutil.tz.gettz("Asia/Istanbul")),
            interpret("1970-01-01 02:00:00.000000000", dateutil.tz.gettz("Asia/Istanbul"))
            ], dtype=series_type)
        ),
        ("us", dateutil.tz.gettz("Asia/Istanbul"), [True, False, None], pd.Series([
            interpret("1970-01-01 02:00:00.000001000", dateutil.tz.gettz("Asia/Istanbul")),
            interpret("1970-01-01 02:00:00.000000000", dateutil.tz.gettz("Asia/Istanbul")),
            pd.NaT
            ], dtype=series_type)
        ),
    ]


#####################
####    TESTS    ####
#####################


# TODO: test_boolean_to_datetime_handles_epochs


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
    "target_dtype, test_unit, test_input, expected_result",
    list(itertools.chain(*[
        make_parameters("datetime", unit_input(pd.Timestamp)),
        make_parameters("datetime[pandas]", unit_input(pd.Timestamp)),
        make_parameters("datetime[python]", unit_input(datetime.datetime)),
        make_parameters("datetime[numpy]", unit_input(np.datetime64)),
    ]))
)
def test_boolean_to_datetime_handles_all_units(
    target_dtype, test_unit, test_input, expected_result
):
    series = BooleanSeries(test_input)
    result = series.to_datetime(target_dtype, unit=test_unit)
    assert result.equals(expected_result), (
        f"BooleanSeries.to_datetime(dtype={repr(target_dtype)}, "
        f"unit={repr(test_unit)}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{expected_result}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize(
    "target_dtype, test_unit, test_tz, test_input, expected_result",
    list(itertools.chain(*[
        make_parameters("datetime", timezone_input(pd.Timestamp)),
        make_parameters("datetime[pandas]", timezone_input(pd.Timestamp)),
        make_parameters("datetime[python]", timezone_input(datetime.datetime)),
    ]))
)
def test_boolean_to_datetime_handles_all_timezones(
    target_dtype, test_unit, test_tz, test_input, expected_result
):
    series = BooleanSeries(test_input)
    result = series.to_datetime(target_dtype, unit=test_unit, tz=test_tz)
    assert result.equals(expected_result), (
        f"BooleanSeries.to_datetime(dtype={repr(target_dtype)}, "
        f"unit={repr(test_unit)}, tz={repr(test_tz)}) failed with input:\n"
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
