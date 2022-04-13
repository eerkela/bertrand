from __future__ import annotations
from functools import cache, partial
from datetime import datetime, timedelta, timezone
from typing import Any, Callable

import numpy as np
import pandas as pd

from pdtypes.error import error_trace
from pdtypes.parse import parse_dtype, parse_string, to_utc


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
    ftol: float = 1e-6,
    return_type: type = bool
) -> bool:
    if pd.isnull(element):
        return pd.NA
    result = return_type(element)
    if force or abs(result - element) < ftol:
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
    return return_type(element, timezone.utc)


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
    return return_type.fromtimestamp(element, timezone.utc)


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
    ftol: float = 1e-6,
    return_type: type = float
) -> float:
    if pd.isnull(element):
        return np.nan
    if abs(element.imag) < ftol or force:
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
    ftol: float = 1e-6,
    return_type: type = pd.Timestamp
) -> datetime | pd.Timestamp:
    if pd.isnull(element):
        return pd.NaT
    if force or abs(element.imag) < ftol:
        return return_type.fromtimestamp(element.real, timezone.utc)
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
    conversion_map = {
        int:
            lambda x: return_type(x),
        float:
            partial(_float_to_integer, round=round, force=force,
                    ftol=ftol, return_type=return_type),
        complex:
            partial(_complex_to_integer, round=round, force=force,
                    ftol=ftol, return_type=return_type),
        bool:
            partial(_boolean_to_integer, return_type=return_type),
        datetime:
            partial(_datetime_to_integer, round=round, force=force,
                    ftol=ftol, return_type=return_type),
        timedelta:
            partial(_timedelta_to_integer, round=round, force=force,
                    ftol=ftol, return_type=return_type)
    }
    parsed = parse_string(element)
    parsed_dtype = parse_dtype(parsed)
    try:
        return conversion_map[parsed_dtype](parsed)
    except (KeyError, ValueError):
        err_msg = (f"[{error_trace()}] could not convert str to int: "
                   f"{repr(element)}")
        raise ValueError(err_msg)


def _string_to_float(
    element: str,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = float
) -> float:
    if pd.isnull(element):
        return np.nan
    conversion_map = {
        int:
            partial(_integer_to_float, return_type=return_type),
        float:
            lambda x: return_type(x),
        complex:
            partial(_complex_to_float, force=force, ftol=ftol,
                    return_type=return_type),
        bool:
            partial(_boolean_to_float, return_type=return_type),
        datetime:
            partial(_datetime_to_float, return_type=return_type),
        timedelta:
            partial(_timedelta_to_float, return_type=return_type)
    }
    parsed = parse_string(element)
    parsed_dtype = parse_dtype(parsed)    
    try:
        return conversion_map[parsed_dtype](parsed)
    except (KeyError, ValueError):
        err_msg = (f"[{error_trace()}] could not convert str to float: "
                f"{repr(element)}")
        raise ValueError(err_msg)


def _string_to_complex(
    element: str,
    return_type: type = float
) -> complex:
    if pd.isnull(element):
        return np.nan
    conversion_map = {
        int:
            partial(_integer_to_complex, return_type=return_type),
        float:
            partial(_float_to_complex, return_type=return_type),
        complex:
            lambda x: return_type(x),
        bool:
            partial(_boolean_to_complex, return_type=return_type),
        datetime:
            partial(_datetime_to_complex, return_type=return_type),
        timedelta:
            partial(_timedelta_to_complex, return_type=return_type)
    }
    parsed = parse_string(element)
    parsed_dtype = parse_dtype(parsed)
    try:
        return conversion_map[parsed_dtype](parsed)
    except (KeyError, ValueError):
        err_msg = (f"[{error_trace()}] could not convert str to complex: "
                   f"{repr(element)}")
        raise ValueError(err_msg)


