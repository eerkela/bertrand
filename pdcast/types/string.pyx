"""This module contains all the prepackaged string types for the ``pdcast``
type system.
"""
import re  # normal python regex for compatibility with pd.Series.str.extract

import numpy as np
cimport numpy as np
import pandas as pd

from pdcast.util.type_hints import dtype_like

from .base cimport ScalarType, AbstractType, Type
from .base import register


# TODO: check to see if StringType.from_dtype works correctly in all cases


#########################
####    CONSTANTS    ####
#########################


cdef object default_string_dtype
cdef bint pyarrow_installed


# if pyarrow >= 1.0.0 is installed, use it as default string storage backend
try:
    import pyarrow
    default_string_dtype = pd.StringDtype("pyarrow")
    pyarrow_installed = True
except ImportError:
    default_string_dtype = pd.StringDtype("python")
    pyarrow_installed = False


#######################
####    GENERIC    ####
#######################


@register
class StringType(AbstractType):
    """String supertype."""

    name = "string"
    aliases = {
        str,
        np.str_,
        np.dtype("U"),
        pd.StringDtype,
        "string",
        "str",
        "unicode",
        "str0",
        "str_",
        "unicode_",
        "U",
    }

    def from_dtype(self, dtype: dtype_like) -> ScalarType:
        # string extension type special case
        if isinstance(dtype, pd.StringDtype):
            if dtype.storage == "pyarrow":
                if PyArrowStringType not in self.registry:
                    raise ValueError("PyArrow string backend is not registered")

                return PyArrowStringType

            if PythonStringType not in self.registry:
                raise ValueError("Python string backend is not registered")

            return PythonStringType

        return self


#####################
####   PYTHON    ####
#####################


@register
@StringType.default
@StringType.implementation("python")
class PythonStringType(ScalarType):

    aliases = set()
    dtype = pd.StringDtype("python")
    type_def = str


#######################
####    PYARROW    ####
#######################


# NOTE: invoking pd.StringDtype("pyarrow") when pyarrow is not installed causes
# an ImportError.  Since pyarrow support is optional, we have to guard this
# type with an if statement rather than using the cond= argument of @register.


if pyarrow_installed:


    @register
    @StringType.default(warn=False)
    @StringType.implementation("pyarrow")
    class PyArrowStringType(ScalarType):

        aliases = set()
        dtype = pd.StringDtype("pyarrow")
        type_def = str

