import datetime
import zoneinfo

import dateutil
import numpy as np
import pandas as pd
import pytest
import pytz

from tests.cast.scheme import (
    CastCase, CastParameters, interpret_iso_8601_string, parametrize
)
from tests.cast.boolean import input_format_data, target_dtype_data

from pdtypes.cast.boolean import BooleanSeries


# NOTE: pd.Timestamp objects do not currently support zoneinfo.ZoneInfo
# tzinfo objects.  Official support will be added soon.


# TODO: on invalid input, use pytest.raises as a context manager and do
# assertions on the returned exc_info object, rather than matching directly
# in pytest.raises() itself.  This should be more stable.


####################
####    DATA    ####
####################


def unit_data(target_dtype):
    interpret = lambda x: interpret_iso_8601_string(x, target_dtype)
    if target_dtype in ("datetime", "datetime[pandas]"):
        series_type = None
    else:
        series_type = "O"

    case = lambda unit, test_output: CastCase(
        {"dtype": target_dtype, "unit": unit},
        pd.Series([True, False]),
        test_output
    )

    return CastParameters(
        #####################
        ####    VALID    ####
        #####################

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

        #######################
        ####    INVALID    ####
        #######################

        case("not valid", pytest.raises(Exception)),
        case("NS", pytest.raises(Exception)),
        case("US", pytest.raises(Exception)),
        case("MS", pytest.raises(Exception)),
        case("S", pytest.raises(Exception)),
        case("H", pytest.raises(Exception)),
        case("d", pytest.raises(Exception)),
        case("w", pytest.raises(Exception)),
        case("y", pytest.raises(Exception)),

    ).with_na(pd.NA, pd.NaT)


def valid_timezone_data(target_dtype):
    interpret = lambda x, tz: interpret_iso_8601_string(x, target_dtype, tz=tz)
    if target_dtype in ("datetime", "datetime[pandas]"):
        series_type = None
    else:
        series_type = "O"

    case = lambda tz, test_output: CastCase(
        {"dtype": target_dtype, "unit": "us", "tz": tz},
        pd.Series([True, False]),
        test_output
    )

    # TODO: if target_dtype is "datetime[numpy]", all should raise
    # TODO: "local" with an xfail?  Or call tzlocal() directly.
    # -> interpret_iso_8601_string should accept UTC strings, converted to tz
    # rather than localized.
    # TODO: this should include M8_timezone_data() as well.

    return CastParameters(
        #####################
        ####    VALID    ####
        #####################

        # naive
        case(None, pd.Series([
            interpret("1970-01-01 00:00:00.000001000", tz=None),
            interpret("1970-01-01 00:00:00.000000000", tz=None)
        ], dtype=series_type)),

        # local timezone
        # case("local", pd.Series([
        #     interpret("1970-01-01 00:00:00.000001000", tz="UTC")
        # ], dtype=series_type))

        # IANA strings
        case("US/Pacific", pd.Series([
            interpret("1969-12-31 16:00:00.000001000", tz="US/Pacific"),
            interpret("1969-12-31 16:00:00.000000000", tz="US/Pacific")
        ], dtype=series_type)),
        case("Europe/Berlin", pd.Series([
            interpret("1970-01-01 01:00:00.000001000", tz="Europe/Berlin"),
            interpret("1970-01-01 01:00:00.000000000", tz="Europe/Berlin")
        ], dtype=series_type)),

        # pytz objects
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

        # zoneinfo objects - NOTE: fails to compile, see NOTE at start of file
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

        # dateutil.tz.tzfile objects
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

        #######################
        ####    INVALID    ####
        #######################

        case("the cake is a lie", pytest.raises(Exception)),
        case("Pacific", pytest.raises(Exception)),
        case("PST", pytest.raises(Exception)),

    ).with_na(pd.NA, pd.NaT)


def M8_timezone_data():
    case = lambda tz: CastCase(
        {"dtype": "datetime[numpy]", "tz": tz},
        pd.Series([True, False]),
        pytest.raises(Exception)
    )

    return CastParameters(
        case("US/Pacific"),
        case("Europe/Berlin"),
        case(pytz.timezone("Asia/Hong_Kong")),
        case(zoneinfo.ZoneInfo("America/Sao_Paulo")),
        case(dateutil.tz.gettz("Asia/Istanbul")),
    )


# TODO: add all custom epochs to this check


