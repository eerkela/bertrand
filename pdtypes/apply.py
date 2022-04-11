from __future__ import annotations
from functools import cache
from datetime import datetime, timedelta, timezone
from typing import Any, Callable

import numpy as np
import pandas as pd

from pdtypes.error import error_trace
from pdtypes.parse import parse_dtype, parse_string, to_utc



CONVERSIONS = {
    int: {
        float: _integer_to_float,
        complex: _integer_to_complex,
        str: _integer_to_string,
        bool: _integer_to_boolean,
        datetime: _integer_to_datetime,
        timedelta: _integer_to_timedelta
    }
}


def _integer_to_float(
    element: int,
    return_type: type = float
) -> float:
    if pd.isnull(element):
        return np.nan
    return return_type(element)


def _integer_to_complex(
    element: int,
    return_type: type = complex
) -> complex:
    if pd.isnull(element):
        return np.nan
    return return_type(element, 0)


def _integer_to_string(
    element: int,
    return_type: type = str
) -> str:
    if pd.isnull(element):
        return pd.NA
    return return_type(element)


def _integer_to_boolean(
    element: int,
    force: bool = False,
    return_type: type = bool
) -> bool:
    if pd.isnull(element):
        return pd.NA
    result = return_type(element)
    if force or result == element:
        return result
    err_msg = (f"[{error_trace()}] could not convert int to bool: "
               f"{repr(element)}")
    raise ValueError(err_msg)


def _integer_to_datetime(
    element: int,
    return_type: type = pd.Timestamp
) -> datetime | pd.Timestamp:
    if pd.isnull(element):
        return pd.NaT
    return return_type(element, "UTC")


def _integer_to_timedelta(
    element: int,
    return_type: type = pd.Timedelta
) -> timedelta | pd.Timedelta:
    if pd.isnull(element):
        return pd.NaT
    return return_type(seconds=element)


def _float_to_integer(
    element: float,
    force: bool = False,
    round: bool = False,
    ftol: float = 1e-6,
    return_type: type = int
) -> int:
    if pd.isnull(element):
        return np.nan
    if round:
        return return_type(np.round(element))
    result = return_type(element)
    if force or abs(result - element) < ftol:
        return result
    err_msg = (f"[{error_trace()}] could not convert float to int: "
               f"{repr(element)}")
    raise ValueError(err_msg)


def _float_to_complex(
    element: float,
    return_type: complex
) -> complex:
    if pd.isnull(element):
        return np.nan
    return return_type(element)


def _float_to_string(
    element: float,
    return_type: str
) -> str:
    if pd.isnull(element):
        return pd.NA
    return return_type(element)


def _float_to_boolean(
    element: float,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = bool
) -> bool:
    if pd.isnull(element):
        return pd.NA
    result = return_type(element)
    if force or abs(result - element) < 1e-6:
        return result
    err_msg = (f"[{error_trace()}] could not convert float to bool: "
               f"{repr(element)}")
    raise ValueError(err_msg)


def _float_to_datetime(
    element: float,
    return_type: type = pd.Timestamp
) -> datetime | pd.Timestamp:
    if pd.isnull(element):
        return pd.NaT
    return return_type.fromtimestamp(element, tz=timezone.utc)


def _float_to_timedelta(
    element: float,
    return_type: type = pd.Timedelta
) -> timedelta | pd.Timedelta:
    if pd.isnull(element):
        return pd.NaT
    return return_type(seconds=element)


def _complex_to_integer(
    element: complex,
    force: bool = False,
    round: bool = False,
    ftol: float = 1e-6,
    return_type: type = int
) -> int:
    if pd.isnull(element):
        return np.nan
    if round:
        result = return_type(np.round(element.real))
    else:
        result = return_type(element.real)
    if force or abs(result - element) < ftol:
        return result
    err_msg = (f"[{error_trace()}] could not convert complex to int: "
               f"{repr(element)}")
    raise ValueError(err_msg)


def _complex_to_float(
    element: complex,
    force: bool = False,
    return_type: type = float
) -> float:
    if pd.isnull(element):
        return np.nan
    if force or not element.imag:
        return return_type(element.real)
    err_msg = (f"[{error_trace()}] could not convert complex to float: "
               f"{repr(element)}")
    raise ValueError(err_msg)


def _complex_to_string(
    element: complex,
    return_type: type = str
) -> str:
    if pd.isnull(element):
        return pd.NA
    return return_type(element)


