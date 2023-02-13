import numpy as np
cimport numpy as np
import pandas as pd

import pytz

from pdtypes.error import shorten_list
from pdtypes.type_hints import numeric
cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast

from pdtypes.util.round cimport Tolerance
from pdtypes.util.time cimport Epoch

from .base cimport AdapterType, AtomicType, CompositeType
from .base import dispatch, generic, subtype
import pdtypes.types.atomic.float as float_types


##################################
####    MIXINS & CONSTANTS    ####
##################################

# NOTE: x86 extended precision float type (long double) is platform-specific
# and may not be exposed depending on hardware configuration.
cdef bint no_clongdouble = (np.dtype(np.clongdouble).itemsize <= 16)


class ComplexMixin:

    ############################
    ####    TYPE METHODS    ####
    ############################

    def downcast(
        self,
        series: cast.SeriesWrapper,
        tol: Tolerance,
        smallest: CompositeType = None
    ) -> cast.SeriesWrapper:
        """Reduce the itemsize of a complex type to fit the observed range."""
        equiv_float = self.equiv_float
        real = equiv_float.downcast(
            series.real,
            smallest=smallest,
            tol=tol
        )
        imag = equiv_float.downcast(
            series.imag,
            smallest=smallest,
            tol=Tolerance(tol.imag)
        )
        return combine_real_imag(real, imag)

    @property
    def equiv_float(self) -> AtomicType:
        candidates = float_types.FloatType.instance().subtypes
        for x in candidates:
            if type(x).__name__ == self._equiv_float:
                return x
        raise TypeError(f"{repr(self)} has no equivalent float type")

    @property
    def smaller(self) -> list:
        result = [
            x for x in self.root.subtypes if (
                x.backend == self.backend and
                x not in ComplexType.backends.values()
            )
        ]
        if not self.is_root:
            result = [
                x for x in result if (
                    (x.itemsize or np.inf) < (self.itemsize or np.inf)
                )
            ]
        result.sort(key=lambda x: x.itemsize)
        return result

    ##############################
    ####    SERIES METHODS    ####
    ##############################

    @dispatch
    def round(
        self,
        series: cast.SeriesWrapper,
        decimals: int = 0,
        rule: str = "half_even"
    ) -> cast.SeriesWrapper:
        """Round a complex series to the given number of decimal places using
        the specified rounding rule.
        """
        equiv_float = self.equiv_float
        real = equiv_float.round(series.real, decimals=decimals, rule=rule)
        imag = equiv_float.round(series.imag, decimals=decimals, rule=rule)
        return combine_real_imag(real, imag)

    @dispatch
    def snap(
        self,
        series: cast.SeriesWrapper,
        tol: numeric = 1e-6
    ) -> cast.SeriesWrapper:
        """Snap each element of the series to the nearest integer if it is
        within the specified tolerance.
        """
        tol = Tolerance(tol)
        if not tol:  # trivial case, tol=0
            return series.copy()

        equiv_float = self.equiv_float
        real = equiv_float.snap(series.real, tol=tol.real)
        imag = equiv_float.snap(series.imag, tol=tol.imag)
        return combine_real_imag(real, imag)

    def to_boolean(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        tol: Tolerance,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert complex data to a boolean data type."""
        # 2-step conversion: complex -> float, float -> bool
        transfer_type = self.equiv_float
        series = self.to_float(
            series,
            dtype=transfer_type,
            tol=tol,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_boolean(
            series,
            dtype=dtype,
            rounding=rounding,
            tol=tol,
            errors=errors,
            **unused
        )

    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        rounding: str,
        tol: Tolerance,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert complex data to an integer data type."""
        # 2-step conversion: complex -> float, float -> int
        transfer_type = self.equiv_float
        series = self.to_float(
            series,
            dtype=transfer_type,
            tol=tol,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_integer(
            series,
            dtype=dtype,
            rounding=rounding,
            tol=tol,
            downcast=downcast,
            errors=errors,
            **unused
        )

    def to_float(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        downcast: CompositeType,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert complex data to a float data type."""
        transfer_type = self.equiv_float
        real = series.real

        # check for nonzero imag
        if errors != "coerce":  # ignore if coercing
            bad = ~series.imag.within_tol(0, tol=tol.imag)
            if bad.any():
                raise ValueError(
                    f"imaginary component exceeds tolerance "
                    f"{float(tol.imag):g} at index "
                    f"{shorten_list(bad[bad].index.values)}"
                )

        return transfer_type.to_float(
            real,
            dtype=dtype,
            tol=tol,
            downcast=downcast,
            errors=errors,
            **unused
        )

    def to_decimal(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert complex data to a decimal data type."""
        # 2-step conversion: complex -> float, float -> decimal
        transfer_type = self.equiv_float
        series = self.to_float(
            series,
            dtype=transfer_type,
            tol=tol,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_decimal(
            series,
            dtype=dtype,
            tol=tol,
            errors=errors,
            **unused
        )

    def to_datetime(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        unit: str,
        step_size: int,
        rounding: str,
        tz: pytz.BaseTzInfo,
        epoch: Epoch,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert complex data to a datetime data type."""
        # 2-step conversion: complex -> float, float -> datetime
        transfer_type = self.equiv_float
        series = self.to_float(
            series,
            dtype=transfer_type,
            tol=tol,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_datetime(
            series,
            dtype=dtype,
            tol=tol,
            unit=unit,
            step_size=step_size,
            rounding=rounding,
            tz=tz,
            epoch=epoch,
            errors=errors,
            **unused
        )

    def to_timedelta(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: Tolerance,
        unit: str,
        step_size: int,
        rounding: str,
        epoch: Epoch,
        errors: str,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert complex data to a timedelta data type."""
        # 2-step conversion: complex -> float, float -> timedelta
        transfer_type = self.equiv_float
        series = self.to_float(
            series,
            dtype=transfer_type,
            tol=tol,
            downcast=None,
            errors=errors
        )
        return transfer_type.to_timedelta(
            series,
            dtype=dtype,
            tol=tol,
            unit=unit,
            step_size=step_size,
            rounding=rounding,
            epoch=epoch,
            errors=errors,
            **unused
        )


#######################
####    GENERIC    ####
#######################


@generic
class ComplexType(ComplexMixin, AtomicType):

    conversion_func = cast.to_complex  # all subtypes/backends inherit this
    name = "complex"
    aliases = {
        complex, "complex", "cfloat", "complex float", "complex floating", "c"
    }
    _equiv_float = "FloatType"

    def __init__(self):
        type_def = complex
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.complex128),
            na_value=type_def("nan+nanj"),
            itemsize=16
        )

@generic
@subtype(ComplexType)
class Complex64Type(ComplexMixin, AtomicType):

    name = "complex64"
    aliases = {
        "complex64", "csingle", "complex single", "singlecomplex", "c8", "F"
    }
    _equiv_float = "Float32Type"

    def __init__(self):
        type_def = np.complex64
        self.min = type_def(-2**24)
        self.max = type_def(2**24)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.complex64),
            na_value=type_def("nan+nanj"),
            itemsize=8
        )


@generic
@subtype(ComplexType)
class Complex128Type(ComplexMixin, AtomicType):

    name = "complex128"
    aliases = {
        "complex128", "cdouble", "complex double", "complex_", "c16", "D"
    }
    _equiv_float = "Float64Type"

    def __init__(self):
        type_def = complex
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.complex128),
            na_value=type_def("nan+nanj"),
            itemsize=16
        )


