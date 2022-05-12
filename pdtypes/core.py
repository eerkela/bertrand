from __future__ import annotations
from datetime import datetime, timedelta
from functools import partial

import numpy as np
import pandas as pd

import pdtypes.cast
from pdtypes.error import error_trace


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


def get_dtypes(data: pd.Series | pd.DataFrame, exact: bool = False) -> type:
    if isinstance(data, pd.Series):
        if exact:
            return data.dtype
        return _infer_series_dtype(data)

    if isinstance(data, pd.DataFrame):
        if exact:
            return {col: data[col].dtype for col in data.columns}
        return {col: _infer_series_dtype(data[col]) for col in data.columns}

    err_msg = (f"[{error_trace()}] `data` must be a pandas.Series or "
               f"pandas.DataFrame instance (received object of type: "
               f"{type(data)})")
    raise TypeError(err_msg)


def check_dtypes(data: pd.Series | pd.DataFrame,
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



def _convert_series_dtype(series: pd.Series,
                          dtype: type,
                          round: bool = False,
                          force: bool = False,
                          unit: str = "s",
                          tol: float = 1e-6) -> pd.Series:
    conversions = {
        "integer": {
            "boolean": partial(pdtypes.cast.integer_to_boolean, dtype=dtype,
                               force=force),
            "integer": lambda s: s.astype(dtype),
            "float": partial(pdtypes.cast.integer_to_float, dtype=dtype),
            "complex": partial(pdtypes.cast.integer_to_complex, dtype=dtype),
            "datetime": partial(pdtypes.cast.integer_to_datetime, unit=unit),
            "timedelta": partial(pdtypes.cast.integer_to_timedelta, unit=unit),
            "object": lambda s: s.astype(dtype),
            "string": partial(pdtypes.cast.integer_to_string, dtype=dtype)
        },
        "float": {
            "boolean": partial(pdtypes.cast.float_to_boolean, dtype)
        }
    }
    from_type = parse_dtype(series.dtype)
    to_type = parse_dtype(dtype)
    return conversions[from_type][to_type](series)


def parse_dtype(dtype: type) -> str:
    if pd.api.types.is_bool_dtype(dtype):
        return "boolean"
    if pd.api.types.is_integer_dtype(dtype):
        return "integer"
    if pd.api.types.is_float_dtype(dtype):
        return "float"
    if pd.api.types.is_complex_dtype(dtype):
        return "complex"
    if (dtype in (datetime, pd.Timestamp) or
        pd.api.types.is_datetime64_any_dtype(dtype)):
        return "datetime"
    if (dtype in (timedelta, pd.Timedelta) or
        pd.api.types.is_timedelta64_dtype(dtype)):
        return "timedelta"
    if pd.api.types.is_object_dtype(dtype):
        return "object"
    if pd.api.types.is_string_dtype(dtype):
        return "string"
    err_msg = (f"[{error_trace()}] could not interpret dtype: {dtype}")
    raise TypeError(err_msg)


def convert_dtypes(data: pd.Series | pd.DataFrame,
                   typespec: type | dict[str | type] | None = None,
                   
                   
                   downcast: bool = True,
                   signed: bool = True,
                   format: str | None = None) -> pd.Series | pd.DataFrame:
    if isinstance(data, pd.Series):
        if not isinstance(typespec, type):
            err_msg = (f"[{error_trace()}] when used on a series, `typespec` "
                       f"must be an atomic data type (received: {typespec})")
            raise TypeError(err_msg)
        from_type = pd.api.types.infer_dtype(data)