def _string_to_boolean(
    element: str,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = bool
) -> bool:
    if pd.isnull(element):
        return pd.NA
    conversion_map = {
        int:
            partial(_integer_to_boolean, force=force, return_type=return_type),
        float:
            partial(_float_to_boolean, force=force, ftol=ftol,
                    return_type=return_type),
        complex:
            partial(_complex_to_boolean, force=force, ftol=ftol,
                    return_type=return_type),
        bool:
            lambda x: return_type(x),
        datetime:
            partial(_datetime_to_boolean, force=force, ftol=ftol,
                    return_type=return_type),
        timedelta:
            partial(_timedelta_to_boolean, force=force, ftol=ftol,
                    return_type=return_type)
    }
    parsed = parse_string(element)
    parsed_dtype = parse_dtype(parsed)
    try:
        return conversion_map[parsed_dtype](parsed)
    except (KeyError, ValueError):
        err_msg = (f"[{error_trace()}] could not convert str to bool: "
                   f"{repr(element)}")
        raise ValueError(err_msg)


def _string_to_datetime(
    element: str,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = pd.Timestamp
) -> pd.Timestamp | datetime:
    if pd.isnull(element):
        return pd.NaT
    conversion_map = {
        int:
            partial(_integer_to_datetime, return_type=return_type),
        float:
            partial(_float_to_datetime, return_type=return_type),
        complex:
            partial(_complex_to_datetime, force=force, ftol=ftol,
                    return_type=return_type),
        bool:
            partial(_boolean_to_datetime, return_type(return_type)),
        datetime:
            lambda x: x if return_type == datetime else return_type(x),
        timedelta:
            partial(_timedelta_to_datetime, return_type=return_type)
    }
    parsed = parse_string(element)
    parsed_dtype = parse_dtype(parsed)
    try:
        return conversion_map[parsed_dtype](parsed)
    except (KeyError, ValueError):
        err_msg = (f"[{error_trace()}] could not convert str to datetime: "
                   f"{repr(element)}")
        raise ValueError(err_msg)


def _string_to_timedelta(
    element: str,
    force: bool = False,
    return_type: type = pd.Timedelta
) -> pd.Timedelta | timedelta:
    if pd.isnull(element):
        return pd.NaT
    conversion_map = {
        int:
            partial(_integer_to_timedelta, return_type=return_type),
        float:
            partial(_float_to_timedelta, return_type=return_type),
        complex:
            partial(_complex_to_timedelta, force=force,
                    return_type=return_type),
        bool:
            partial(_boolean_to_timedelta, return_type=return_type),
        datetime:
            partial(_datetime_to_timedelta, return_type=return_type),
        timedelta:
            lambda x: return_type(seconds=element.total_seconds())
    }
    parsed = parse_string
    parsed_dtype = parse_dtype(parsed)
    try:
        return conversion_map[parsed_dtype](parsed)
    except (KeyError, ValueError):
        err_msg = (f"[{error_trace()}] could not convert str to timedelta: "
                   f"{repr(element)}")
        raise ValueError(err_msg)


def _boolean_to_integer(
    element: bool,
    return_type: type = int
) -> int:
    if pd.isnull(element):
        return np.nan
    return return_type(element)


def _boolean_to_float(
    element: bool,
    return_type: type = float
) -> float:
    if pd.isnull(element):
        return np.nan
    return return_type(element)


def _boolean_to_complex(
    element: bool,
    return_type: type = complex
) -> complex:
    if pd.isnull(element):
        return np.nan
    return return_type(element)


def _boolean_to_string(
    element: bool,
    return_type: type = str
) -> str:
    if pd.isnull(element):
        return pd.NA
    return return_type(element)


def _boolean_to_datetime(
    element: bool,
    return_type: type = pd.Timestamp
) -> pd.Timestamp | datetime:
    if pd.isnull(element):
        return pd.NaT
    return return_type.fromtimestamp(element, timezone.utc)


def _boolean_to_timedelta(
    element: bool,
    return_type: type = pd.Timedelta
) -> pd.Timedelta | timedelta:
    if pd.isnull(element):
        return pd.NaT
    return return_type.fromtimestamp(seconds=element)


def _datetime_to_integer(
    element: pd.Timestamp | datetime,
    round: bool = False,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = int
) -> int:
    if pd.isnull(element):
        return np.nan
    timestamp = to_utc(element).timestamp()
    if round:
        return return_type(np.round(timestamp))
    result = return_type(timestamp)
    if abs(result - timestamp) < ftol or force:
        return result
    err_msg = (f"[{error_trace()}] could not convert datetime to int: "
               f"{repr(element)}")
    raise ValueError(err_msg)


