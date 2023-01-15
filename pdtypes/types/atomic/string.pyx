import decimal
from types import MappingProxyType
from typing import Union, Sequence

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType
from .base import generic

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve


# TODO: add fixed-length numpy string backend?
# -> These would be a numpy-only feature if implemented


#########################
####    CONSTANTS    ####
#########################


cdef object default_string_dtype
cdef bint pyarrow_installed


# if pyarrow >= 1.0.0 is installed, use as default string storage backend
try:
    default_string_dtype = pd.StringDtype("pyarrow")
    pyarrow_installed = True
except ImportError:
    default_string_dtype = pd.StringDtype("python")
    pyarrow_installed = False


##############################
####    GENERIC STRING    ####
##############################


@generic
class StringType(AtomicType):
    """String supertype."""

    name = "string"
    aliases = {
        str,
        np.str_,
        # np.dtype("U") handled in resolve_typespec_dtype() special case
        "string",
        "str",
        "unicode",
        "str0",
        "str_",
        "unicode_",
        "U",
    }

    def __init__(self):
        super().__init__(
            type_def=str,
            dtype=default_string_dtype,
            na_value=pd.NA,
            itemsize=None,
        )


############################
####   PYTHON STRING    ####
############################


@StringType.register_backend("python")
class PythonStringType(AtomicType):

    aliases = {pd.StringDtype("python"), "pystr"}

    def __init__(self):
        super().__init__(
            type_def=str,
            dtype=pd.StringDtype("python"),
            na_value=pd.NA,
            itemsize=None
        )


##############################
####    PYARROW STRING    ####
##############################


@StringType.register_backend("pyarrow")
class PyArrowStringType(AtomicType, ignore=not pyarrow_installed):

    aliases = {"arrowstr"}

    def __init__(self):
        super().__init__(
            type_def=str,
            dtype=pd.StringDtype("pyarrow"),
            na_value=pd.NA,
            itemsize=None
        )


# add pyarrow string extension type to PyArrowStringType aliases
if pyarrow_installed:
    PyArrowStringType.register_alias(pd.StringDtype("pyarrow"))
