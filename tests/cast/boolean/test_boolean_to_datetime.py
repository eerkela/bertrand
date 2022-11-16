import datetime
import zoneinfo

import dateutil
import numpy as np
import pandas as pd
import pytest
import pytz

from tests.cast.scheme import CastCase, CastParameters, parametrize
from tests.cast.boolean import (
    valid_input_data, valid_dtype_data, invalid_input_data, invalid_dtype_data
)
from tests.cast.datetime import interpret_iso_8601_string

from pdtypes.cast.boolean import BooleanSeries


# NOTE: pd.Timestamp objects do not currently support zoneinfo.ZoneInfo
# tzinfo objects.  Official support will be added soon.




# TODO: M8/datetime64 target dtype with specific units/step size
# np.dtype("M8[ns]"), np.dtype("M8[5us]"),
# np.dtype("M8[50ms]"), np.dtype("M8[2s]"), np.dtype("M8[30m]"),
# np.dtype("M8[h]"), np.dtype("M8[3D]"), np.dtype("M8[2W]"),
# np.dtype("M8[3M]"), np.dtype("M8[10Y]"),



# TODO: convert parameterized data functions to not use Parameters injection
# -> more like valid_input_data.  Saves lines and is more readable.
# -> use actual dtype as category - "datetime", "datetime[pandas]", etc.



####################
####    DATA    ####
####################