def _datetime_to_float(
    element: pd.Timestamp | datetime,
    return_type: type = float
) -> float:
    if pd.isnull(element):
        return np.nan
    return return_type(to_utc(element).timestamp())


def _datetime_to_complex(
    element: pd.Timestamp | datetime,
    return_type: type = complex
) -> complex:
    if pd.isnull(element):
        return np.nan
    return return_type(to_utc(element).timestamp())


def _datetime_to_string(
    element: pd.Timestamp | datetime,
    return_type: type = str
) -> str:
    if pd.isnull(element):
        return pd.NA
    return return_type(to_utc(element).isoformat())


def _datetime_to_boolean(
    element: pd.Timestamp | datetime,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = bool
) -> bool:
    if pd.isnull(element):
        return pd.NA
    timestamp = to_utc(element).timestamp()
    result = return_type(timestamp)
    if abs(result - timestamp) < ftol or force:
        return result
    err_msg = (f"[{error_trace()}] could not convert datetime to bool: "
               f"{repr(element)}")
    raise ValueError(err_msg)


def _datetime_to_timedelta(
    element: pd.Timestamp | datetime,
    return_type: type = pd.Timedelta
) -> pd.Timedelta | timedelta:
    if pd.isnull(element):
        return pd.NaT
    return return_type(seconds=to_utc(element).timestamp())


def _timedelta_to_integer(
    element: pd.Timedelta | timedelta,
    round: bool = False,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = int
) -> int:
    if pd.isnull(element):
        return np.nan
    seconds = element.total_seconds()
    if round:
        return return_type(np.round(seconds))
    result = return_type(seconds)
    if abs(result - seconds) < ftol or force:
        return result
    err_msg = (f"[{error_trace()}] could not convert timedelta to int: "
               f"{repr(element)}")
    raise ValueError(err_msg)


def _timedelta_to_float(
    element: pd.Timedelta | timedelta,
    return_type: type = float
) -> float:
    if pd.isnull(element):
        return np.nan
    return return_type(element.total_seconds())


def _timedelta_to_complex(
    element: pd.Timedelta | timedelta,
    return_type: type = complex
) -> complex:
    if pd.isnull(element):
        return np.nan
    return return_type(element.total_seconds())


def _timedelta_to_string(
    element: pd.Timedelta | timedelta,
    return_type: type = str
) -> str:
    if pd.isnull(element):
        return pd.NA
    return return_type(element)


def _timedelta_to_boolean(
    element: pd.Timedelta | timedelta,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = bool
) -> bool:
    if pd.isnull(element):
        return pd.NA
    seconds = element.total_seconds()
    result = return_type(seconds)
    if abs(result - seconds) < ftol or force:
        return result
    err_msg = (f"[{error_trace()}] could not convert timedelta to bool: "
               f"{repr(element)}")
    raise ValueError(err_msg)


def _timedelta_to_datetime(
    element: pd.Timedelta | timedelta,
    return_type: type = pd.Timestamp
) -> pd.Timestamp | datetime:
    if pd.isnull(element):
        return pd.NaT
    return return_type.fromtimestamp(element.total_seconds(), timezone.utc)


@cache
def to_integer(
    element: Any,
    round: bool = False,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = int
) -> np.nan | int:
    if pd.isnull(element):
        return np.nan
    conversion_map = {
        int:
            lambda x: return_type(element),
        float:
            partial(_float_to_integer, round=round, force=force, ftol=ftol,
                    return_type=return_type),
        complex:
            partial(_complex_to_integer, round=round, force=force, ftol=ftol,
                    return_type=return_type),
        str:
            partial(_string_to_integer, round=round, force=force, ftol=ftol,
                    return_type=return_type),
        bool:
            partial(_boolean_to_integer, return_type=return_type),
        datetime:
            partial(_datetime_to_integer, round=round, force=force, ftol=ftol,
                    return_type=return_type),
        timedelta:
            partial(_timedelta_to_integer, round=round, force=force, ftol=ftol,
                    return_type=return_type)
    }
    from_type = parse_dtype(type(element))
    try:
        return conversion_map[from_type](element)
    except (KeyError, ValueError) as err:
        err_msg = (f"[{error_trace()}] could not convert element to int: "
                   f"{repr(element)}")
        raise ValueError(err_msg) from err

    # if from_type == object:
    #     try:
    #         try:
    #             return int(element)
    #         except TypeError as err:
    #             context = f"[{error_trace()}] object has no __int__ method"
    #             raise ValueError(context) from err
    #     except ValueError as err:
    #         raise ValueError(err_msg) from err



