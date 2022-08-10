from __future__ import annotations
from typing import Any

import numpy as np
import pandas as pd

import pdtypes.cast.boolean
import pdtypes.cast.complex
import pdtypes.cast.empty
import pdtypes.cast.float
import pdtypes.cast.integer

from pdtypes.cast.helpers import SeriesWrapper
from pdtypes.check import get_dtype, object_types, supertype
from pdtypes.util.type_hints import array_like


# TODO: consider adding `None` supertype for missing values, and accompanying
# EmptySeries wrapper.
# -> If compositing can be nailed down, then this could potentially replace all
# missing value problems in the individual conversions.  Each series could be
# assumed not to contain them, and if they all return an equivalently-dtyped
# result, then .transform will match them up accordingly.  This would involve
# running an extra .groupby for every incoming series, but it would likely be
# more efficient than scanning through missing values for every input series.

# extension types would be sorted out at the outer wrapper level.
# SeriesWrapper: dispatches to multiple ConversionSeries
#   ConversionSeries (IntegerSeries, ...): One for each supertype

# Every conversion would have to return a like-indexed result for this to
# work properly, but exclude_na would be unnecessary, which could alleviate
# that.
# -> np.frompyfunc preserves index

wrappers = {
    bool: pdtypes.cast.boolean.BooleanSeries,
    int: pdtypes.cast.integer.IntegerSeries,
    float: pdtypes.cast.float.FloatSeries,
    complex: pdtypes.cast.complex.ComplexSeries,
    type(None): pdtypes.cast.empty.EmptySeries
}


def dispatch(arg: Any | array_like) -> SeriesWrapper:
    """test"""
    wrappers = {
        bool: pdtypes.cast.boolean.BooleanSeries,
        int: pdtypes.cast.integer.IntegerSeries,
        float: pdtypes.cast.float.FloatSeries,
        complex: pdtypes.cast.complex.ComplexSeries
    }

    element_type = get_dtype(arg)
    if isinstance(element_type, tuple):
        element_type = {supertype(t) for t in element_type}
        if len(element_type) > 1:
            return CompositeSeries(arg)
        element_type = element_type.pop()

    wrap_class = wrappers[supertype(element_type)]
    return wrap_class(arg)


class CompositeSeries(SeriesWrapper):
    """test"""

    def __init__(
        self,
        series: array_like,
        nans: None | bool | array_like = None
    ) -> CompositeSeries:
        super().__init__(series, nans=nans)
        # subset = self.series[~self.is_na]
        subset = self.series
        obj_types = object_types(subset, supertypes=True)
        self.groups = subset.groupby(obj_types, sort=False)

    def to_boolean(
        self,
        *args,
        **kwargs
    ) -> pd.Series:
        """test"""
        wrap = lambda grp: wrappers[grp.name](grp, nans=False, validate=False)
        compute = lambda wrapped: wrapped.to_boolean(*args, **kwargs)
        return self.groups.transform(lambda x: compute(wrap(x)))

    def to_integer(
        self,
        *args,
        **kwargs
    ) -> pd.Series:
        """test"""
        wrap = lambda grp: wrappers[grp.name](grp, nans=False, validate=False)
        compute = lambda wrapped: wrapped.to_integer(*args, **kwargs)
        return self.groups.transform(lambda x: compute(wrap(x)))

    def to_float(
        self,
        *args,
        **kwargs
    ) -> pd.Series:
        """test"""
        wrap = lambda grp: wrappers[grp.name](grp, nans=False, validate=False)
        compute = lambda wrapped: wrapped.to_float(*args, **kwargs)
        return self.groups.transform(lambda x: compute(wrap(x)))

    def to_complex(
        self,
        *args,
        **kwargs
    ) -> pd.Series:
        """test"""
        wrap = lambda grp: wrappers[grp.name](grp, nans=False, validate=False)
        compute = lambda wrapped: wrapped.to_complex(*args, **kwargs)
        return self.groups.transform(lambda x: compute(wrap(x)))

    def to_decimal(
        self,
        *args,
        **kwargs
    ) -> pd.Series:
        """test"""
        wrap = lambda grp: wrappers[grp.name](grp, nans=False, validate=False)
        compute = lambda wrapped: wrapped.to_decimal(*args, **kwargs)
        return self.groups.transform(lambda x: compute(wrap(x)))

    def to_datetime(
        self,
        *args,
        **kwargs
    ) -> pd.Series:
        """test"""
        wrap = lambda grp: wrappers[grp.name](grp, nans=False, validate=False)
        compute = lambda wrapped: wrapped.to_datetime(*args, **kwargs)
        return self.groups.transform(lambda x: compute(wrap(x)))

    def to_timedelta(
        self,
        *args,
        **kwargs
    ) -> pd.Series:
        """test"""
        wrap = lambda grp: wrappers[grp.name](grp, nans=False, validate=False)
        compute = lambda wrapped: wrapped.to_timedelta(*args, **kwargs)
        return self.groups.transform(lambda x: compute(wrap(x)))

    def to_string(
        self,
        *args,
        **kwargs
    ) -> pd.Series:
        """test"""
        wrap = lambda grp: wrappers[grp.name](grp, nans=False, validate=False)
        compute = lambda wrapped: wrapped.to_string(*args, **kwargs)
        return self.groups.transform(lambda x: compute(wrap(x)))
