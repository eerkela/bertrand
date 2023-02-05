import decimal
from functools import partial
import re  # normal python regex for compatibility with pd.Series.str.extract
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

from pdtypes.util.round cimport Tolerance


# TODO: add fixed-length numpy string backend?
# -> These would be a numpy-only feature if implemented


##################################
####    MIXINS & CONSTANTS    ####
##################################


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
        true: set,
        false: set,
        errors: str,
        ignore_case: bool,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert string data to a boolean data type."""
        # configure lookup dict
        cdef dict lookup = dict.fromkeys(true, 1) | dict.fromkeys(false, 0)
        if "*" in true:
            fill = 1  # KeyErrors become truthy
        elif "*" in false:
            fill = 0  # KeyErrors become falsy
        else:
            fill = -1  # raise

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
        return super().to_boolean(series, dtype, errors=errors)

    @dispatch
    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        base: int,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert string data to an integer data type with the given base."""
        result = series.apply_with_errors(
            partial(int, base=base),
            errors=errors
        )
        result.element_type = int
        return result.to_integer(
            dtype=dtype,
            base=base,
            errors=errors,
            **unused
        )

    @dispatch
    def to_float(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: bool,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert string data to a floating point data type."""
        decimal_type = resolve.resolve_type("decimal")
        result = self.to_decimal(series, decimal_type, errors=errors)
        return result.to_float(
            dtype=dtype,
            tol=tol,
            downcast=downcast,
            errors=errors,
            **unused
        )

    @dispatch
    def to_complex(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: bool,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert string data to a complex data type."""
        # NOTE: this is technically a 3-step conversion: (1) str -> str,
        # (2) str -> float, (3) float -> complex.  This allows for full
        # precision loss/overflow/downcast checks for both real + imag.

        # (1) separate real, imaginary components via regex
        components = series.str.extract(complex_pattern)
        real = cast.SeriesWrapper(
            components["real"],
            hasnans=series.hasnans,
            element_type=self
        )
        imag = cast.SeriesWrapper(
            components["imag"],
            hasnans=series.hasnans,
            element_type=self
        )

        # (2) convert real, imag to float, applying checks independently
        real = self.to_float(
            real,
            dtype.equiv_float,
            tol=Tolerance(tol.real),
            downcast=False,
            errors="raise"
        )
        imag = self.to_float(
            imag,
            dtype.equiv_float,
            tol=Tolerance(tol.imag),
            downcast=False,
            errors="raise"
        )

        # (3) combine floats into complex result
        result = real + imag * 1j
        result.element_type = dtype
        return super().to_complex(
            result,
            dtype,
            tol=tol,
            downcast=downcast,
            errors=errors
        )


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


cdef object complex_pattern = re.compile(
    r"\(?(?P<real>[+-]?[0-9.]+)(?P<imag>[+-][0-9.]+)?j?\)?"
)


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
