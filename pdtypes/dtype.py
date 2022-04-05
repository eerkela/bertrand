from __future__ import annotations
from datetime import datetime, timedelta, timezone
from functools import cache
from typing import Any, Callable

import numpy as np
import pandas as pd

from datatube.error import error_trace


CONVERSIONS = {
    int: {
        float:
            lambda x: np.nan if pd.isnull(x) else float(x),
        complex:
            lambda x: np.nan if pd.isnull(x) else complex(x, 0),
        str:
            lambda x: pd.NA if pd.isnull(x) else str(int(x)),
        bool:
            lambda x: pd.NA if pd.isnull(x) else bool(x) if bool(x) == x
                      else ValueError,
        datetime:
            lambda x: pd.NaT if pd.isnull(x)
                      else pd.Timestamp.fromtimestamp(x, tz=timezone.utc),
        timedelta:
            lambda x: pd.NaT if pd.isnull(x) else pd.Timedelta(seconds=x)
    },
    float: {
        int:
            lambda x: np.nan if pd.isnull(x) else int(x) if int(x) == x
                      else ValueError,
        complex:
            lambda x: np.nan if pd.isnull(x) else complex(x, 0),
        str:
            lambda x: pd.NA if pd.isnull(x) else str(x),
        bool:
            lambda x: pd.NA if pd.isnull(x) else bool(x) if bool(x) == x
                      else ValueError,
        datetime:
            lambda x: pd.NaT if pd.isnull(x)
                      else pd.Timestamp.fromtimestamp(x, tz=timezone.utc),
        timedelta:
            lambda x: pd.NaT if pd.isnull(x) else pd.Timedelta(seconds=x)
    },
    complex: {
        int:
            lambda x: np.nan if pd.isnull(x)
                      else int(x.real) if int(x.real) == x
                      else ValueError,
        float:
            lambda x: np.nan if pd.isnull(x) else x.real if x.real == x
                      else ValueError,
        str:
            lambda x: pd.NA if pd.isnull(x) else str(x),
        bool:
            lambda x: pd.NA if pd.isnull(x)
                      else bool(x.real) if bool(x.real) == x
                      else ValueError,
        datetime:
            lambda x: pd.NaT if pd.isnull(x)
                      else pd.Timestamp.fromtimestamp(x.real, tz=timezone.utc)
                      if x.imag == 0
                      else ValueError,
        timedelta:
            lambda x: pd.NaT if pd.isnull(x)
                      else pd.Timedelta(seconds=x.real) if x.imag == 0
                      else ValueError
    },
    str: {
        int:
            lambda x: np.nan if pd.isnull(x) else int(x.strip()),
        float:
            lambda x: np.nan if pd.isnull(x) else float(x.strip()),
        complex:
            lambda x: np.nan if pd.isnull(x) else complex(x.strip()),
        bool:
            lambda x: pd.NA if pd.isnull(x)
                      else True if x.strip().lower() in ("true", "t")
                      else False if x.strip().lower() in ("false", "f")
                      else ValueError,
        timedelta:
            lambda x: pd.NaT if pd.isnull(x)
                      else pd.Timedelta(seconds=float(x.strip()))
    },
    bool: {
        int:
            lambda x: np.nan if pd.isnull(x) else int(x),
        float:
            lambda x: np.nan if pd.isnull(x) else float(x),
        complex:
            lambda x: np.nan if pd.isnull(x) else complex(x, 0),
        str:
            lambda x: np.nan if pd.isnull(x) else str(x),
        datetime:
            lambda x: pd.NaT if pd.isnull(x)
                      else pd.Timestamp.fromtimestamp(x, tz=timezone.utc),
        timedelta:
            lambda x: pd.NaT if pd.isnull(x) else pd.Timedelta(seconds=x)
    },
    datetime: {
        int:
            lambda x: np.nan if pd.isnull(x) else int(x.timestamp())
                      if int(x.timestamp()) == x.timestamp()
                      else ValueError,
        float:
            lambda x: np.nan if pd.isnull(x) else x.timestamp(),
        complex:
            lambda x: np.nan if pd.isnull(x) else complex(x.timestamp(), 0),
        str:
            lambda x: pd.NA if pd.isnull(x) else x.isoformat(),
        bool:
            lambda x: pd.NA if pd.isnull(x) else bool(x.timestamp())
                      if bool(x.timestamp()) == x.timestamp()
                      else ValueError,
        timedelta:
            lambda x: pd.NaT if pd.isnull(x)
                      else pd.Timedelta(seconds=x.timestamp())
    },
    timedelta: {
        int:
            lambda x: np.nan if pd.isnull(x) else int(x.total_seconds())
                      if int(x.total_seconds()) == x.total_seconds()
                      else ValueError,
        float:
            lambda x: np.nan if pd.isnull(x) else x.total_seconds(),
        complex:
            lambda x: np.nan if pd.isnull(x) else complex(x.total_seconds(), 0),
        str:
            lambda x: pd.NA if pd.isnull(x) else str(x),
        bool:
            lambda x: pd.NA if pd.isnull(x) else bool(x.total_seconds())
                      if bool(x.total_seconds()) == x.total_seconds()
                      else ValueError,
        datetime:
            lambda x: pd.NaT if pd.isnull(x)
                      else pd.Timestamp.fromtimestamp(x.total_seconds(),
                                                      tz=timezone.utc)
    },
    object: {
        int:
            lambda x: np.nan if pd.isnull(x) else int(x),
        float:
            lambda x: np.nan if pd.isnull(x) else float(x),
        complex:
            lambda x: np.nan if pd.isnull(x) else complex(x),
        str:
            lambda x: pd.NA if pd.isnull(x) else str(x),
        bool:
            lambda x: pd.NA if pd.isnull(x) else bool(x),
        datetime:  # TODO: remap the function used for this
            lambda x: pd.NaT if pd.isnull(x) else x.to_datetime(),
        timedelta:  # TODO: remap the function used for this
            lambda x: pd.NaT if pd.isnull(x) else x.to_timedelta()
    }
}
LOCAL_TIMEZONE = datetime.now(timezone.utc).astimezone().tzinfo


