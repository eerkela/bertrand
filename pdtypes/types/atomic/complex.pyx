import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AdapterType, AtomicType
from .base import generic, subtype
cimport pdtypes.types.atomic.float as float_types

from pdtypes.error import shorten_list
cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast


# TODO: consider ftol in downcast()?


# TODO: might need a special case for root FloatType that considers
# Complex160Type in downcast


# TODO: come up with a way to link equiv_complex/equiv_float that doesn't
# rely on forward declaration


#########################
####    CONSTANTS    ####
#########################


cdef bint no_clongdouble = not (np.dtype(np.clongdouble).itemsize > 16)


######################
####    MIXINS    ####
######################


class ComplexMixin:

    def downcast(self, series: pd.Series) -> AtomicType:
        """Reduce the itemsize of a complex type to fit the observed range."""
        for s in self.smaller:
            attempt = series.astype(s.dtype)
            if (attempt == series).all():
                return s
        return self

    @property
    def equiv_float(self) -> AtomicType:
        _class = float_types.forward_declare[self._equiv_float]
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
class ComplexType(ComplexMixin, AtomicType):

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


###########################
####    NUMPY TYPES    ####
###########################


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


@Complex64Type.register_backend("numpy")
@subtype(NumpyComplexType)
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


@Complex128Type.register_backend("numpy")
@subtype(NumpyComplexType)
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


@Complex160Type.register_backend("numpy")
@subtype(NumpyComplexType)
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


############################
####    PYTHON TYPES    ####
############################


@ComplexType.register_backend("python")
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


@Complex128Type.register_backend("python")
@subtype(PythonComplexType)
class PythonComplex128Type(ComplexMixin, AtomicType):

    aliases = set()
    _equiv_float = "PythonFloat64Type"

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


cdef dict forward_declare = locals()