@cache
def to_float(
    element: Any,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = float
) -> np.nan | float:
    if pd.isnull(element):
        return np.nan
    conversion_map = {
        int:
            partial(_integer_to_float, return_type=return_type),
        float:
            lambda x: return_type(x),
        complex:
            partial(_complex_to_float, force=force, ftol=ftol,
                    return_type=return_type),
        str:
            partial(_string_to_float, force=force, ftol=ftol,
                    return_type=return_type),
        bool:
            partial(_boolean_to_float, return_type=return_type),
        datetime:
            partial(_datetime_to_float, return_type=return_type),
        timedelta:
            partial(_timedelta_to_float, return_type=return_type)
    }
    from_type = parse_dtype(type(element))
    try:
        return conversion_map[from_type](element)
    except (KeyError, ValueError) as err:
        err_msg = (f"[{error_trace()}] could not convert element to float: "
                   f"{repr(element)}")
        raise ValueError(err_msg) from err

    # if from_type == object:
    #     try:
    #         try:
    #             return float(element)
    #         except TypeError as err:
    #             context = f"[{error_trace()}] object has no __float__ method"
    #             raise ValueError(context) from err
    #     except ValueError as err:
    #         raise ValueError(err_msg) from err



@cache
def to_complex(
    element: Any,
    force: bool = False,
    return_type: type = complex
) -> np.nan | complex:
    if pd.isnull(element):
        return np.nan
    conversion_map = {
        int:
            partial(_integer_to_complex, return_type=return_type),
        float:
            partial(_float_to_complex, return_type=return_type),
        complex:
            lambda x: return_type(x),
        str:
            partial(_string_to_complex, return_type=return_type),
        bool:
            partial(_boolean_to_complex, return_type=return_type),
        datetime:
            partial(_datetime_to_complex, return_type=return_type),
        timedelta:
            partial(_timedelta_to_complex, return_type=return_type)
    }
    from_type = parse_dtype(type(element))
    try:
        return conversion_map[from_type](element)
    except (KeyError, ValueError) as err:
        err_msg = (f"[{error_trace()}] could not convert element to complex: "
                   f"{repr(element)}")
        raise ValueError(err_msg)

    # if from_type == object:
    #     try:
    #         try:
    #             return complex(element)
    #         except TypeError as err:
    #             context = f"[{error_trace()}] object has no __complex__ method"
    #             raise ValueError(context) from err
    #     except ValueError as err:
    #         raise ValueError(err_msg) from err


@cache
def to_boolean(
    element: Any,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = bool
) -> pd.NA | bool:
    if pd.isnull(element):
        return pd.NA
    conversion_map = {
        int:
            partial(_integer_to_boolean, force=force, ftol=ftol,
                    return_type=return_type),
        float:
            partial(_float_to_boolean, force=force, ftol=ftol,
                    return_type=return_type),
        complex:
            partial(_complex_to_boolean, force=force, ftol=ftol,
                    return_type=return_type),
        str:
            partial(_string_to_boolean, force=force, ftol=ftol,
                    return_type=return_type),
        bool:
            lambda x: return_type(x),
        datetime:
            partial(_datetime_to_boolean, force=force, ftol=ftol,
                    return_type=return_type),
        timedelta:
            partial(_timedelta_to_boolean, force=force, ftol=ftol,
                    return_type=return_type)
    }
    from_type = parse_dtype(type(element))
    try:
        return conversion_map[from_type](element)
    except (KeyError, ValueError) as err:
        err_msg = (f"[{error_trace()}] could not convert element to bool: "
                   f"{repr(element)}")
        raise ValueError(err_msg) from err

    # if from_type == object:
    #     try:
    #         try:
    #             return bool(element)
    #         except TypeError as err:
    #             context = f"[{error_trace()}] object has no __bool__ method"
    #             raise ValueError(context) from err
    #     except ValueError as err:
    #         raise ValueError(err_msg) from err