def _infer_series_dtype(series: pd.Series) -> type:
    """Detect which dtype best fits the observed series data."""
    # complex case - can't use series.convert_dtypes()
    if pd.api.types.is_complex_dtype(series):
        def to_integer(x):
            if pd.isnull(x):
                return np.nan
            if x.imag == 0 and int(x.real) == x.real:
                return int(x.real)
            raise ValueError()

        def to_float(x):
            if pd.isnull(x):
                return np.nan
            if x.imag == 0:
                return x.real
            raise ValueError()

        try:
            series.apply(to_integer)
            return int
        except ValueError:
            pass
        try:
            series.apply(to_float)
            return float
        except ValueError:
            pass
        return complex

    series = series.convert_dtypes()
    if pd.api.types.is_integer_dtype(series):
        return int
    if pd.api.types.is_float_dtype(series):
        return float
    if pd.api.types.is_bool_dtype(series):
        return bool
    if pd.api.types.is_object_dtype(series):
        if not len(series.dropna()):
            return object
        try:  # differentiate between misformatted datetimes and actual objects
            pd.to_datetime(series, utc=True, infer_datetime_format=True)
            return datetime
        except (TypeError, ValueError):
            return object
    if pd.api.types.is_string_dtype(series):
        if not len(series.dropna()):
            return str
        try:  # differentiate between datetime strings and strings
            pd.to_datetime(series, utc=True, infer_datetime_format=True)
            return datetime
        except (TypeError, ValueError):
            return str
    if pd.api.types.is_datetime64_any_dtype(series):
        return datetime
    if pd.api.types.is_timedelta64_dtype(series):
        return timedelta
    err_msg = (f"[{error_trace()}] unexpected error: could not interpret "
               f"series dtype ({series.dtype})")
    raise TypeError(err_msg)


@cache
def _parse_string(
    string: str
) -> int | float | complex | pd.Timedelta | pd.Timestamp | bool | str:
    """If an arbitrary string can be interpreted as an atomic data type,
    convert it to that data type and return the result.

    Examples
    ----------
    ```
    _parse_string("1") == 1
    _parse_string("1.0") == 1.0
    _parse_string("(1+0j)") == complex(1, 0)
    _parse_string("1970-01-01 00:00:00+00:00") == pd.Timestamp.fromtimestamp(0, "utc")
    _parse_string("1 day, 0:00:10") == pd.Timedelta(days=1, seconds=10)
    _parse_string("True") == True
    _parse_string("a") == "a"
    ```
    """
    try:  # integer string
        return int(string)
    except (TypeError, ValueError):
        pass

    try:  # float string
        return float(string)
    except (TypeError, ValueError):
        pass

    try:  # complex string
        return complex(string)
    except (TypeError, ValueError):
        pass

    try:  # timedelta string
        return pd.to_timedelta(string)
    except ValueError:
        pass

    try:  # datetime string
        return pd.to_datetime(string)
    except ValueError:
        pass

    # boolean string
    lower = string.strip().lower()
    if lower in ("t", "true"):
        return True
    if lower in ("f", "false"):
        return False

    return string


