from __future__ import annotations
import decimal as pydecimal
import inspect
from typing import Any, Callable

import numpy as np
import pandas as pd

from pdtypes.check.check import is_dtype, supertype, extension_type
from pdtypes.cython.loops import object_types
from pdtypes.error import ConversionError
from pdtypes.util.array import vectorize
from pdtypes.util.type_hints import array_like, scalar

from pdtypes.cast.boolean_ import BooleanSeries
from pdtypes.cast.integer_ import IntegerSeries
from pdtypes.cast.float_ import FloatSeries
from pdtypes.cast.complex_ import ComplexSeries
from pdtypes.cast.decimal_ import DecimalSeries
from pdtypes.cast.string_ import StringSeries


# TODO: https://www.fast.ai/2019/08/06/delegation/

# TODO: https://python.plainenglish.io/easy-python-jump-tables-35e61ac48cab

# TODO: pass errors="ignore" ConversionError up to convert_dtypes.  Eliminates
# need for old_nans attribute + reconstruct() method, and increases performance


wrappers = {
    bool: BooleanSeries,
    int: IntegerSeries,
    float: FloatSeries,
    complex: ComplexSeries,
    pydecimal.Decimal: DecimalSeries,
    str: StringSeries
}


def filter_kwargs(func: Callable, **kwargs) -> dict[str, Any]:
    """Filter out any arguments in **kwargs that don't match the signature of
    `func`.
    """
    # TODO: there should be a better solution than direct filter, but for now,
    # this works fine.

    # TODO: as long as convert_dtypes() has a fixed signature, then
    # kwarg-filtering can be ok
    sig = inspect.signature(func)
    return {k: v for k, v in kwargs.items() if k in sig.parameters}



# TODO: making this an actual, dynamic adapter class is probably the way to
# go.  Adapt the slicing infrastructure to allow easy subsetting.  If given
# a mask with len > series, subset the mask according to self.is_na


