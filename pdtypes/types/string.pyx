import decimal
from functools import partial
import re  # normal python regex for compatibility with pd.Series.str.extract

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AtomicType, BaseType
from .base import generic, register

cimport pdtypes.cast as cast
import pdtypes.cast as cast
cimport pdtypes.resolve as resolve
import pdtypes.resolve as resolve

from pdtypes.util.round cimport Tolerance
from pdtypes.util.time cimport Epoch
from pdtypes.util.time import timedelta_string_to_ns


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

    conversion_func = cast.to_string

    #############################
    ####    SERIES METHODS   ####
    #############################

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
        series = series.apply_with_errors(
            partial(
                boolean_apply,
                lookup=lookup,
                ignore_case=ignore_case,
                fill=fill
            ),
            errors=errors
        )
        series.element_type = bool
        return super().to_boolean(series, dtype, errors=errors)

    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        base: int,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert string data to an integer data type with the given base."""
        transfer_type = resolve.resolve_type(int)
        series = series.apply_with_errors(
            partial(int, base=base),
            errors=errors
        )
        series.element_type = transfer_type
        return transfer_type.to_integer(
            series,
            dtype=dtype,
            base=base,
            errors=errors,
            **unused
        )

    def to_float(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert string data to a floating point data type."""
        transfer_type = resolve.resolve_type("decimal")
        series = self.to_decimal(series, transfer_type, errors=errors)
        return transfer_type.to_float(
            series,
            dtype=dtype,
            tol=tol,
            errors=errors,
            **unused
        )

    def to_complex(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: bool | BaseType,
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
            dtype=dtype.equiv_float,
            tol=Tolerance(tol.real),
            downcast=None,
            errors="raise"
        )
        imag = self.to_float(
            imag,
            dtype=dtype.equiv_float,
            tol=Tolerance(tol.imag),
            downcast=None,
            errors="raise"
        )

        # (3) combine floats into complex result
        series = real + imag * 1j
        series.element_type = dtype
        return super().to_complex(
            series,
            dtype,
            tol=tol,
            downcast=downcast,
            errors=errors
        )

    def to_datetime(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert string data into a datetime data type."""
        return dtype.from_string(series, dtype=dtype, **unused)

    def to_timedelta(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        unit: str,
        step_size: int,
        since: Epoch,
        as_hours: bool,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert string data into a timedelta representation."""
        # 2-step conversion: str -> int, int -> timedelta
        transfer_type = resolve.resolve_type(int)
        series = series.apply_with_errors(
            partial(timedelta_string_to_ns, as_hours=as_hours, since=since),
            errors=errors
        )
        series.element_type = transfer_type
        return transfer_type.to_timedelta(
            series,
            dtype=dtype,
            unit="ns",
            step_size=1,
            since=since,
            errors=errors,
            **unused
        )


#######################
####    GENERIC    ####
#######################


@register
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
    dtype = default_string_dtype
    type_def = str


#####################
####   PYTHON    ####
#####################


@register
@StringType.register_backend("python")
class PythonStringType(StringMixin, AtomicType):

    aliases = {pd.StringDtype("python"), "pystr"}
    dtype = pd.StringDtype("python")
    type_def = str


#######################
####    PYARROW    ####
#######################


if pyarrow_installed:


    @register
    @StringType.register_backend("pyarrow")
    class PyArrowStringType(StringMixin, AtomicType):

        aliases = {"arrowstr", pd.StringDtype("pyarrow")}
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
    if ignore_case:
        val = val.lower()
    if fill == -1:
        return lookup[val]
    return lookup.get(val, fill)
