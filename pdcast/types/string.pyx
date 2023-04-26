"""This module contains all the prepackaged string types for the ``pdcast``
type system.
"""
import re  # normal python regex for compatibility with pd.Series.str.extract
from typing import Callable

import numpy as np
cimport numpy as np
import pandas as pd

cimport pdcast.resolve as resolve
import pdcast.resolve as resolve

from .base cimport AtomicType, BaseType
from .base import generic, register


##################################
####    MIXINS & CONSTANTS    ####
##################################


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


class StringMixin:

    @property
    def conversion_func(self) -> Callable:
        from pdcast import convert

        return convert.to_string


#######################
####    GENERIC    ####
#######################


@register
@generic
class StringType(StringMixin, AtomicType):
    """String supertype."""

    # internal root fields - all subtypes/backends inherit these
    _family = "string"

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
@StringType.register_backend("python")
class PythonStringType(StringMixin, AtomicType):

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
    @StringType.register_backend("pyarrow")
    class PyArrowStringType(StringMixin, AtomicType):

        aliases = set()
        dtype = pd.StringDtype("pyarrow")
        type_def = str


#######################
####    PRIVATE    ####
#######################


cdef object complex_pattern = re.compile(
    r"\(?(?P<real>[+-]?[0-9.]+)(?P<imag>[+-][0-9.]+)?j?\)?"
)


cdef char boolean_apply(
    str val,
    dict lookup,
    bint ignore_case,
    char fill
) except -1:
    """Convert a scalar string into a boolean value using the given lookup
    dictionary.

    Notes
    -----
    The ``fill`` argument is implemented as an 8-bit integer as an optimization,
    which allows us to lower this function into pure Cython.  The values this
    variable can take are as follows:

        *   ``1``, which signals that ``KeyErrors`` should be taken as ``True``
        *   ``0``, which signals that ``KeyErrors`` should be taken as ``False``
        *   ``-1``, which signals that ``KeyErrors`` should be raised up the
            stack.
    """
    if ignore_case:
        val = val.lower()
    if fill == -1:
        try:
            return lookup[val]
        except:
            raise ValueError(f"encountered non-boolean value: '{val}'")
    return lookup.get(val, fill)