class ConversionSeries:
    """test"""

    def __init__(
        self,
        series: scalar | array_like
    ) -> ConversionSeries:
        # vectorize input
        series = pd.Series(vectorize(series), copy=False)

        # identify missing values
        is_extension = pd.api.types.is_extension_array_dtype(series)
        not_nullable = is_dtype(series.dtype, (bool, int))
        if not_nullable and not is_extension:  # series cannot contain NA
            self.is_na = pd.Series(np.broadcast_to(False, series.shape))
            self.is_na.index = series.index
            self.hasnans = False
        else:
            self.is_na = pd.isna(series)
            self.hasnans = self.is_na.any()

        # strip missing values
        if self.hasnans:
            # store old NAs in case `errors='ignore'`
            self.old_nans = series[self.is_na]
            series = series[~self.is_na]

        # generate wrapped series/groupby
        if pd.api.types.is_object_dtype(series):  # might be composite
            obj_types = object_types(series.to_numpy(), supertypes=True)
            self.groups = series.groupby(obj_types, sort=False)
        else:
            self.series = wrappers[supertype(series.dtype)](series)

    def compute(self, target: str, **kwargs) -> pd.Series:
        """Compute partial result for target conversion, excluding missing
        values.  Automatically handles grouped operations in the case of
        object series.
        """
        # series is grouped, wrap each group and transform
        if hasattr(self, "groups"):
            def groupwise(grp) -> pd.Series:
                wrapped = wrappers[grp.name](grp, validate=False)
                conversion = getattr(wrapped, target)
                return conversion(**filter_kwargs(conversion, **kwargs))

            return self.groups.transform(groupwise)

        # series is already wrapped
        conversion = getattr(self.series, target)
        return conversion(**filter_kwargs(conversion, **kwargs))

    def reconstruct(self) -> pd.Series:
        """Reconstruct the original series that was passed to this object's
        constructor.  Ordinarily, this is only necessary when a conversion is
        invoked with `errors='ignore'` and a `ConversionError` is encountered.

        The output will be automatically converted to a `pandas.Series` object,
        but is otherwise unmodified.  If the original series was a list-like
        (without an explicit `.dtype` attribute), it will be returned with
        `dtype='O'`.
        """
        # unwrap non-missing part of original series
        if hasattr(self, "groups"):
            series = self.groups.transform(lambda x: x)  # identity function
        else:
            series = self.series.series

        # replace missing values
        if hasattr(self, "old_nans"):
            result = np.empty(self.is_na.shape, dtype="O")
            result = pd.Series(result, dtype=series.dtype, copy=False)
            result.index = self.is_na.index
            result[~self.is_na] = series
            result[self.is_na] = self.old_nans
            return result

        return series

    def to_boolean(self, errors: str = "raise", **kwargs) -> pd.Series:
        """test"""
        try:
            series = self.compute(target="to_boolean", errors=errors, **kwargs)
        except ConversionError as err:
            if errors == "ignore":
                return self.reconstruct()  # reconstruct original series
            raise err

        # replace missing values
        if self.hasnans:
            dtype = extension_type(series.dtype)
            result = np.empty(self.is_na.shape, dtype="O")
            result = pd.Series(result, dtype=dtype, copy=False)
            result.index = self.is_na.index
            result[~self.is_na] = series
            return result

        return series

    def to_integer(self, errors: str = "raise", **kwargs) -> pd.Series:
        """test"""
        try:
            series = self.compute(target="to_integer", errors=errors, **kwargs)
        except ConversionError as err:
            if errors == "ignore":
                return self.reconstruct()
            raise err

        # replace missing values
        if self.hasnans:
            dtype = extension_type(series.dtype)
            result = np.full(self.is_na.shape, pd.NA, dtype="O")
            result = pd.Series(result, dtype=dtype, copy=False)
            result.index = self.is_na.index
            result[~self.is_na] = series
            return result

        # no missing values to replace
        return series

    def to_float(self, errors: str = "raise", **kwargs) -> pd.Series:
        """test"""
        try:
            series = self.compute(target="to_float", errors=errors, **kwargs)
        except ConversionError as err:
            if errors == "ignore":
                return self.reconstruct()
            raise err

        # replace missing values
        if self.hasnans:
            result = np.full(self.is_na.shape, np.nan, dtype=series.dtype)
            result = pd.Series(result, copy=False)
            result.index = self.is_na.index
            result[~self.is_na] = series
            return result

        return series

    def to_complex(self, errors: str = "raise", **kwargs) -> pd.Series:
        """test"""
        try:
            series = self.compute(target="to_complex", errors=errors, **kwargs)
        except ConversionError as err:
            if errors == "ignore":
                return self.reconstruct()
            raise err

        # replace missing values
        if self.hasnans:
            result = np.full(self.is_na.shape, None, dtype=series.dtype)
            result = pd.Series(result, copy=False)
            result.index = self.is_na.index
            result[~self.is_na] = series
            return result

        return series

    def to_decimal(self, errors: str = "raise", **kwargs) -> pd.Series:
        """test"""
        try:
            series = self.compute(target="to_decimal", errors=errors, **kwargs)
        except ConversionError as err:
            if errors == "ignore":
                return self.reconstruct()
            raise err

        # replace missing values
        if self.hasnans:
            result = np.full(self.is_na.shape, pd.NA, dtype="O")
            result = pd.Series(result, copy=False)
            result.index = self.is_na.index
            result[~self.is_na] = series
            return result

        return series

    def to_datetime(self, errors: str = "raise", **kwargs) -> pd.Series:
        """test"""
        try:
            series = self.compute(target="to_datetime", errors=errors, **kwargs)
        except ConversionError as err:
            if errors == "ignore":
                return self.reconstruct()
            raise err

        # replace missing values
        if self.hasnans:
            # TODO: special handling of pd.Timestamp-dtyped result series
            result = np.full(self.is_na.shape, pd.NaT, dtype="O")
            result = pd.Series(result, copy=False)
            result.index = self.is_na.index
            result[~self.is_na] = series
            return result

        return series

    def to_timedelta(self, errors: str = "raise", **kwargs) -> pd.Series:
        """test"""
        try:
            series = self.compute(target="to_timedelta", errors=errors,
                                  **kwargs)
        except ConversionError as err:
            if errors == "ignore":
                return self.reconstruct()
            raise err

        # replace missing values
        if self.hasnans:
            # TODO: special handling of pd.Timestamp-dtyped result series
            result = np.full(self.is_na.shape, pd.NaT, dtype="O")
            result = pd.Series(result, copy=False)
            result.index = self.is_na.index
            result[~self.is_na] = series
            return result

        return series

    def to_string(self, errors: str = "raise", **kwargs) -> pd.Series:
        """test"""
        try:
            series = self.compute(target="to_string", errors=errors, **kwargs)
        except ConversionError as err:
            if errors == "ignore":
                return self.reconstruct()
            raise err

        # TODO: ensure returned series is always pd.StringDtype()

        # replace missing values
        if self.hasnans:
            result = np.full(self.is_na.shape, pd.NA, dtype="O")
            result = pd.Series(result, dtype=series.dtype)
            result.index = self.is_na.index
            result[~self.is_na] = series
            return result

        return series
