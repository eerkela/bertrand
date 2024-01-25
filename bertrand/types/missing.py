"""This module describes a ``NullType`` object, which can be used to represent
missing values.
"""
import pandas as pd

from .base import Type


class Missing(Type, backend="missing"):
    """Missing value type."""

    aliases = {
        "null", "none", "None", "NaN", "nan", "NA", "na", "missing",
        type(None), type(pd.NA)
    }
    scalar = type(pd.NA)
    missing = pd.NA
