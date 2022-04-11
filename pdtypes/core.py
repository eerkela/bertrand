from __future__ import annotations
from datetime import datetime, timedelta

import numpy as np
import pandas as pd

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






def _downcast_series(series: pd.Series) -> pd.Series:
    series_dtype = parse_dtype(series.dtype)

    if series_dtype == int:
        extension_type = pd.api.types.is_extension_array_dtype(series_dtype)
        series_min = series.min()  # faster than series.quantile([0, 1])
        series_max = series.max()
        if series_min >= 0:  # unsigned
            if extension_type:
                integer_types = (pd.UInt8Dtype(), pd.UInt16Dtype(),
                                 pd.UInt32Dtype(), pd.UInt64Dtype())
            else:
                integer_types = (np.dtype(np.uint8), np.dtype(np.uint16),
                                 np.dtype(np.uint32), np.dtype(np.uint64))
            for typespec in integer_types:
                max_int = 2 ** (typespec.itemsize * 8)
                if series_max < max_int:
                    return series.astype(typespec)
        else:  # signed
            if extension_type:
                integer_types = (pd.Int8Dtype(), pd.Int16Dtype(),
                                 pd.Int32Dtype(), pd.Int64Dtype())
            else:
                integer_types = (np.dtype(np.int8), np.dtype(np.int16),
                                 np.dtype(np.int32), np.dtype(np.int64))
            for typespec in integer_types:
                max_int = 2 ** (typespec.itemsize * 8 - 1)
                if series_max < max_int:
                    return series.astype(typespec)
        err_msg = (f"[{error_trace()}] could not downcast series "
                    f"(head: {list(series.head())})")
        raise ValueError(err_msg)

    if series_dtype == float:
        extension_type = pd.api.types.is_extension_array_dtype(series_dtype)
        series_min = series.min()  # faster than series.quantile([0, 1])
        series_max = series.max()
        # TODO: count significant digits for downcast
        #   fp32 carries 24 bits of precision data, so minimum precision
        #   cannot be less than 1/(2^23).
        #       min/max = +/- 2^2^(exponent bitcount - 1 due to bias) * (2 - precision) / 2
        #       precision = 1/2^(mantissa bitcount)
        #       fp16 -> +/-
        #       fp32 -> min/max: +/-2^128 (2^127 from exponent, 2 from mantissa)
        #               precision: 1/(2^23)
        #   https://www.h-schmidt.net/FloatConverter/IEEE754.html
        
    


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