def valid_epoch_data(target_dtype):
    interpret = lambda x: interpret_iso_8601_string(x, target_dtype)
    if target_dtype in ("datetime", "datetime[pandas]"):
        series_type = None
    else:
        series_type = "O"

    case = lambda epoch, test_output: CastCase(
        {"dtype": target_dtype, "unit": "us", "since": epoch},
        pd.Series([True, False]),
        test_output
    )

    return CastParameters(
        # named epochs
        case("UTC", pd.Series([
            interpret("1970-01-01 00:00:00.000001000"),
            interpret("1970-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("reduced julian", pd.Series([
            interpret("1858-11-16 12:00:00.000001000"),
            interpret("1858-11-16 12:00:00.000000000")
        ], dtype=series_type)),

        # ISO 8601
        case("1941-12-07 08:00:00-1030", pd.Series([
            interpret("1941-12-07 18:30:00.000001000"),
            interpret("1941-12-07 18:30:00.000000000")
        ], dtype=series_type)),

        # parsed
        case("November 22, 1963 at 12:30 PM", pd.Series([
            interpret("1963-11-22 12:30:00.000001000"),
            interpret("1963-11-22 12:30:00.000000000")
        ], dtype=series_type)),

        # pd.Timestamp
        case(pd.Timestamp("1989-11-09 19:00:00", tz="Europe/Berlin"), pd.Series([
            interpret("1989-11-09 18:00:00.000001000"),
            interpret("1989-11-09 18:00:00.000000000")
        ], dtype=series_type)),

        # datetime.datetime
        case(datetime.datetime.fromisoformat("1918-11-11 11:00:00"), pd.Series([
            interpret("1918-11-11 11:00:00.000001000"),
            interpret("1918-11-11 11:00:00.000000000")
        ], dtype=series_type)),

        # np.datetime64
        case(np.datetime64("1789-07-14 20:00:00"), pd.Series([
            interpret("1789-07-14 20:00:00.000001000"),
            interpret("1789-07-14 20:00:00.000000000")
        ], dtype=series_type)),
    ).with_na(pd.NA, pd.NaT)


# TODO:
# unparseable epochs (excluding "now", "today")


# def invalid_epoch_data()


def irregular_unit_data(target_dtype):
    interpret = lambda x: interpret_iso_8601_string(x, target_dtype)
    if target_dtype in ("datetime", "datetime[pandas]"):
        series_type = None
    else:
        series_type = "O"

    case = lambda unit, epoch, test_output: CastCase(
        {"dtype": target_dtype, "unit": unit, "since": epoch},
        pd.Series([True, False]),
        test_output
    )

    months = [
        "01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12"
    ]

    return CastParameters(
        # months (non-leap year)
        CastParameters(*[
            case("M", f"1970-{months[i]}-01 00:00:00", pd.Series([
                interpret(
                    f"{1970 + (i + 1) // len(months)}-"  # roll into next year
                    f"{months[(i + 1) % len(months)]}-"  # one month periods
                    f"01 00:00:00"
                ),
                interpret(f"1970-{months[i]}-01 00:00:00")
            ], dtype=series_type))
            for i in range(len(months))
        ]),

        # months (leap year)
        CastParameters(*[
            case("M", f"1968-{months[i]}-01 00:00:00", pd.Series([
                interpret(
                    f"{1968 + (i + 1) // len(months)}-"  # roll into next year
                    f"{months[(i + 1) % len(months)]}-"  # one month periods
                    f"01 00:00:00"),
                interpret(f"1968-{months[i]}-01 00:00:00")
            ], dtype=series_type))
            for i in range(len(months))
        ]),

        # years
        CastParameters(*[
            case("Y", f"{year}-01-01 00:00:00", pd.Series([
                interpret(f"{year + 1}-01-01 00:00:00.000000000"),
                interpret(f"{year}-01-01 00:00:00.000000000")
            ], dtype=series_type))
            for year in [1968, 1969, 1970, 1971, 1972]
        ]),

        # Gregorian exceptions: centuries
        case("Y", "1900-01-01 00:00:00", pd.Series([
            interpret("1901-01-01 00:00:00.000000000"),
            interpret("1900-01-01 00:00:00.000000000")
        ], dtype=series_type)),
        case("Y", "2100-01-01 00:00:00", pd.Series([
            interpret("2101-01-01 00:00:00.000000000"),
            interpret("2100-01-01 00:00:00.000000000")
        ], dtype=series_type)),

        # Gregorian exceptions: 400-year cycles
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
####    TESTS    ####
#####################


@parametrize(input_format_data("datetime"))
def test_boolean_to_datetime_accepts_valid_input_data(case: CastCase):
    # valid
    if case.is_valid:
        result = BooleanSeries(case.input).to_datetime(**case.kwargs)
        assert result.equals(case.output), (
            f"BooleanSeries.to_datetime({case.signature()}) failed with "
            f"input:\n"
            f"{case.input}\n"
            f"expected:\n"
            f"{case.output}\n"
            f"received:\n"
            f"{result}"
        )

    # invalid
    else:
        with case.output as exc_info:
            BooleanSeries(case.input).to_datetime(**case.kwargs)
            pytest.fail(
                f"BooleanSeries.to_datetime({case.signature()}) did not "
                f"reject "
                f"input data:\n"
                f"{case.input}"
            )

        assert exc_info.type is TypeError


@parametrize(target_dtype_data("datetime"))
def test_boolean_to_datetime_accepts_datetime_type_specifiers(case: CastCase):
    # valid
    if case.is_valid:
        result = BooleanSeries(case.input).to_datetime(**case.kwargs)
        assert result.equals(case.output), (
            f"BooleanSeries.to_datetime({case.signature()}) failed with "
            f"input:\n"
            f"{case.input}\n"
            f"expected:\n"
            f"{case.output}\n"
            f"received:\n"
            f"{result}"
        )

    # invalid
    else:
        with case.output as exc_info:
            BooleanSeries(case.input).to_datetime(**case.kwargs)
            pytest.fail(  # called when no exception is encountered
                f"BooleanSeries.to_datetime({case.signature('dtype')}) did "
                f"not reject dtype={repr(case.kwargs['dtype'])}"
            )

        assert exc_info.type is TypeError
        assert exc_info.match("`dtype` must be datetime-like")


@parametrize(
    unit_data("datetime"),
    unit_data("datetime[pandas]"),
    unit_data("datetime[python]"),
    unit_data("datetime[numpy]"),
)
def test_boolean_to_datetime_handles_all_units(case: CastCase):
    # valid
    if case.is_valid:
        result = BooleanSeries(case.input).to_datetime(**case.kwargs)
        assert result.equals(case.output), (
            f"BooleanSeries.to_datetime({case.signature()}) failed with "
            f"input:\n"
            f"{case.input}\n"
            f"expected:\n"
            f"{case.output}\n"
            f"received:\n"
            f"{result}"
        )

    # invalid
    else:
        with case.output as exc_info:
            BooleanSeries(case.input).to_datetime(**case.kwargs)
            pytest.fail(
                f"BooleanSeries.to_datetime({case.signature('unit')}) did "
                f"not reject unit={repr(case.kwargs['unit'])}"
            )

        assert exc_info.type is ValueError
        # assert exc_info.match(
        #     "`from_unit` must be one of ('ns', 'us', 'ms', 's', 'm', 'h', "
        #     "'D', 'W', 'M', 'Y')"
        # )


@parametrize(
    valid_timezone_data("datetime"),
    valid_timezone_data("datetime[pandas]"),
    valid_timezone_data("datetime[python]"),
)
def test_boolean_to_datetime_handles_all_timezones(case: CastCase):
    # valid
    if case.is_valid:
        result = BooleanSeries(case.input).to_datetime(**case.kwargs)
        assert result.equals(case.output), (
            f"BooleanSeries.to_datetime({case.signature()}) failed with "
            f"input:\n"
            f"{case.input}\n"
            f"expected:\n"
            f"{case.output}\n"
            f"received:\n"
            f"{result}"
        )

    # invalid
    else:
        with case.output as exc_info:
            BooleanSeries(case.input).to_datetime(**case.kwargs)
            pytest.fail(
                f"BooleanSeries.to_datetime({case.signature('tz')}) did "
                f"not reject tz={repr(case.kwargs['tz'])}"
            )

        assert exc_info.type is pytz.exceptions.UnknownTimeZoneError


@parametrize(
    valid_epoch_data("datetime"),
    valid_epoch_data("datetime[pandas]"),
    valid_epoch_data("datetime[python]"),
    valid_epoch_data("datetime[numpy]"),
)
def test_boolean_to_datetime_handles_valid_epochs(case: CastCase):
    result = BooleanSeries(case.input).to_datetime(**case.kwargs)
    assert result.equals(case.output), (
        f"BooleanSeries.to_datetime({case.signature()}) failed with input:\n"
        f"{case.input}\n"
        f"expected:\n"
        f"{case.output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(
    irregular_unit_data("datetime"),
    irregular_unit_data("datetime[pandas]"),
    irregular_unit_data("datetime[python]"),
    irregular_unit_data("datetime[numpy]"),
)
def test_boolean_to_datetime_handles_irregular_month_and_year_units(
    case: CastCase
):
    result = BooleanSeries(case.input).to_datetime(**case.kwargs)
    assert result.equals(case.output), (
        f"BooleanSeries.to_datetime({case.signature()}) failed with input:\n"
        f"{case.input}\n"
        f"expected:\n"
        f"{case.output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(M8_data())
def test_boolean_to_datetime_handles_numpy_M8_dtypes_with_units_and_step_size(
    case: CastCase
):
    result = BooleanSeries(case.input).to_datetime(**case.kwargs)
    assert result.equals(case.output), (
        f"BooleanSeries.to_datetime({case.signature()}) failed with input:\n"
        f"{case.input}\n"
        f"expected:\n"
        f"{case.output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(unit_promotion_data())
def test_boolean_to_datetime_unit_promotion_with_datetime_and_M8_supertypes(
    case: CastCase
):
    result = BooleanSeries(case.input).to_datetime(**case.kwargs)
    assert result.equals(case.output), (
        f"BooleanSeries.to_datetime({case.signature()}) failed with input:\n"
        f"{case.input}\n"
        f"expected:\n"
        f"{case.output}\n"
        f"received:\n"
        f"{result}"
    )


@parametrize(timezone_promotion_data())
def test_boolean_to_datetime_timezone_promotion(case: CastCase):
    result = BooleanSeries(case.input).to_datetime(**case.kwargs)
    assert result.equals(case.output), (
        f"BooleanSeries.to_datetime({case.signature()}) failed with input:\n"
        f"{case.input}\n"
        f"expected:\n"
        f"{case.output}\n"
        f"received:\n"
        f"{result}"
    )


#######################
####    INVALID    ####
#######################


# @parametrize(invalid_unit_data())
# def test_boolean_to_datetime_rejects_invalid_units(
#     kwargs, test_input, test_output
# ):
#     with pytest.raises(ValueError):
#         BooleanSeries(test_input).to_datetime(**kwargs)

#         # custom error message
#         fmt_kwargs = ", ".join(
#             f"{k}={repr(v)}" for k, v in kwargs.items() if k != "unit"
#         )
#         pytest.fail(
#             f"BooleanSeries.to_datetime({fmt_kwargs}) did not reject "
#             f"unit={repr(kwargs['unit'])}"
#         )


# @parametrize(non_IANA_timezone_data())
# def test_boolean_to_datetime_rejects_non_IANA_timezones(
#     kwargs, test_input, test_output
# ):
#     with pytest.raises(pytz.exceptions.UnknownTimeZoneError):
#         BooleanSeries(test_input).to_datetime(**kwargs)

#         # custom error message
#         fmt_kwargs = ", ".join(
#             f"{k}={repr(v)}" for k, v in kwargs.items() if k != "tz"
#         )
#         pytest.fail(
#             f"BooleanSeries.to_datetime({fmt_kwargs}) did not reject "
#             f"tz={repr(kwargs['tz'])}"
#         )


@parametrize(M8_timezone_data())
def test_boolean_to_datetime_rejects_non_UTC_timezones_with_numpy_M8_types(case: CastCase):
    with case.output as exc_info:
        BooleanSeries(case.input).to_datetime(**case.kwargs)
        pytest.fail(
            f"BooleanSeries.to_datetime({case.signature('tz')}) did not "
            f"reject tz={repr(case.kwargs['tz'])}"
        )

    assert exc_info.type is RuntimeError
    # assert exc_info.match(
    #     r"numpy.datetime64 objects do not carry timezone information (must "
    #     r"be UTC or None)"
    # )



# @parametrize(invalid_epoch_data())
# def test_boolean_to_datetime_rejects_invalid_epochs(
#     kwargs, test_input, test_output
# ):


# TODO: invalid
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
    interpret = lambda x: interpret_iso_8601_string(x, target_dtype)
    if target_dtype in ("datetime", "datetime[pandas]"):
        series_type = None
    else:
        series_type = "O"

    # arrange
    case = CastCase(
        {"dtype": target_dtype, "unit": "s"},
        pd.Series(
            [True, False, pd.NA],
            index=[4, 5, 6],
            dtype=pd.BooleanDtype()
        ),
        pd.Series(
            [
                interpret("1970-01-01 00:00:01.000000000"),
                interpret("1970-01-01 00:00:00.000000000"),
                pd.NaT
            ],
            index=[4, 5, 6],
            dtype=series_type
        )
    )

    # act
    result = BooleanSeries(case.input).to_datetime(**case.kwargs)

    # assert
    assert result.equals(case.output), (
        f"BooleanSeries.to_datetime({case.signature()}) does not preserve "
        f"index.\n"
        f"expected:\n"
        f"{case.output}\n"
        f"received:\n"
        f"{result}"
    )
