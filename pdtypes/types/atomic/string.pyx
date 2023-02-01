import decimal
from functools import partial
from types import MappingProxyType
from typing import Iterable, Union

cimport cython
import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType
from .base import dispatch, generic

cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
cimport pdtypes.types.resolve as resolve
import pdtypes.types.resolve as resolve


# TODO: add fixed-length numpy string backend?
# -> These would be a numpy-only feature if implemented


##################################
####    MIXINS & CONSTANTS    ####
##################################


cdef set default_false = {"false", "f", "no", "n", "off", "0"}
cdef set default_true = {"true", "t", "yes", "y", "on", "1"}


cdef object default_string_dtype
cdef bint pyarrow_installed


# if pyarrow >= 1.0.0 is installed, use as default string storage backend
try:
    default_string_dtype = pd.StringDtype("pyarrow")
    pyarrow_installed = True
except ImportError:
    default_string_dtype = pd.StringDtype("python")
    pyarrow_installed = False


class StringMixin:

    #############################
    ####    SERIES METHODS   ####
    #############################

    @dispatch
    def to_boolean(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        true: str | Iterable[str] = default_true,
        false: str | Iterable[str] = default_false,
        errors: str = False,
        ignore_case: bool = True,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert string data to a boolean data type."""
        # convert to set
        true = {true} if isinstance(true, str) else set(true)
        false = {false} if isinstance(false, str) else set(false)

        # ensure true, false are disjoint
        if not true.isdisjoint(false):
            intersection = true.intersection(false)
            err_msg = f"`true` and `false` must be disjoint "
            if len(intersection) == 1:
                err_msg += (
                    f"({repr(intersection.pop())} is present in both sets)"
                )
            else:
                err_msg += f"({intersection} are present in both sets)"
            raise ValueError(err_msg)

        # configure values for lookup function
        cdef dict lookup = dict.fromkeys(true, 1) | dict.fromkeys(false, 0)
        if "*" in true:
            fill = 1
        elif "*" in false:
            fill = 0
        else:
            fill = -1

        # apply lookup function with specified errors
        result = series.apply_with_errors(
            partial(
                boolean_apply,
                lookup=lookup,
                ignore_case=ignore_case,
                fill=fill
            ),
            errors=errors
        )
        result.element_type = bool

        # delegate to AtomicType.to_boolean()
        return super().to_boolean(
            result,
            dtype=dtype,
            errors=errors,
            **unused
        )

    @dispatch
    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        base: int = 0,
        errors: str = "raise",
        **unused
    ) -> cast.SeriesWrapper:
        """Convert string data to an integer data type with the given base."""
        result = series.apply_with_errors(
            partial(int, base=base),
            errors=errors
        )
        result.element_type = int
        return result.to_integer(dtype=dtype, errors=errors, **unused)

    @dispatch
    def to_float(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert string data to a floating point data type."""
        decimal_type = resolve.resolve_type("decimal")
        result = series.to_decimal(dtype=decimal_type, **unused)
        return result.to_float(dtype=dtype, **unused)



#######################
####    GENERIC    ####
#######################


@generic
class StringType(StringMixin, AtomicType):
    """String supertype."""

    conversion_func = cast.to_string  # all subtypes/backends inherit this
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


#####################
####   PYTHON    ####
#####################


@StringType.register_backend("python")
class PythonStringType(StringMixin, AtomicType):

    aliases = {pd.StringDtype("python"), "pystr"}

    def __init__(self):
        super().__init__(
            type_def=str,
            dtype=pd.StringDtype("python"),
            na_value=pd.NA,
            itemsize=None
        )


#######################
####    PYARROW    ####
#######################


@StringType.register_backend("pyarrow")
@StringType.register_backend("arrow")
class PyArrowStringType(StringMixin, AtomicType, ignore=not pyarrow_installed):

    aliases = {"arrowstr"}

    def __init__(self):
        super().__init__(
            type_def=str,
            dtype=pd.StringDtype("pyarrow"),
            na_value=pd.NA,
            itemsize=None
        )


#######################
####    PRIVATE    ####
#######################


cdef char boolean_apply(
    str val,
    dict lookup,
    bint ignore_case,
    char fill
) except -1:
    if ignore_case:
        val = val.lower()
    if fill == -1:
        return lookup[val]
    return lookup.get(val, fill)


# add pyarrow string extension type to PyArrowStringType aliases
if pyarrow_installed:
    # NOTE: if pyarrow isn't installed, StringDtype("pyarrow") throws an error
    PyArrowStringType.register_alias(pd.StringDtype("pyarrow"))
