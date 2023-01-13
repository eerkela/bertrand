from types import MappingProxyType

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AdapterType, AtomicType
from .base import generic, subtype
cimport pdtypes.types.atomic.complex as complex_types

from pdtypes.error import shorten_list
cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast


# TODO: some types are aliases of each other.  Can reduce repetition by
# exploiting inheritance.


# TODO: float methods are not aware of when they are wrapped in an AdapterType.
# The only way around this is to implement a separate AdapterType method that
# calls the base method and just wraps the result.  This will always be
# exposed, but may throw an AttributeError if the wrapped AtomicType does not
# implement that method.


# TODO: consider ftol in downcast()?
# -> ftol -> tol, signifies the maximum amount of precision loss that can
# occur before an error is raised.  If this is complex, real and imaginary
# components are considered separately.
# -> implement a Tolerance object as a cdef class, similar to Timezone, Epoch


# TODO: might need a special case for root FloatType that considers
# Float80Type in downcast


# TODO: come up with a way to link equiv_complex/equiv_float that doesn't
# rely on forward declaration


#########################
####    CONSTANTS    ####
#########################


cdef bint no_longdouble = not (np.dtype(np.longdouble).itemsize > 8)


######################
####    MIXINS    ####
######################


class FloatMixin:

    def downcast(self, series: pd.Series) -> AtomicType:
        """Reduce the itemsize of a float type to fit the observed range."""
        for s in self.smaller:
            attempt = series.astype(s.dtype)
            if (attempt == series).all():
                return s
        return self

    @property
    def equiv_complex(self) -> AtomicType:
        _class = complex_types.forward_declare[self._equiv_complex]
        return _class.instance(backend=self.backend)

    @property
    def smaller(self) -> list:
        subtypes = [
            x for x in self.root.subtypes if (
                x.backend == self.backend and
                (x.itemsize or np.inf) < (self.itemsize or np.inf)
            )
        ]
        subtypes.sort(key=lambda x: x.itemsize)
        return subtypes


#############################
####    GENERIC TYPES    ####
#############################


@generic
class FloatType(FloatMixin, AtomicType):

    name = "float"
    aliases = {float, "float", "floating", "f"}
    _equiv_complex = "ComplexType"

    def __init__(self):
        type_def = float
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.float64),
            na_value=np.nan,
            itemsize=8
        )


@generic
@subtype(FloatType)
class Float16Type(FloatMixin, AtomicType):

    name = "float16"
    aliases = {"float16", "half", "f2", "e"}
    _equiv_complex = "Complex64Type"

    def __init__(self):
        type_def = np.float16
        self.min = type_def(-2**11)
        self.max = type_def(2**11)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.float16),
            na_value=np.nan,
            itemsize=2
        )


@generic
@subtype(FloatType)
class Float32Type(FloatMixin, AtomicType):

    name = "float32"
    aliases = {"float32", "single", "f4"}
    _equiv_complex = "Complex64Type"

    def __init__(self):
        type_def = np.float32
        self.min = type_def(-2**24)
        self.max = type_def(2**24)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.float32),
            na_value=np.nan,
            itemsize=4
        )


@generic
@subtype(FloatType)
class Float64Type(FloatMixin, AtomicType):

    name = "float64"
    aliases = {"float64", "double", "float_", "f8", "d"}
    _equiv_complex = "Complex128Type"

    def __init__(self):
        type_def = float
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.float64),
            na_value=np.nan,
            itemsize=8
        )


@generic
@subtype(FloatType)
class Float80Type(FloatMixin, AtomicType, ignore=no_longdouble):

    name = "float80"
    aliases = {
        "float80", "longdouble", "longfloat", "long double", "long float",
        "f10", "g"
    }
    _equiv_complex = "Complex160Type"

    def __init__(self):
        type_def = np.longdouble
        self.min = type_def(-2**64)
        self.max = type_def(2**64)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.longdouble),
            na_value=np.nan,
            itemsize=np.dtype(np.longdouble).itemsize
        )


###########################
####    NUMPY TYPES    ####
###########################


@FloatType.register_backend("numpy")
class NumpyFloatType(FloatMixin, AtomicType):

    aliases = {np.floating}
    _equiv_complex = "NumpyComplexType"

    def __init__(self):
        type_def = np.float64
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.float64),
            na_value=np.nan,
            itemsize=8
        )


@Float16Type.register_backend("numpy")
@subtype(NumpyFloatType)
class NumpyFloat16Type(FloatMixin, AtomicType):

    aliases = {np.float16, np.dtype(np.float16)}
    _equiv_complex = "NumpyComplex64Type"

    def __init__(self):
        type_def = np.float16
        self.min = type_def(-2**11)
        self.max = type_def(2**11)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.float16),
            na_value=np.nan,
            itemsize=2
        )


@Float32Type.register_backend("numpy")
@subtype(NumpyFloatType)
class NumpyFloat32Type(FloatMixin, AtomicType):

    aliases = {np.float32, np.dtype(np.float32)}
    _equiv_complex = "NumpyComplex64Type"

    def __init__(self):
        type_def = np.float32
        self.min = type_def(-2**24)
        self.max = type_def(2**24)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.float32),
            na_value=np.nan,
            itemsize=4
        )


@Float64Type.register_backend("numpy")
@subtype(NumpyFloatType)
class NumpyFloat64Type(FloatMixin, AtomicType):

    aliases = {np.float64, np.dtype(np.float64)}
    _equiv_complex = "NumpyComplex128Type"

    def __init__(self):
        type_def = np.float64
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.float64),
            na_value=np.nan,
            itemsize=8
        )


@Float80Type.register_backend("numpy")
@subtype(NumpyFloatType)
class NumpyFloat80Type(FloatMixin, AtomicType, ignore=no_longdouble):

    aliases = {np.longdouble, np.dtype(np.longdouble)}
    _equiv_complex = "NumpyComplex160Type"

    def __init__(self):
        type_def = np.longdouble
        self.min = type_def(-2**64)
        self.max = type_def(2**64)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype(np.longdouble),
            na_value=np.nan,
            itemsize=np.dtype(np.longdouble).itemsize
        )


############################
####    PYTHON TYPES    ####
############################


@FloatType.register_backend("python")
class PythonFloatType(FloatMixin, AtomicType):

    aliases = set()
    _equiv_complex = "PythonComplexType"

    def __init__(self):
        type_def = float
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype("O"),
            na_value=np.nan,
            itemsize=8
        )


@Float64Type.register_backend("python")
@subtype(NumpyFloatType)
class PythonFloat64Type(FloatMixin, AtomicType):

    aliases = set()
    _equiv_complex = "PythonComplex128Type"

    def __init__(self):
        type_def = float
        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        super().__init__(
            type_def=type_def,
            dtype=np.dtype("O"),
            na_value=np.nan,
            itemsize=8
        )


#######################
####    PRIVATE    ####
#######################


cdef dict forward_declare = locals()
