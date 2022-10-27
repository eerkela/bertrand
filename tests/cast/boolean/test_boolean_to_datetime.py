import datetime

import numpy as np
import pandas as pd
import pytest

from pdtypes.cast.boolean import BooleanSeries


# TODO: test_boolean_to_datetime_handles_epochs


# TODO: preserves index


##################################
####    DATETIME SUPERTYPE    ####
##################################


@pytest.mark.parametrize("given", [
    # scalar
    (True, pd.to_datetime(pd.Series(1))),
    (False, pd.to_datetime(pd.Series(0))),
    ([True, False], pd.to_datetime(pd.Series([1, 0]))),
    ((False, True), pd.to_datetime(pd.Series([0, 1]))),
    ([True, False, None], pd.to_datetime(pd.Series([1, 0, None]))),
    ([True, False, pd.NA], pd.to_datetime(pd.Series([1, 0, None]))),
    ([True, False, np.nan], pd.to_datetime(pd.Series([1, 0, None]))),

    # array
    (np.array(True), pd.to_datetime(pd.Series(1))),
    (np.array([True, False]), pd.to_datetime(pd.Series([1, 0]))),
    (np.array([True, False], dtype="O"), pd.to_datetime(pd.Series([1, 0]))),
    (np.array([True, False, None], dtype="O"), pd.to_datetime(pd.Series([1, 0, None]))),
    (np.array([True, False, pd.NA], dtype="O"), pd.to_datetime(pd.Series([1, 0, None]))),
    (np.array([True, False, np.nan], dtype="O"), pd.to_datetime(pd.Series([1, 0, None]))),

    # series
    (pd.Series(True), pd.to_datetime(pd.Series(1))),
    (pd.Series([True, False]), pd.to_datetime(pd.Series([1, 0]))),
    (pd.Series([True, False], dtype="O"), pd.to_datetime(pd.Series([1, 0]))),
    (pd.Series([True, False, None], dtype="O"), pd.to_datetime(pd.Series([1, 0, None]))),
    (pd.Series([True, False, pd.NA], dtype="O"), pd.to_datetime(pd.Series([1, 0, None]))),
    (pd.Series([True, False, np.nan], dtype="O"), pd.to_datetime(pd.Series([1, 0, None]))),
    (pd.Series([True, False, None], dtype=pd.BooleanDtype()), pd.to_datetime(pd.Series([1, 0, None])))
])
def test_boolean_to_datetime_supertype_accepts_valid_inputs(given):
    # arrange
    val, expected = given

    # act
    result = BooleanSeries(val).to_datetime("datetime")

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_datetime(dtype='datetime') failed with input:\n"
        f"{val}\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    ("ns", pd.to_datetime(pd.Series([1, 0, None]), unit="ns")),
    ("us", pd.to_datetime(pd.Series([1, 0, None]), unit="us")),
    ("ms", pd.to_datetime(pd.Series([1, 0, None]), unit="ms")),
    ("s", pd.to_datetime(pd.Series([1, 0, None]), unit="s")),
    ("m", pd.to_datetime(pd.Series([1, 0, None]), unit="m")),
    ("h", pd.to_datetime(pd.Series([1, 0, None]), unit="h")),
    ("D", pd.to_datetime(pd.Series([1, 0, None]), unit="D")),
    ("W", pd.to_datetime(pd.Series([1, 0, None]), unit="W")),
    ("M", pd.Series([pd.Timestamp("1970-02-01"), pd.Timestamp("1970-01-01"), pd.NaT])),
    ("Y", pd.Series([pd.Timestamp("1971-01-01"), pd.Timestamp("1970-01-01"), pd.NaT])),
])
def test_boolean_to_datetime_supertype_handles_units(given):
    # arrange
    unit, expected = given

    # act
    series = pd.Series([1, 0, pd.NA], dtype=pd.BooleanDtype())
    result = BooleanSeries(series).to_datetime("datetime", unit=unit)

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_datetime(dtype='datetime', unit={repr(unit)}) "
        f"failed:\n"
        f"expected:"
        f"{expected}\n"
        f"received:"
        f"{result}"
    )


@pytest.mark.parametrize("given", [
    (
        "UTC",
        pd.to_datetime(pd.Series([1, 0, None]), unit="ns", utc=True)
    ),
    (
        "US/Pacific",
        pd.to_datetime(pd.Series([1, 0, None]), unit="ns", utc=True).dt.tz_convert("US/Pacific")
    ),
    (
        "Europe/Berlin",
        pd.to_datetime(pd.Series([1, 0, None]), unit="ns", utc=True).dt.tz_convert("Europe/Berlin")
    ),
    (
        "Etc/GMT+12",
        pd.to_datetime(pd.Series([1, 0, None]), unit="ns", utc=True).dt.tz_convert("Etc/GMT+12")
    )
])
def test_boolean_to_datetime_supertype_handles_timezones(given):
    # arrange
    tz, expected = given

    # act
    series = pd.Series([1, 0, pd.NA], dtype=pd.BooleanDtype())
    result = BooleanSeries(series).to_datetime("datetime", unit="ns", tz=tz)

    # assert
    assert result.equals(expected), (
        f"BooleanSeries.to_datetime(dtype='datetime', unit='ns', "
        f"tz={repr(tz)}) failed:\n"
        f"expected:"
        f"{expected}\n"
        f"received:"
        f"{result}"
    )