def _complex_to_boolean(
    element: complex,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = bool
) -> bool:
    if pd.isnull(element):
        return pd.NA
    result = return_type(element.real)
    if force or abs(result - element) < ftol:
        return result
    err_msg = (f"[{error_trace()}] could not convert complex to bool: "
               f"{repr(element)}")
    raise ValueError(err_msg)


def _complex_to_datetime(
    element: complex,
    force: bool = False,
    return_type: type = pd.Timestamp
) -> datetime | pd.Timestamp:
    if pd.isnull(element):
        return pd.NaT
    if force or not element.imag:
        return return_type.fromtimestamp(element.real, "UTC")
    err_msg = (f"[{error_trace()}] could not convert complex to datetime: "
               f"{repr(element)}")
    raise ValueError(err_msg)


def _complex_to_timedelta(
    element: complex,
    force: bool = False,
    return_type: type = pd.Timedelta
) -> timedelta | pd.Timedelta:
    if pd.isnull(element):
        return pd.NaT
    if force or not element.imag:
        return return_type(seconds=element.real)
    err_msg = (f"[{error_trace()}] could not convert complex to timedelta: "
               f"{repr(element)}")
    raise ValueError(err_msg)


def _string_to_integer(
    element: str,
    force: bool = False,
    round: bool = False,
    ftol: float = 1e-6,
    return_type: type = int
) -> int:
    if pd.isnull(element):
        return np.nan

    parsed = parse_string(element)
    parsed_dtype = parse_dtype(parsed)
    err_msg = (f"[{error_trace()}] could not convert str to int: "
               f"{repr(element)}")

    if parsed_dtype == int:
        return return_type(parsed)

    if parsed_dtype == float:
        if round:
            return return_type(np.round(parsed))
        result = return_type(parsed)
        if force or abs(result - parsed) < ftol:
            return result
        raise ValueError(err_msg)

    if parsed_dtype == complex:
        result = return_type(parsed.real)
        if force or abs(result - parsed) < ftol:
            return result
        raise ValueError(err_msg)

    if parsed_dtype == str:
        raise ValueError(err_msg)

    if parsed_dtype == bool:
        return return_type(parsed)

    if parsed_dtype == datetime:
        utc = to_utc(parsed)
        timestamp = utc.timestamp()
        if round:
            return return_type(np.round(timestamp))
        result = return_type(timestamp)
        if abs(result - timestamp) < ftol or force:
            return result
        raise ValueError(err_msg)

    if parsed_dtype == timedelta:
        seconds = parsed.total_seconds()
        if round:
            return return_type(np.round(seconds))
        result = return_type(seconds)
        if abs(result - seconds) < ftol or force:
            return result
        raise ValueError(err_msg)

    raise ValueError(err_msg)


def _string_to_float(
    element: str,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = float
) -> float:
    if pd.isnull(element):
        return np.nan

    parsed = parse_string(element)
    parsed_dtype = parse_dtype(parsed)
    err_msg = (f"[{error_trace()}] could not convert str to int: "
               f"{repr(element)}")

    if parsed_dtype == int:
        return return_type(parsed)

    if parsed_dtype == float:
        return return_type(parsed)

    if parsed_dtype == complex:
        result = return_type(parsed.real)
        if parsed.imag < ftol or force:
            return result
        raise ValueError(err_msg)

    if parsed_dtype == str:
        raise ValueError(err_msg)

    if parsed_dtype == bool:
        return return_type(parsed)

    if parsed_dtype == datetime:
        utc = to_utc(parsed)
        return return_type(utc.timestamp())

    if parsed_dtype == timedelta:
        return return_type(parsed.total_seconds())

    raise ValueError(err_msg)


def _string_to_complex(
    element: str,
    return_type: type = float
) -> complex:
    if pd.isnull(element):
        return np.nan

    parsed = parse_string(element)
    parsed_dtype = parse_dtype(parsed)
    err_msg = (f"[{error_trace()}] could not convert str to complex: "
               f"{repr(element)}")

    if parsed_dtype == int:
        return return_type(parsed)

    if parsed_dtype == float:
        return return_type(parsed)

    if parsed_dtype == complex:
        return return_type(parsed)

    if parsed_dtype == str:
        return return_type(parsed)

    if parsed_dtype == bool:
        return return_type(parsed)

    if parsed_dtype == datetime:
        utc = to_utc(parsed)
        return return_type(utc.timestamp())

    if parsed_dtype == timedelta:
        return return_type(parsed.total_seconds())

    raise ValueError(err_msg)