@cache
def to_string(
    element: Any,
    return_type: type = str
) -> pd.NA | str:
    if pd.isnull(element):
        return pd.NA
    conversion_map = {
        int:
            partial(_integer_to_string, return_type=return_type),
        float:
            partial(_float_to_string, return_type=return_type),
        complex:
            partial(_complex_to_string, return_type=return_type),
        str:
            lambda x: return_type(x),
        bool:
            partial(_boolean_to_string, return_type=return_type),
        datetime:
            partial(_datetime_to_string, return_type=return_type),
        timedelta:
            partial(_timedelta_to_string, return_type=return_type),
    }
    from_type = parse_dtype(type(element))
    try:
        return conversion_map[from_type](element)
    except (KeyError, ValueError) as err:
        err_msg = (f"[{error_trace()}] could not convert element to string: "
                   f"{repr(element)}")
        raise ValueError(err_msg) from err

    # if from_type == object:
    #     try:
    #         try:
    #             return str(element)
    #         except TypeError as err:
    #             context = f"[{error_trace()}] object has no __str__ method"
    #             raise ValueError(context) from err
    #     except ValueError as err:
    #         raise ValueError(err_msg) from err



@cache
def to_datetime(
    element: Any,
    *args,
    force: bool = False,
    ftol: float = 1e-6,
    format: str | None = None,
    func: Callable | None = None,
    return_type: type = pd.Timestamp,
    **kwargs
) -> pd.NaT | pd.Timestamp | datetime:
    if pd.isnull(element):
        return pd.NaT
    conversion_map = {
        int:
            partial(_integer_to_datetime, return_type=return_type),
        float:
            partial(_float_to_datetime, return_type=return_type),
        complex:
            partial(_complex_to_datetime, force=force, ftol=ftol,
                    return_type=return_type),
        str:
            partial(_string_to_datetime, force=force, ftol=ftol,
                    return_type=return_type),
        bool:
            partial(_boolean_to_datetime, return_type=return_type),
        datetime:
            lambda x: return_type.fromtimestamp(to_utc(x).timestamp(),
                                                timezone.utc),
        timedelta:
            partial(_timedelta_to_datetime, return_type=return_type)
    }
    from_type = parse_dtype(type(element))
    try:
        return conversion_map[from_type](element)
    except (KeyError, ValueError) as err:
        err_msg = (f"[{error_trace()}] could not convert element to datetime: "
                   f"{repr(element)}")
        raise ValueError(err_msg) from err

    # if from_type == object:
    #     try:
    #         try:
    #             timestamp = element.to_datetime()
    #             return pd.Timestamp(timestamp.timestamp(), "UTC")
    #         except TypeError as err:
    #             context = f"[{error_trace()}] object has no to_datetime method"
    #             raise ValueError(context) from err
    #     except ValueError as err:
    #         raise ValueError(err_msg) from err


@cache
def to_timedelta(
    element: Any,
    force: bool = False,
    ftol: float = 1e-6,
    return_type: type = pd.Timedelta
) -> pd.NaT | pd.Timedelta:
    if pd.isnull(element):
        return pd.NaT
    conversion_map = {
        int:
            partial(_integer_to_timedelta, return_type=return_type),
        float:
            partial(_float_to_timedelta, return_type=return_type),
        complex:
            partial(_complex_to_timedelta, force=force, ftol=ftol,
                    return_type=return_type),
        str:
            partial(_string_to_timedelta, force=force, ftol=ftol,
                    return_type=return_type),
        bool:
            partial(_boolean_to_timedelta, return_type=return_type),
        datetime:
            partial(_datetime_to_timedelta, return_type=return_type),
        timedelta:
            lambda x: return_type(seconds=x.total_seconds())
    }
    from_type = parse_dtype(type(element))
    try:
        return conversion_map[from_type](element)
    except (KeyError, ValueError) as err:
        err_msg = (f"[{error_trace()}] could not convert element to timedelta: "
                   f"{repr(element)}")
        raise ValueError(err_msg) from err

    # if from_type == object:
    #     try:
    #         try:
    #             seconds = element.to_timedelta().total_seconds()
    #             return pd.Timestamp.fromtimestamp(seconds, tz="UTC")
    #         except TypeError as err:
    #             context = f"[{error_trace()}] object has no to_timedelta method"
    #             raise ValueError(context) from err
    #     except ValueError as err:
    #         raise ValueError(err_msg) from err

