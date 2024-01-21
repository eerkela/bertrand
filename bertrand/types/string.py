"""This module contains all the prepackaged string types for the ``pdcast``
type system.
"""
from sys import getsizeof

import numpy as np
import pandas as pd

from .base import DTYPE, Type, TypeMeta


class String(Type):
    """Abstract string type."""

    aliases = {
        "string", "str", "unicode", "str0", "str_", "unicode_", "U", pd.StringDtype
    }

    @classmethod
    def from_dtype(cls, dtype: DTYPE) -> TypeMeta:
        """TODO"""
        if isinstance(dtype, pd.StringDtype):
            if dtype.storage == "pyarrow":
                return PyArrowString

            return PythonString

        return cls


@String.default
class PythonString(String, backend="python"):
    """Python string type."""

    aliases = {str, np.str_, np.dtype("U")}
    scalar = str
    dtype = pd.StringDtype("python")
    itemsize = getsizeof("")
    missing = pd.StringDtype("python").na_value


class PyArrowString(String, backend="pyarrow"):
    """PyArrow string type."""

    aliases = set()
    dtype = pd.StringDtype("pyarrow")
    itemsize = getsizeof("")
    missing = pd.StringDtype("pyarrow").na_value
