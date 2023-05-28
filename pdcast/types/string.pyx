"""This module contains all the prepackaged string types for the ``pdcast``
type system.
"""
import re  # normal python regex for compatibility with pd.Series.str.extract

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType, Type
from .base import generic, register


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
@generic
class StringType(AtomicType):
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
    dtype = default_string_dtype
    type_def = str

    @classmethod
    def from_dtype(
        cls,
        dtype: np.dtype | pd.api.extensions.ExtensionDtype
    ) -> AtomicType:
        # string extension type special case
        if isinstance(dtype, pd.StringDtype):
            if dtype.storage == "pyarrow":
                return PyArrowStringType.instance()
            return PythonStringType.instance()

        return cls.instance()


#####################
####   PYTHON    ####
#####################


@register
@StringType.implementation("python")
class PythonStringType(AtomicType):

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
    @StringType.implementation("pyarrow")
    class PyArrowStringType(AtomicType):

        aliases = set()
        dtype = pd.StringDtype("pyarrow")
        type_def = str