def _string_to_boolean(
    element: str,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = bool
) -> bool:
    if pd.isnull(element):
        return pd.NA

    parsed = parse_string(element)
    parsed_dtype = parse_dtype(parsed)
    err_msg = (f"[{error_trace()}] could not convert str to bool: "
               f"{repr(element)}")

    if parsed_dtype == int:
        result = return_type(parsed)
        if abs(result - parsed) < ftol or force:
            return result
        raise ValueError(err_msg)








@cache
def to_integer(element: Any,
               force: bool = False,
               ftol: float = 1e-6,
               round: bool = False) -> np.nan | int:
    if pd.isnull(element):
        return np.nan

    from_type = parse_dtype(type(element))
    err_msg = (f"[{error_trace()}] could not convert {from_type.__name__} to "
               f"int: {repr(element)}")

    if from_type == int:
        return element

    if from_type == float:
        result = int(element)
        if force or result == element:
            return result
        raise ValueError(err_msg)

    if from_type == complex:
        result = int(element.real)
        if force or result == element:
            return result
        raise ValueError(err_msg)

    if from_type == str:
        parsed = parse_string(element)
        dtype = parse_dtype(type(parsed))
        if dtype == int:
            return parsed
        if dtype == float:
            result = int(parsed)
            if force or result == parsed:
                return result
            raise ValueError(err_msg)
        if dtype == complex:
            result = int(parsed.real)
            if force or result == parsed:
                return result
            raise ValueError(err_msg)
        if dtype == str:
            raise ValueError(err_msg)
        if dtype == bool:
            return int(parsed)
        if dtype == datetime:
            utc = to_utc(parsed)
            timestamp = utc.timestamp()
            result = int(timestamp)
            if force or result == timestamp:
                return result
            raise ValueError(err_msg)
        if dtype == timedelta:
            seconds = parsed.total_seconds()
            result = int(seconds)
            if force or result == seconds:
                return result
            raise ValueError(err_msg)

    if from_type == bool:
        return int(element)

    if from_type == datetime:
        utc = to_utc(element)
        timestamp = utc.timestamp()
        result = int(timestamp)
        if force or result == timestamp:
            return result
        raise ValueError(err_msg)

    if from_type == timedelta:
        seconds = element.total_seconds()
        result = int(seconds)
        if force or result == seconds:
            return result
        raise ValueError(err_msg)

    if from_type == object:
        try:
            try:
                return int(element)
            except TypeError as err:
                context = f"[{error_trace()}] object has no __int__ method"
                raise ValueError(context) from err
        except ValueError as err:
            raise ValueError(err_msg) from err

    raise RuntimeError(err_msg)


@cache
def to_float(element: Any, force: bool = False) -> np.nan | float:
    if pd.isnull(element):
        return np.nan

    from_type = parse_dtype(type(element))
    err_msg = (f"[{error_trace()}] could not convert {from_type.__name__} to "
               f"float: {repr(element)}")

    if from_type == int:
        return float(element)

    if from_type == float:
        return element

    if from_type == complex:
        result = element.real
        if force or result == element:
            return result
        raise ValueError(err_msg)

    if from_type == str:
        parsed = parse_string(element)
        dtype = parse_dtype(type(parsed))
        if dtype == int:
            return float(parsed)
        if dtype == float:
            return parsed
        if dtype == complex:
            return complex(parsed, 0)
        if dtype == str:
            raise ValueError(err_msg)
        if dtype == bool:
            return float(parsed)
        if dtype == datetime:
            return parsed.timestamp()
        if dtype == timedelta:
            return parsed.total_seconds()

    if from_type == bool:
        return float(element)

    if from_type == datetime:
        return element.timestamp()

    if from_type == timedelta:
        return element.total_seconds()

    if from_type == object:
        try:
            try:
                return float(element)
            except TypeError as err:
                context = f"[{error_trace()}] object has no __float__ method"
                raise ValueError(context) from err
        except ValueError as err:
            raise ValueError(err_msg) from err

    raise RuntimeError(err_msg)