def _to_utc(timestamp: datetime | pd.Timestamp) -> datetime | pd.Timestamp:
    if isinstance(timestamp, pd.Timestamp):
        if timestamp.tzinfo is None:
            timestamp = timestamp.tz_localize(LOCAL_TIMEZONE)
        return timestamp.tz_convert("UTC")
    if timestamp.tzinfo is None:
        timestamp.astimezone(LOCAL_TIMEZONE)
    return timestamp.astimezone(timezone.utc)


@cache
def _to_integer(element: Any, force: bool = False) -> np.nan | int:
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
        parsed = _parse_string(element)
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
def _to_float(element: Any, force: bool = False) -> np.nan | float:
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
        parsed = _parse_string(element)
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
def _to_complex(element: Any, force: bool = False) -> np.nan | complex:
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
        parsed = _parse_string(element)
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
def _to_boolean(element: Any, force: bool = False) -> pd.NA | bool:
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
        parsed = _parse_string(element)
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
def _to_string(element: Any, force: bool = False) -> pd.NA | str:
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
def _to_datetime(element: Any, force: bool = False) -> pd.NaT | pd.Timestamp:
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
        parsed = _parse_string(element)
        dtype = parse_dtype(type(parsed))
        if dtype == int:
            return pd.Timestamp.fromtimestamp(parsed, "UTC")
        if dtype == float:
            return pd.Timestamp.fromtimestamp(parsed, "UTC")
        if dtype == complex:
            if force or parsed.imag == 0:
                return pd.Timestamp.fromtimestamp(parsed.real, "UTC")
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
def _to_timedelta(element: Any, force: bool = False) -> pd.NaT | pd.Timedelta:
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
        parsed = _parse_string(element)
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


def _coerce_series_dtype(series: pd.Series,
                         typespec: type,
                         exact: bool = False,
                         use_extension_dtypes: bool = True) -> pd.Series:
    from_type = parse_dtype(series.dtype)
    to_type = parse_dtype(typespec)
    if to_type == int:
        return series.apply(_to_integer)
    if to_type == float:
        return series.apply(_to_float)
    if to_type == complex:
        return series.apply(_to_complex)
    if to_type == str:
        return series.apply(_to_string)
    if to_type == bool:
        return series.apply(_to_boolean)
    if to_type == datetime:
        return series.apply(_to_datetime)
    if to_type == timedelta:
        return series.apply(_to_timedelta)
    if to_type == object:
        return series.astype(np.dtype("O"))
    raise RuntimeError()
    
    
    
    def do_coercion(element):
        result = CONVERSIONS[from_type][to_type](element)
        if result == ValueError:
            err_msg = (f"[{error_trace(stack_index=5)}] cannot coerce series "
                       f"values to {to_type} without losing information "
                       f"(head: {list(series.head())})")
            raise ValueError(err_msg)
        return result

    if to_type == object:
        return series.astype(np.dtype("O"))
    from_type = get_dtype(series)
    if from_type == to_type:
        return series.copy()
    if from_type == str:
        if to_type == datetime:
            return pd.to_datetime(series, infer_datetime_format=True)
        if to_type == timedelta:
            return pd.to_timedelta(series)
    return series.apply(do_coercion)


def parse_dtype(dtype: type) -> type:
    if pd.api.types.is_integer_dtype(dtype):
        return int
    if pd.api.types.is_float_dtype(dtype):
        return float
    if pd.api.types.is_complex_dtype(dtype):
        return complex
    if pd.api.types.is_bool_dtype(dtype):
        return bool
    if (pd.api.types.is_datetime64_any_dtype(dtype) or
        dtype in (datetime, pd.Timestamp)):
        return datetime
    if (pd.api.types.is_timedelta64_dtype(dtype) or 
        dtype in (timedelta, pd.Timedelta)):
        return timedelta
    if pd.api.types.is_object_dtype(dtype):
        return object
    if pd.api.types.is_string_dtype(dtype):
        return str
    err_msg = (f"[{error_trace()}] unrecognized dtype: {dtype}")
    raise TypeError(err_msg)


