import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AdapterType, AtomicType
from .base import generic
cimport pdtypes.types.atomic.float as float_types

from pdtypes.error import shorten_list
cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast


# TODO: consider ftol in downcast()?

# TODO: forward declare should replace the string with the appropriate
# class definition.  That way, extending .downcast should consist of just
# appending a class definition to type.smaller (list).


#########################
####    CONSTANTS    ####
#########################


cdef bint has_clongdouble = np.dtype(np.clongdouble).itemsize > 16


######################
####    MIXINS    ####
######################


class ComplexMixin:

    def downcast(self, series: pd.Series) -> AtomicType:
        """Reduce the itemsize of a complex type to fit the observed range."""
        for s in self._smaller:
            try:
                instance = forward_declare[s].instance(backend=self.backend)
            except:
                continue
            attempt = series.astype(instance.dtype)
            if (attempt == series).all():
                return instance
        return self

    @property
    def equiv_float(self) -> AtomicType:
        _class = float_types.forward_declare[self._equiv_float]
        return _class.instance(backend=self.backend)


#############################
####    GENERIC TYPES    ####
#############################


@generic
class ComplexType(
    ComplexMixin,
    AtomicType
):

    name = "complex"
    aliases = {
        complex, "complex", "cfloat", "complex float", "complex floating", "c"
    }
    _equiv_float = "FloatType"
    _smaller = ("Complex64Type", "Complex128Type", "Complex160Type")

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
class Complex64Type(
    ComplexMixin,
    AtomicType,
    supertype=ComplexType
):

    name = "complex64"
    aliases = {
        "complex64", "csingle", "complex single", "singlecomplex", "c8", "F"
    }
    _equiv_float = "Float32Type"
    _smaller = ()

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
class Complex128Type(
    ComplexMixin,
    AtomicType,
    supertype=ComplexType
):

    name = "complex128"
    aliases = {
        "complex128", "cdouble", "complex double", "complex_", "c16", "D"
    }
    _equiv_float = "Float64Type"
    _smaller = ("Complex64Type",)

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
class Complex160Type(
    ComplexMixin,
    AtomicType,
    add_to_registry=has_clongdouble,
    supertype=ComplexType
):

    name = "complex160"
    aliases = {
        "complex160", "clongdouble", "clongfloat", "complex longdouble",
        "complex longfloat", "complex long double", "complex long float",
        "longcomplex", "long complex", "c20", "G"
    }
    _equiv_float = "Float80Type"
    _smaller = ("Complex64Type", "Complex128Type")

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
class NumpyComplexType(
    ComplexMixin,
    AtomicType
):

    aliases = {np.complexfloating}
    _equiv_float = "NumpyFloatType"
    _smaller = (
        "NumpyComplex64Type", "NumpyComplex128Type", "NumpyComplex160Type"
    )

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
class NumpyComplex64Type(
    ComplexMixin,
    AtomicType,
    supertype=NumpyComplexType
):

    aliases = {np.complex64, np.dtype(np.complex64)}
    _equiv_float = "NumpyFloat32Type"
    _smaller = ()

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
class NumpyComplex128Type(
    ComplexMixin,
    AtomicType,
    supertype=NumpyComplexType
):

    aliases = {np.complex128, np.dtype(np.complex128)}
    _equiv_float = "NumpyFloat64Type"
    _smaller = ("NumpyComplex64Type",)

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
class NumpyComplex160Type(
    ComplexMixin,
    AtomicType,
    add_to_registry=has_clongdouble,
    supertype=NumpyComplexType
):

    aliases = {np.clongdouble, np.dtype(np.clongdouble)}
    _equiv_float = "NumpyFloat80Type"
    _smaller = ("NumpyComplex64Type", "NumpyComplex128Type")

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
class PythonComplexType(
    ComplexMixin,
    AtomicType
):

    aliases = set()
    _equiv_float = "PythonFloatType"
    _smaller = ()

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
class PythonComplex128Type(
    ComplexMixin,
    AtomicType,
    supertype=PythonComplexType
):

    aliases = set()
    _equiv_float = "PythonFloat64Type"
    _smaller = ()

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