@cache
def to_complex(element: Any, force: bool = False) -> np.nan | complex:
    if pd.isnull(element):
        return np.nan

    from_type = parse_dtype(type(element))
    err_msg = (f"[{error_trace()}] could not convert {from_type.__name__} to "
               f"complex: {repr(element)}")

    if from_type == int:
        return complex(element, 0)

    if from_type == float:
        return complex(element, 0)

    if from_type == complex:
        return element

    if from_type == str:
        parsed = parse_string(element)
        dtype = parse_dtype(type(parsed))
        if dtype == int:
            return complex(parsed, 0)
        if dtype == float:
            return complex(parsed, 0)
        if dtype == complex:
            return parsed
        if dtype == str:
            raise ValueError(err_msg)
        if dtype == bool:
            return complex(parsed, 0)
        if dtype == datetime:
            return complex(parsed.timestamp(), 0)
        if dtype == timedelta:
            return complex(parsed.total_seconds(), 0)

    if from_type == bool:
        return complex(element, 0)

    if from_type == datetime:
        return complex(element.timestamp(), 0)

    if from_type == timedelta:
        return complex(element.total_seconds(), 0)

    if from_type == object:
        try:
            try:
                return complex(element)
            except TypeError as err:
                context = f"[{error_trace()}] object has no __complex__ method"
                raise ValueError(context) from err
        except ValueError as err:
            raise ValueError(err_msg) from err

    raise RuntimeError(err_msg)


@cache
def to_boolean(element: Any, force: bool = False) -> pd.NA | bool:
    if pd.isnull(element):
        return pd.NA

    from_type = parse_dtype(type(element))
    err_msg = (f"[{error_trace()}] could not convert {from_type.__name__} to "
               f"bool: {repr(element)}")

    if from_type == int:
        result = bool(element)
        if force or result == element:
            return result
        raise ValueError(err_msg)

    if from_type == float:
        result = bool(element)
        if force or result == element:
            return result
        raise ValueError(err_msg)

    if from_type == complex:
        result = bool(element.real)
        if force or result == element:
            return result
        raise ValueError(err_msg)

    if from_type == str:
        parsed = parse_string(element)
        dtype = parse_dtype(type(parsed))
        if dtype == int:
            result = bool(parsed)
            if force or result == parsed:
                return result
            raise ValueError(err_msg)
        if dtype == float:
            result = bool(parsed)
            if force or result == parsed:
                return result
            raise ValueError(err_msg)
        if dtype == complex:
            result = bool(parsed)
            if force or result == parsed:
                return result
            raise ValueError(err_msg)
        if dtype == str:
            raise ValueError(err_msg)
        if dtype == bool:
            return parsed
        if dtype == datetime:
            timestamp = parsed.timestamp()
            result = bool(timestamp)
            if force or result == timestamp:
                return result
            raise ValueError(err_msg)
        if dtype == timedelta:
            seconds = parsed.total_seconds()
            result = bool(seconds)
            if force or result == seconds:
                return result
            raise ValueError(err_msg)

    if from_type == bool:
        return element

    if from_type == datetime:
        timestamp = element.timestamp()
        result = bool(timestamp)
        if force or result == timestamp:
            return result
        raise ValueError(err_msg)

    if from_type == timedelta:
        seconds = element.total_seconds()
        result = bool(seconds)
        if force or result == seconds:
            return result
        raise ValueError(err_msg)

    if from_type == object:
        try:
            try:
                return bool(element)
            except TypeError as err:
                context = f"[{error_trace()}] object has no __bool__ method"
                raise ValueError(context) from err
        except ValueError as err:
            raise ValueError(err_msg) from err

    raise RuntimeError(err_msg)


@cache
def to_string(element: Any, force: bool = False) -> pd.NA | str:
    if pd.isnull(element):
        return pd.NA

    from_type = parse_dtype(type(element))
    err_msg = (f"[{error_trace()}] could not convert {from_type.__name__} to "
               f"string: {repr(element)}")

    if from_type == int:
        return str(element)

    if from_type == float:
        return str(element)

    if from_type == complex:
        return str(element)

    if from_type == str:
        return element

    if from_type == bool:
        return str(element)

    if from_type == datetime:
        return element.isoformat()

    if from_type == timedelta:
        return str(element)

    if from_type == object:
        try:
            try:
                return str(element)
            except TypeError as err:
                context = f"[{error_trace()}] object has no __str__ method"
                raise ValueError(context) from err
        except ValueError as err:
            raise ValueError(err_msg) from err

    raise RuntimeError(err_msg)


