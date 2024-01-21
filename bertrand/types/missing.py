"""This module describes a ``NullType`` object, which can be used to represent
missing values.
"""
import numpy as np
import pandas as pd

from .base cimport ScalarType
from .base import register


@register
class NullType(ScalarType):
    """Missing value type."""

    name = "null"
    aliases = {
        "null", "none", "None", "NaN", "nan", "NA", "na", "missing",
        type(None), type(pd.NA)
    }
    type_def = type(pd.NA)
    na_value = pd.NA