def get_dtype(data: pd.Series | pd.DataFrame, exact: bool = False) -> type:
    if isinstance(data, pd.Series):
        if exact:
            return data.dtype
        return _get_series_dtype(data)

    if isinstance(data, pd.DataFrame):
        if exact:
            return {col: data[col].dtype for col in data.columns}
        return {col: _get_series_dtype(data[col]) for col in data.columns}

    err_msg = (f"[{error_trace()}] `data` must be a pandas.Series or "
               f"pandas.DataFrame instance (received object of type: "
               f"{type(data)})")
    raise TypeError(err_msg)


def check_dtype(data: pd.Series | pd.DataFrame,
                typespec: type | dict[str, type],
                exact: bool = False) -> bool:
    if isinstance(data, pd.Series):
        dtype = get_dtype(data, exact=exact)
        if isinstance(typespec, type):
            return dtype == typespec
        if isinstance(typespec, (tuple, list, set)):
            return dtype in typespec
        err_msg = (f"[{error_trace()}] when used on a series, `typespec` must "
                   f"be an atomic data type or sequence of atomic data types "
                   f"(received object of type: {type(typespec)})")
        raise TypeError(err_msg)

    if isinstance(data, pd.DataFrame):
        if isinstance(typespec, dict):
            for col_name, ts in typespec.items():
                dtype = get_dtype(data[col_name], exact=exact)
                if isinstance(ts, (tuple, list, set)):
                    if not dtype in ts:
                        return False
                else:
                    if dtype != ts:
                        return False
            return True
        err_msg = (f"[{error_trace()}] when used on a dataframe, `typespec` "
                   f"must be a map of column names and atomic data types or "
                   f"sequences of atomic data types to check against "
                   f"(received object of type: {type(typespec)})")
        raise TypeError(err_msg)

    err_msg = (f"[{error_trace()}] `data` must be either a pandas.Series or "
               f"pandas.DataFrame instance (received object of type: "
               f"{type(data)})")
    raise TypeError(err_msg)


def coerce_dtypes(
    data: pd.Series | pd.DataFrame,
    typespec: type | dict[str, type],
    downcast: bool = True,
    signed: bool = True,
    datetime_format: str | list[str] | tuple[str] | set[str] | None = None,
    use_extension_dtypes: bool = True
) -> pd.Series | pd.DataFrame:
    if isinstance(data, pd.Series):
        if isinstance(typespec, type):
            try:
                return _coerce_series_dtype(data, typespec)
            except ValueError as exc:
                err_msg = (f"[{error_trace()}] cannot coerce series values to "
                           f"{typespec} without losing information "
                           f"(head: {list(data.head())})")
                raise ValueError(err_msg) from exc
        err_msg = (f"[{error_trace()}] when used on a series, `typespec` must "
                   f"be an atomic data type (received object of type: "
                   f"{type(typespec)})")
        raise TypeError(err_msg)

    if isinstance(data, pd.DataFrame):
        if isinstance(typespec, dict):
            result = {}
            for col_name, ts in typespec.items():
                try:
                    result[col_name] = _coerce_series_dtype(data[col_name], ts)
                except ValueError as exc:
                    err_msg = (f"[{error_trace()}] cannot coerce column "
                               f"{repr(col_name)} to {ts} without losing "
                               f"information (head: "
                               f"{list(data[col_name].head())})")
                    raise ValueError(err_msg) from exc
            return pd.concat(result, axis=1)
        err_msg = (f"[{error_trace()}] when used on a dataframe, "
                   f"`typespec` must be a dictionary of column names and "
                   f"atomic data types (received object of type: "
                   f"{type(typespec)})")
        raise TypeError(err_msg)

    err_msg = (f"[{error_trace()}] `data` must be either a pandas.Series or "
               f"pandas.DataFrame instance (received object of type: "
               f"{type(data)})")
    raise TypeError(err_msg)


def convert_dtypes(data: pd.DataFrame) -> pd.DataFrame:
    return coerce_dtypes(data, **check_dtypes(data))