@generic
@subtype(ComplexType)
class Complex160Type(ComplexMixin, AtomicType, ignore=no_clongdouble):

    name = "complex160"
    aliases = {
        "complex160", "clongdouble", "clongfloat", "complex longdouble",
        "complex longfloat", "complex long double", "complex long float",
        "longcomplex", "long complex", "c20", "G"
    }
    _equiv_float = "Float80Type"

    def __init__(self):
        type_def = np.clongdouble
        self.min = type_def(-2**64)
        self.max = type_def(2**64)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.clongdouble),
            na_value=type_def("nan+nanj"),
            itemsize=np.dtype(np.clongdouble).itemsize
        )


#####################
####    NUMPY    ####
#####################


@ComplexType.register_backend("numpy")
class NumpyComplexType(ComplexMixin, AtomicType):

    aliases = {np.complexfloating}
    _equiv_float = "NumpyFloatType"

    def __init__(self):
        type_def = np.complex128
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.complex128),
            na_value=type_def("nan+nanj"),
            itemsize=16
        )


@subtype(NumpyComplexType)
@Complex64Type.register_backend("numpy")
class NumpyComplex64Type(ComplexMixin, AtomicType):

    aliases = {np.complex64, np.dtype(np.complex64)}
    _equiv_float = "NumpyFloat32Type"

    def __init__(self):
        type_def = np.complex64
        self.min = type_def(-2**24)
        self.max = type_def(2**24)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.complex64),
            na_value=type_def("nan+nanj"),
            itemsize=8
        )


@subtype(NumpyComplexType)
@Complex128Type.register_backend("numpy")
class NumpyComplex128Type(ComplexMixin, AtomicType):

    aliases = {np.complex128, np.dtype(np.complex128)}
    _equiv_float = "NumpyFloat64Type"

    def __init__(self):
        type_def = np.complex128
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.complex128),
            na_value=type_def("nan+nanj"),
            itemsize=16
        )


@subtype(NumpyComplexType)
@Complex160Type.register_backend("numpy")
class NumpyComplex160Type(ComplexMixin, AtomicType, ignore=no_clongdouble):

    aliases = {np.clongdouble, np.dtype(np.clongdouble)}
    _equiv_float = "NumpyFloat80Type"

    def __init__(self):
        type_def = np.clongdouble
        self.min = type_def(-2**64)
        self.max = type_def(2**64)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.clongdouble),
            na_value=type_def("nan+nanj"),
            itemsize=np.dtype(np.clongdouble).itemsize
        )


######################
####    PYTHON    ####
######################


@ComplexType.register_backend("python")
@Complex128Type.register_backend("python")
class PythonComplexType(ComplexMixin, AtomicType):

    aliases = set()
    _equiv_float = "PythonFloatType"

    def __init__(self):
        type_def = complex
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype("O"),
            na_value=type_def("nan+nanj"),
            itemsize=16
        )


#######################
####    PRIVATE    ####
#######################


cdef cast.SeriesWrapper combine_real_imag(
    cast.SeriesWrapper real,
    cast.SeriesWrapper imag
):
    """Merge separate real, imaginary components into a complex series."""
    largest = max(
        [real.element_type, imag.element_type],
        key=lambda x: x.itemsize or np.inf
    )
    result = real + imag * 1j
    result.hasnans = real.hasnans or imag.hasnans
    result.element_type = largest.equiv_complex
    return result
