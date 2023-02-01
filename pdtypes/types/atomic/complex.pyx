import numpy as np
cimport numpy as np
import pandas as pd

from pdtypes.error import shorten_list
from pdtypes.type_hints import numeric
cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast
from pdtypes.util.round import Tolerance

from .base cimport AdapterType, AtomicType
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

    def downcast(
        self,
        series: cast.SeriesWrapper,
        tol: numeric = 0
    ) -> cast.SeriesWrapper:
        """Reduce the itemsize of a complex type to fit the observed range."""
        tol = Tolerance(tol)
        equiv_float = self.equiv_float
        real = equiv_float.downcast(series.real, tol=tol.real)
        imag = equiv_float.downcast(series.imag, tol=tol.imag)
        return combine_real_imag(real, imag)

    @dispatch
    def round(
        self,
        series: cast.SeriesWrapper,
        rule: str = "half_even",
        decimals: int = 0
    ) -> cast.SeriesWrapper:
        """Round a complex series to the given number of decimal places using
        the specified rounding rule.

        NOTE: this method rounds real and imaginary components separately.
        """
        real = series.real.round(rule=rule, decimals=decimals)
        imag = series.imag.round(rule=rule, decimals=decimals)
        return combine_real_imag(real, imag)

    @dispatch
    def to_boolean(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        **unused
    ) -> cast.SeriesWrapper:
        """Convert complex data to a boolean data type."""
        # 2-step conversion: complex -> float, float -> bool
        result = series.to_float(dtype=self.equiv_float, **unused)
        return result.to_boolean(dtype=dtype, **unused)

    @dispatch
    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: numeric = 1e-6,
        errors: str = "raise",
        **unused
    ) -> cast.SeriesWrapper:
        """Convert complex data to an integer data type."""
        # 2-step conversion: complex -> float, float -> int
        result = series.to_float(dtype=self.equiv_float, tol=tol, errors=errors)
        return result.to_integer(dtype=dtype, **unused)

    @dispatch
    def to_float(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: numeric = 0,
        errors: str = "raise",
        **unused
    ) -> cast.SeriesWrapper:
        """Convert complex data to a float data type."""
        real = series.real

        # check for nonzero imag
        if errors != "coerce":  # ignore if coercing
            tol = Tolerance(tol)
            bad = ~cast.within_tolerance(series.imag, 0, tol=tol.imag)
            if bad.any():
                raise ValueError(
                    f"imaginary component exceeds tolerance "
                    f"{float(tol.imag):g} at index "
                    f"{shorten_list(bad[bad].index.values)}"
                )

        return real.to_float(dtype=dtype, tol=tol, errors=errors, **unused)

    @dispatch
    def to_decimal(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: numeric = 1e-6,
        errors: str = "raise",
        **unused
    ) -> cast.SeriesWrapper:
        """Convert complex data to a decimal data type."""
        # 2-step conversion: complex -> float, float -> decimal
        result = series.to_float(dtype=self.equiv_float, tol=tol, errors=errors)
        return result.to_decimal(dtype=dtype, **unused)

    @dispatch
    def to_datetime(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: numeric = 1e-6,
        errors: str = "raise",
        **unused
    ) -> cast.SeriesWrapper:
        """Convert complex data to a datetime data type."""
        # 2-step conversion: complex -> float, float -> datetime
        result = series.to_float(dtype=self.equiv_float, tol=tol, errors=errors)
        return result.to_datetime(dtype=dtype, **unused)

    @dispatch
    def to_timedelta(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        tol: numeric = 1e-6,
        errors: str = "raise",
        **unused
    ) -> cast.SeriesWrapper:
        """Convert complex data to a timedelta data type."""
        # 2-step conversion: complex -> float, float -> timedelta
        result = series.to_float(dtype=self.equiv_float, tol=tol, errors=errors)
        return result.to_timedelta(dtype=dtype, **unused)


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



