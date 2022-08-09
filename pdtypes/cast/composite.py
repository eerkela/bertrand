from __future__ import annotations
from typing import Any

import numpy as np
import pandas as pd

import pdtypes.cast.boolean
import pdtypes.cast.complex
import pdtypes.cast.float
import pdtypes.cast.integer

from pdtypes.cast.helpers import SeriesWrapper
from pdtypes.check import get_dtype, supertype
from pdtypes.util.type_hints import array_like


def dispatch(arg: Any | array_like) -> SeriesWrapper:
    """test"""
    element_type = get_dtype(arg)
    if isinstance(element_type, tuple):
        # composite series
        return NotImplementedError()

    wrappers = {
        bool: pdtypes.cast.boolean.BooleanSeries,
        int: pdtypes.cast.integer.IntegerSeries,
        float: pdtypes.cast.float.FloatSeries,
        complex: pdtypes.cast.complex.ComplexSeries
    }
    wrap_class = wrappers[supertype(element_type)]
    return wrap_class(arg)


# class CompositeSeries(SeriesWrapper):


