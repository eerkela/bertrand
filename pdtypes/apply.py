from __future__ import annotations
from functools import cache
from datetime import datetime, timedelta
from typing import Any, Callable

import numpy as np
import pandas as pd

from pdtypes.error import error_trace
from pdtypes.parse import parse_dtype, parse_string


@cache
def to_integer(element: Any,
               force: bool = False,
               ftol: float = 1e-6) -> np.nan | int:
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
            result = int(np.round(parsed))
            if force or result == parsed:
                return result
            raise ValueError(err_msg)
        if dtype == complex:
            result = int(np.round(parsed.real))
            if force or result == parsed:
                return result
            raise ValueError(err_msg)
        if dtype == str:
            raise ValueError(err_msg)
        if dtype == bool:
            return int(parsed)
        if dtype == datetime:
            timestamp = parsed.timestamp()
            result = int(np.round(timestamp))
            if force or result == timestamp:
                return result
            raise ValueError(err_msg)
        if dtype == timedelta:
            seconds = parsed.total_seconds()
            result = int(np.round(seconds))
            if force or result == seconds:
                return result
            raise ValueError(err_msg)

    if from_type == bool:
        return int(element)

    if from_type == datetime:
        timestamp = element.timestamp()
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