def valid_unit_data(expected_dtype):
    interpret = lambda x: interpret_iso_8601_string(x, expected_dtype)
    series_type = None if expected_dtype is pd.Timestamp else "O"

    case = lambda unit, test_output: CastCase(
        {"unit": unit},
        pd.Series([True, False]),
        test_output
    )

    return CastParameters(
        case("ns", pd.Series([
            interpret("1970-01-01 00:00:00.000000001"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("us", pd.Series([
            interpret("1970-01-01 00:00:00.000001000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("ms", pd.Series([
            interpret("1970-01-01 00:00:00.001000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("s", pd.Series([
            interpret("1970-01-01 00:00:01.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("m", pd.Series([
            interpret("1970-01-01 00:01:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("h", pd.Series([
            interpret("1970-01-01 01:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("D", pd.Series([
            interpret("1970-01-02 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("W", pd.Series([
            interpret("1970-01-08 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", pd.Series([
            interpret("1970-02-01 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("Y", pd.Series([
            interpret("1971-01-01 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
    ).with_na(pd.NA, pd.NaT)


def invalid_unit_data():
    case = lambda unit: CastCase(
        {"unit": unit},
        pd.Series([True, False]),
        pd.Series([True, False])
    )

    return CastParameters(
        case("not valid"),
        case("NS"),
        case("US"),
        case("MS"),
        case("S"),
        case("H"),
        case("d"),
        case("w"),
        case("y"),
    ).with_na(pd.NA, pd.NaT)


def valid_timezone_data(expected_dtype):
    interpret = lambda x, tz: interpret_iso_8601_string(x, expected_dtype, tz)
    series_type = None if expected_dtype is pd.Timestamp else "O"

    case = lambda tz, test_output: CastCase(
        {"unit": "us", "tz": tz},
        pd.Series([True, False]),
        test_output
    )

    return CastParameters(
        case(None, pd.Series([
            interpret("1970-01-01 00:00:00.000001000", tz=None),
            interpret("1970-01-01 00:00:00.000000000", tz=None)
        ], dtype=series_type)),
        case("US/Pacific", pd.Series([
            interpret("1969-12-31 16:00:00.000001000", tz="US/Pacific"),
            interpret("1969-12-31 16:00:00.000000000", tz="US/Pacific")
        ], dtype=series_type)),
        case("Europe/Berlin", pd.Series([
            interpret("1970-01-01 01:00:00.000001000", tz="Europe/Berlin"),
            interpret("1970-01-01 01:00:00.000000000", tz="Europe/Berlin")
        ], dtype=series_type)),
        case(pytz.timezone("Asia/Hong_Kong"), pd.Series([
            interpret(
                "1970-01-01 08:00:00.000001000",
                tz=pytz.timezone("Asia/Hong_Kong")
            ),
            interpret(
                "1970-01-01 08:00:00.000000000",
                tz=pytz.timezone("Asia/Hong_Kong")
            )
        ], dtype=series_type)),

        # NOTE: fails to compile, see NOTE at start of file
        # case(zoneinfo.ZoneInfo("America/Sao_Paulo"), pd.Series([
        #     interpret(
        #         "1969-12-31 21:00:00.000001000",
        #         tz=zoneinfo.ZoneInfo("America/Sao_Paulo")
        #     ),
        #     interpret(
        #         "1969-12-31 21:00:00.000000000",
        #         tz=zoneinfo.ZoneInfo("America/Sao_Paulo")
        #     )
        # ], dtype=series_type)),

        case(dateutil.tz.gettz("Asia/Istanbul"), pd.Series([
            interpret(
                "1970-01-01 02:00:00.000001000",
                tz=dateutil.tz.gettz("Asia/Istanbul")
            ),
            interpret(
                "1970-01-01 02:00:00.000000000",
                tz=dateutil.tz.gettz("Asia/Istanbul")
            )
        ], dtype=series_type)),
    ).with_na(pd.NA, pd.NaT)


def valid_epoch_data(expected_dtype):
    interpret = lambda x: interpret_iso_8601_string(x, expected_dtype)
    series_type = None if expected_dtype is pd.Timestamp else "O"

    case = lambda epoch, test_output: CastCase(
        {"unit": "us", "since": epoch},
        pd.Series([True, False]),
        test_output
    )

    return CastParameters(
        case(
            "UTC",
            pd.Series([
                interpret("1970-01-01 00:00:00.000001000"),
                interpret("1970-01-01 00:00:00.000000000")
            ], dtype=series_type)
        ),
        case(
            "reduced julian",
            pd.Series([
                interpret("1858-11-16 12:00:00.000001000"),
                interpret("1858-11-16 12:00:00.000000000")
            ], dtype=series_type)
        ),
        case(
            "1941-12-07 08:00:00-1030",
            pd.Series([
                interpret("1941-12-07 18:30:00.000001000"),
                interpret("1941-12-07 18:30:00.000000000")
            ], dtype=series_type)
        ),
        case(
            "November 22, 1963 at 12:30 PM",
            pd.Series([
                interpret("1963-11-22 12:30:00.000001000"),
                interpret("1963-11-22 12:30:00.000000000")
            ], dtype=series_type)
        ),
        case(
            pd.Timestamp("1989-11-09 19:00:00", tz="Europe/Berlin"),
            pd.Series([
                interpret("1989-11-09 18:00:00.000001000"),
                interpret("1989-11-09 18:00:00.000000000")
            ], dtype=series_type)
        ),
        case(
            datetime.datetime.fromisoformat("1918-11-11 11:00:00"),
            pd.Series([
                interpret("1918-11-11 11:00:00.000001000"),
                interpret("1918-11-11 11:00:00.000000000")
            ], dtype=series_type)
        ),
        case(
            np.datetime64("1789-07-14 20:00:00"),
            pd.Series([
                interpret("1789-07-14 20:00:00.000001000"),
                interpret("1789-07-14 20:00:00.000000000")
            ], dtype=series_type)
        ),
    ).with_na(pd.NA, pd.NaT)


def irregular_unit_data(expected_dtype):
    interpret = lambda x: interpret_iso_8601_string(x, expected_dtype)
    series_type = None if expected_dtype is pd.Timestamp else "O"

    case = lambda unit, epoch, test_output: CastCase(
        {"unit": unit, "since": epoch},
        pd.Series([True, False]),
        test_output
    )

    return CastParameters(
        # months (non-leap year)
        case("M", "1970-01-01 00:00:00", pd.Series([
            interpret("1970-02-01 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1970-02-01 00:00:00", pd.Series([
            interpret("1970-03-01 00:00:00.000000000"),
            interpret("1970-02-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1970-03-01 00:00:00", pd.Series([
            interpret("1970-04-01 00:00:00.000000000"),
            interpret("1970-03-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1970-04-01 00:00:00", pd.Series([
            interpret("1970-05-01 00:00:00.000000000"),
            interpret("1970-04-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1970-05-01 00:00:00", pd.Series([
            interpret("1970-06-01 00:00:00.000000000"),
            interpret("1970-05-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1970-06-01 00:00:00", pd.Series([
            interpret("1970-07-01 00:00:00.000000000"),
            interpret("1970-06-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1970-07-01 00:00:00", pd.Series([
            interpret("1970-08-01 00:00:00.000000000"),
            interpret("1970-07-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1970-08-01 00:00:00", pd.Series([
            interpret("1970-09-01 00:00:00.000000000"),
            interpret("1970-08-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1970-09-01 00:00:00", pd.Series([
            interpret("1970-10-01 00:00:00.000000000"),
            interpret("1970-09-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1970-10-01 00:00:00", pd.Series([
            interpret("1970-11-01 00:00:00.000000000"),
            interpret("1970-10-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1970-11-01 00:00:00", pd.Series([
            interpret("1970-12-01 00:00:00.000000000"),
            interpret("1970-11-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1970-12-01 00:00:00", pd.Series([
            interpret("1971-01-01 00:00:00.000000000"),
            interpret("1970-12-01 00:00:00.000000000")
        ], dtype=series_type)),

        # months (leap year)
        case("M", "1968-01-01 00:00:00", pd.Series([
            interpret("1968-02-01 00:00:00.000000000"),
            interpret("1968-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1968-02-01 00:00:00", pd.Series([
            interpret("1968-03-01 00:00:00.000000000"),
            interpret("1968-02-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1968-03-01 00:00:00", pd.Series([
            interpret("1968-04-01 00:00:00.000000000"),
            interpret("1968-03-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1968-04-01 00:00:00", pd.Series([
            interpret("1968-05-01 00:00:00.000000000"),
            interpret("1968-04-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1968-05-01 00:00:00", pd.Series([
            interpret("1968-06-01 00:00:00.000000000"),
            interpret("1968-05-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1968-06-01 00:00:00", pd.Series([
            interpret("1968-07-01 00:00:00.000000000"),
            interpret("1968-06-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1968-07-01 00:00:00", pd.Series([
            interpret("1968-08-01 00:00:00.000000000"),
            interpret("1968-07-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1968-08-01 00:00:00", pd.Series([
            interpret("1968-09-01 00:00:00.000000000"),
            interpret("1968-08-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1968-09-01 00:00:00", pd.Series([
            interpret("1968-10-01 00:00:00.000000000"),
            interpret("1968-09-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1968-10-01 00:00:00", pd.Series([
            interpret("1968-11-01 00:00:00.000000000"),
            interpret("1968-10-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1968-11-01 00:00:00", pd.Series([
            interpret("1968-12-01 00:00:00.000000000"),
            interpret("1968-11-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("M", "1968-12-01 00:00:00", pd.Series([
            interpret("1969-01-01 00:00:00.000000000"),
            interpret("1968-12-01 00:00:00.000000000")
        ], dtype=series_type)),

        # years
        case("Y", "1968-01-01 00:00:00", pd.Series([
            interpret("1969-01-01 00:00:00.000000000"),
            interpret("1968-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("Y", "1969-01-01 00:00:00", pd.Series([
            interpret("1970-01-01 00:00:00.000000000"),
            interpret("1969-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("Y", "1970-01-01 00:00:00", pd.Series([
            interpret("1971-01-01 00:00:00.000000000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("Y", "1971-01-01 00:00:00", pd.Series([
            interpret("1972-01-01 00:00:00.000000000"),
            interpret("1971-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("Y", "1972-01-01 00:00:00", pd.Series([
            interpret("1973-01-01 00:00:00.000000000"),
            interpret("1972-01-01 00:00:00.000000000")
        ], dtype=series_type)),

        # centuries
        case("Y", "1900-01-01 00:00:00", pd.Series([
            interpret("1901-01-01 00:00:00.000000000"),
            interpret("1900-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("Y", "2100-01-01 00:00:00", pd.Series([
            interpret("2101-01-01 00:00:00.000000000"),
            interpret("2100-01-01 00:00:00.000000000")
        ], dtype=series_type)),

        # 400-year exceptions
        case("Y", "2000-01-01 00:00:00", pd.Series([
            interpret("2001-01-01 00:00:00.000000000"),
            interpret("2000-01-01 00:00:00.000000000")
        ], dtype=series_type)),
    ).with_na(pd.NA, pd.NaT)


def M8_data():
    case = lambda M8_repr, test_output: CastCase(
        {"dtype": M8_repr, "unit": "Y"},
        pd.Series([True, False]),
        test_output
    )

    return CastParameters(
        case("M8[ns]", pd.Series([
            np.datetime64("1971-01-01 00:00:00.000000000", "ns"),
            np.datetime64("1970-01-01 00:00:00.000000000", "ns")
        ], dtype="O")),
        case("datetime64[25us]", pd.Series([
            np.datetime64("1971-01-01 00:00:00.000000", "25us"),
            np.datetime64("1970-01-01 00:00:00.000000", "25us")
        ], dtype="O")),
        case("datetime64[ms]", pd.Series([
            np.datetime64("1971-01-01 00:00:00.000", "ms"),
            np.datetime64("1970-01-01 00:00:00.000", "ms")
        ], dtype="O")),
        case("M8[10s]", pd.Series([
            np.datetime64("1971-01-01 00:00:00", "10s"),
            np.datetime64("1970-01-01 00:00:00", "10s")
        ], dtype="O")),
        case("M8[60m]", pd.Series([
            np.datetime64("1971-01-01 00:00", "60m"),
            np.datetime64("1970-01-01 00:00", "60m")
        ], dtype="O")),
        case("datetime64[3h]", pd.Series([
            np.datetime64("1971-01-01 00", "3h"),
            np.datetime64("1970-01-01 00", "3h")
        ], dtype="O")),
        case("M8[3D]", pd.Series([
            np.datetime64("1970-12-30", "3D"),
            np.datetime64("1970-01-01", "3D")
        ], dtype="O")),
        case("datetime64[W]", pd.Series([
            np.datetime64("1970-12-31", "W"),
            np.datetime64("1970-01-01", "W")
        ], dtype="O")),
        case("M8[3M]", pd.Series([
            np.datetime64("1971-01", "3M"),
            np.datetime64("1970-01", "3M")
        ], dtype="O")),
        case("datetime64[Y]", pd.Series([
            np.datetime64("1971", "Y"),
            np.datetime64("1970", "Y")
        ], dtype="O")),
    ).with_na(pd.NA, pd.NaT)


def unit_promotion_data():
    case = lambda target_dtype, unit, epoch, test_output: CastCase(
        {"dtype": target_dtype, "unit": unit, "since": epoch},
        pd.Series([True, False]),
        test_output
    )

    return CastParameters(
        # pd.Timestamp -> datetime.datetime
        case("datetime", "us", "2262-04-11 23:47:16.854774", pd.Series([
            pd.Timestamp("2262-04-11 23:47:16.854775"),
            pd.Timestamp("2262-04-11 23:47:16.854774")
        ])),
        case("datetime", "us", "2262-04-11 23:47:16.854775", pd.Series([
            datetime.datetime.fromisoformat("2262-04-11 23:47:16.854776"),
            datetime.datetime.fromisoformat("2262-04-11 23:47:16.854775")
        ])),

        # M8[ns] -> M8[us]
        case("M8", "us", "2262-04-11 23:47:16.854774", pd.Series([
            np.datetime64("2262-04-11 23:47:16.854775000", "ns"),
            np.datetime64("2262-04-11 23:47:16.854774000", "ns")
        ], dtype="O")),
        case("datetime64", "us", "2262-04-11 23:47:16.854775", pd.Series([
            np.datetime64("2262-04-11 23:47:16.854776", "us"),
            np.datetime64("2262-04-11 23:47:16.854775", "us")
        ], dtype="O")),

        # datetime.datetime -> M8[us]
        case("datetime", "us", "9999-12-31 23:59:59.999998", pd.Series([
            datetime.datetime.fromisoformat("9999-12-31 23:59:59.999999"),
            datetime.datetime.fromisoformat("9999-12-31 23:59:59.999998")
        ], dtype="O")),
        case("datetime", "us", "9999-12-31 23:59:59.999999", pd.Series([
            np.datetime64("10000-01-01 00:00:00.000000", "us"),
            np.datetime64("9999-12-31 23:59:59.999999", "us")
        ], dtype="O")),

        # M8[us] -> M8[ms]
        case("datetime", "ms", "294247-01-10 04:00:54.774", pd.Series([
            np.datetime64("294247-01-10 04:00:54.775000", "us"),
            np.datetime64("294247-01-10 04:00:54.774000", "us")
        ], dtype="O")),
        case("datetime64", "ms", "294247-01-10 04:00:54.775", pd.Series([
            np.datetime64("294247-01-10 04:00:54.776", "ms"),
            np.datetime64("294247-01-10 04:00:54.775", "ms")
        ], dtype="O")),

        # M8[ms] -> M8[s]
        case("datetime", "s", "292278994-08-17 07:12:54", pd.Series([
            np.datetime64("292278994-08-17 07:12:55.000", "ms"),
            np.datetime64("292278994-08-17 07:12:54.000", "ms")
        ], dtype="O")),
        case("datetime64", "s", "292278994-08-17 07:12:55", pd.Series([
            np.datetime64("292278994-08-17 07:12:56", "s"),
            np.datetime64("292278994-08-17 07:12:55", "s")
        ], dtype="O")),

        # M8[s] -> M8[m]
        case("datetime64", "m", "292277026596-12-04 15:29", pd.Series([
            np.datetime64("292277026596-12-04 15:30:00", "s"),
            np.datetime64("292277026596-12-04 15:29:00", "s")
        ], dtype="O")),
        case("datetime", "m", "292277026596-12-04 15:30", pd.Series([
            np.datetime64("292277026596-12-04 15:31", "m"),
            np.datetime64("292277026596-12-04 15:30", "m")
        ], dtype="O")),

        # M8[m] -> M8[h]
        case("M8", "h", "17536621479585-08-30 17:00", pd.Series([
            np.datetime64("17536621479585-08-30 18:00", "m"),
            np.datetime64("17536621479585-08-30 17:00", "m")
        ], dtype="O")),
        case("datetime64", "h", "17536621479585-08-30 18:00", pd.Series([
            np.datetime64("17536621479585-08-30 19", "h"),
            np.datetime64("17536621479585-08-30 18", "h")
        ], dtype="O")),

        # M8[h] -> M8[D]
        case("datetime", "D", "1052197288658909-10-09", pd.Series([
            np.datetime64("1052197288658909-10-10 00:00", "h"),
            np.datetime64("1052197288658909-10-09 00:00", "h")
        ], dtype="O")),
        case("datetime", "D", "1052197288658909-10-10", pd.Series([
            np.datetime64("1052197288658909-10-11", "D"),
            np.datetime64("1052197288658909-10-10", "D")
        ], dtype="O")),

        # M8[D] -> M8[M]
        case("M8", "M", "25252734927768524-06", pd.Series([
            np.datetime64("25252734927768524-07-01", "D"),
            np.datetime64("25252734927768524-06-01", "D")
        ], dtype="O")),
        case("datetime", "M", "25252734927768524-07", pd.Series([
            np.datetime64("25252734927768524-08", "M"),
            np.datetime64("25252734927768524-07", "M")
        ], dtype="O")),

        # M8[M] -> M8[Y]
        case("datetime", "Y", "768614336404566619", pd.Series([
            np.datetime64("768614336404566620-01", "M"),
            np.datetime64("768614336404566619-01", "M")
        ], dtype="O")),
        case("M8", "Y", "768614336404566620", pd.Series([
            np.datetime64("768614336404566621", "Y"),
            np.datetime64("768614336404566620", "Y")
        ], dtype="O")),
    ).with_na(pd.NA, pd.NaT)


def timezone_promotion_data():
    case = lambda tz, test_output: CastCase(
        {
            "dtype": "datetime",
            "unit": "us",
            "tz": tz,
            "since": "2262-04-11 23:47:16.854774"
        },
        pd.Series([True, False]),
        test_output
    )

    return CastParameters(
        case(None, pd.Series([
            pd.Timestamp("2262-04-11 23:47:16.854775"),
            pd.Timestamp("2262-04-11 23:47:16.854774")
        ])),
        case("UTC", pd.Series([
            pd.Timestamp("2262-04-11 23:47:16.854775", tz="UTC"),
            pd.Timestamp("2262-04-11 23:47:16.854774", tz="UTC")
        ])),
        case("Europe/Berlin", pd.Series([
            pytz.timezone("Europe/Berlin").localize(datetime.datetime.fromisoformat("2262-04-12 00:47:16.854775")),
            pytz.timezone("Europe/Berlin").localize(datetime.datetime.fromisoformat("2262-04-12 00:47:16.854774"))
        ], dtype="O")),
        case("US/Pacific", pd.Series([
            pd.Timestamp("2262-04-11 15:47:16.854775", tz="US/Pacific"),
            pd.Timestamp("2262-04-11 15:47:16.854774", tz="US/Pacific")
        ])),
    ).with_na(pd.NA, pd.NaT)


#####################
####    VALID    ####
#####################


@parametrize(valid_input_data("datetime"))
def test_boolean_to_datetime_accepts_all_valid_inputs(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_datetime(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_datetime({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(valid_dtype_data("datetime"))
def test_boolean_to_datetime_accepts_all_valid_type_specifiers(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_datetime(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_datetime({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(
    CastParameters(
        valid_unit_data(pd.Timestamp),
        dtype="datetime"
    ),
    CastParameters(
        valid_unit_data(pd.Timestamp),
        dtype="datetime[pandas]"
    ),
    CastParameters(
        valid_unit_data(datetime.datetime),
        dtype="datetime[python]"
    ),
    CastParameters(
        valid_unit_data(np.datetime64),
        dtype="datetime[numpy]"
    ),
)
def test_boolean_to_datetime_handles_all_valid_units(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_datetime(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_datetime({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(
    CastParameters(
        valid_timezone_data(pd.Timestamp),
        dtype="datetime"
    ),
    CastParameters(
        valid_timezone_data(pd.Timestamp),
        dtype="datetime[pandas]"
    ),
    CastParameters(
        valid_timezone_data(datetime.datetime),
        dtype="datetime[python]"
    ),
)
def test_boolean_to_datetime_handles_all_valid_timezones(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_datetime(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_datetime({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(
    CastParameters(
        valid_epoch_data(pd.Timestamp),
        dtype="datetime"
    ),
    CastParameters(
        valid_epoch_data(pd.Timestamp),
        dtype="datetime[pandas]"
    ),
    CastParameters(
        valid_epoch_data(datetime.datetime),
        dtype="datetime[python]"
    ),
    CastParameters(
        valid_epoch_data(np.datetime64),
        dtype="datetime[numpy]"
    ),
)
def test_boolean_to_datetime_handles_all_valid_epochs(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_datetime(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_datetime({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(
    CastParameters(
        irregular_unit_data(pd.Timestamp),
        dtype="datetime"
    ),
    CastParameters(
        irregular_unit_data(pd.Timestamp),
        dtype="datetime[pandas]"
    ),
    CastParameters(
        irregular_unit_data(datetime.datetime),
        dtype="datetime[python]"
    ),
    CastParameters(
        irregular_unit_data(np.datetime64),
        dtype="datetime[numpy]"
    ),
)
def test_boolean_to_datetime_handles_irregular_month_and_year_units_with_different_epochs(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_datetime(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_datetime({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(M8_data())
def test_boolean_to_datetime_handles_numpy_M8_dtypes_with_units_and_step_size(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_datetime(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_datetime({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(unit_promotion_data())
def test_boolean_to_datetime_unit_promotion_with_datetime_and_M8_supertypes(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_datetime(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_datetime({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(timezone_promotion_data())
def test_boolean_to_datetime_timezone_promotion(
    kwargs, test_input, test_output
):
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    result = BooleanSeries(test_input).to_datetime(**kwargs)
    assert result.equals(test_output), (
        f"BooleanSeries.to_datetime({fmt_kwargs}) failed with input:\n"
        f"{test_input}\n"
        f"expected:\n"
        f"{test_output}\n"
        f"received:\n"
        f"{result}"
    )


#######################
####    INVALID    ####
#######################


@parametrize(invalid_input_data())
def test_boolean_to_datetime_rejects_all_invalid_inputs(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError):
        BooleanSeries(test_input).to_datetime(**kwargs)


@parametrize(invalid_dtype_data("datetime"))
def test_boolean_to_datetime_rejects_all_invalid_type_specifiers(
    kwargs, test_input, test_output
):
    with pytest.raises(TypeError, match="`dtype` must be datetime-like"):
        BooleanSeries(test_input).to_datetime(**kwargs)


@parametrize(invalid_unit_data())
def test_boolean_to_datetime_rejects_invalid_units(
    kwargs, test_input, test_output
):
    with pytest.raises(ValueError):
        BooleanSeries(test_input).to_datetime(**kwargs)




# TODO: invalid
# - invalid dtype (default for all conversions)
# - invalid unit
# - invalid timezone (malformed)
# - invalid epoch (malformed, outside M8[Y] range)


#####################
####    OTHER    ####
#####################


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
    kwargs = {"dtype": target_dtype, "unit": "s"}
    result = BooleanSeries(val).to_datetime(**kwargs)

    # assert
    fmt_kwargs = ", ".join(f"{k}={repr(v)}" for k, v in kwargs.items())
    assert result.equals(expected), (
        f"BooleanSeries.to_datetime({fmt_kwargs}) does not preserve index.\n"
        f"expected:\n"
        f"{expected}\n"
        f"received:\n"
        f"{result}"
    )