@cache
def to_datetime(element: Any,
                force: bool = False,
                *args,
                format: str | None = None,
                func: Callable | None = None,
                **kwargs) -> pd.NaT | pd.Timestamp:
    if pd.isnull(element):
        return pd.NaT

    from_type = parse_dtype(type(element))
    err_msg = (f"[{error_trace()}] could not convert {from_type.__name__} to "
               f"datetime: {repr(element)}")

    if from_type == int:
        return pd.Timestamp.fromtimestamp(element, "UTC")

    if from_type == float:
        return pd.Timestamp.fromtimestamp(element, "UTC")

    if from_type == complex:
        if force or element.imag == 0:
            return pd.Timestamp.fromtimestamp(element.real, "UTC")
        raise ValueError(err_msg)

    if from_type == str:
        parsed = parse_string(element)
        dtype = parse_dtype(type(parsed))
        if dtype == int:
            return pd.Timestamp.fromtimestamp(parsed, "UTC")
        if dtype == float:
            return pd.Timestamp.fromtimestamp(parsed, "UTC")
        if dtype == complex:
            if force or parsed.imag == 0:
                return pd.Timestamp.fromtimestamp(parsed.real, "UTC")
            raise ValueError(err_msg)
        if dtype == str:
            raise ValueError(err_msg)
        if dtype == bool:
            return pd.Timestamp.fromtimestamp(parsed, "UTC")
        if dtype == datetime:
            return pd.Timestamp.fromtimestamp(parsed.timestamp(), "UTC")
        if dtype == timedelta:
            return pd.Timestamp.fromtimestamp(parsed.total_seconds(), "UTC")

    if from_type == bool:
        return pd.Timestamp.fromtimestamp(parsed, "UTC")

    if from_type == datetime:
        return pd.Timestamp.fromtimestamp(element.timestamp(), "UTC")

    if from_type == timedelta:
        return pd.Timestamp.fromtimestamp(element.total_seconds(), "UTC")

    if from_type == object:
        try:
            try:
                timestamp = element.to_datetime()
                return pd.Timestamp(timestamp.timestamp(), "UTC")
            except TypeError as err:
                context = f"[{error_trace()}] object has no to_datetime method"
                raise ValueError(context) from err
        except ValueError as err:
            raise ValueError(err_msg) from err

    raise RuntimeError(err_msg)


@cache
def to_timedelta(element: Any, force: bool = False) -> pd.NaT | pd.Timedelta:
    if pd.isnull(element):
        return pd.NaT

    from_type = parse_dtype(type(element))
    err_msg = (f"[{error_trace()}] could not convert {from_type.__name__} to "
               f"timedelta: {repr(element)}")

    if from_type == int:
        return pd.Timedelta(seconds=element)

    if from_type == float:
        return pd.Timedelta(seconds=element)

    if from_type == complex:
        if force or element.imag == 0:
            return pd.Timedelta(seconds=element.real)
        raise ValueError(err_msg)

    if from_type == str:
        parsed = parse_string(element)
        dtype = parse_dtype(type(parsed))
        if dtype == int:
            return pd.Timedelta(seconds=parsed)
        if dtype == float:
            return pd.Timedelta(seconds=parsed)
        if dtype == complex:
            if force or parsed.imag == 0:
                return pd.Timedelta(seconds=parsed.real)
        if dtype == str:
            raise ValueError(err_msg)
        if dtype == bool:
            return pd.Timedelta(seconds=parsed)
        if dtype == datetime:
            utc = pd.Timestamp.fromtimestamp(parsed.timestamp(), "UTC")
            return pd.Timedelta(seconds=utc.timestamp())
        if dtype == timedelta:
            return parsed

    if from_type == bool:
        return pd.Timedelta(seconds=element)

    if from_type == datetime:
        utc = pd.Timestamp.fromtimestamp(element.timestamp(), "UTC")
        return pd.Timedelta(seconds=utc.timestamp())

    if from_type == timedelta:
        return element

    if from_type == object:
        try:
            try:
                seconds = element.to_timedelta().total_seconds()
                return pd.Timestamp.fromtimestamp(seconds, tz="UTC")
            except TypeError as err:
                context = f"[{error_trace()}] object has no to_timedelta method"
                raise ValueError(context) from err
        except ValueError as err:
            raise ValueError(err_msg) from err

    raise RuntimeError(err_msg)
