from types import MappingProxyType

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AdapterType, AtomicType
cimport pdtypes.types.atomic.float as float_types

from pdtypes.error import shorten_list
cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast


# TODO: consider ftol in downcast()?


#########################
####    CONSTANTS    ####
#########################


cdef bint has_clongdouble = np.dtype(np.clongdouble).itemsize > 16


######################
####    MIXINS    ####
######################


class ComplexMixin:

    ########################
    ####    REQUIRED    ####
    ########################

    @classmethod
    def slugify(cls, backend: str = None) -> str:
        slug = cls.name
        if backend is not None:
            slug += f"[{backend}]"
        return slug

    @property
    def kwargs(self) -> MappingProxyType:
        return MappingProxyType({"backend": self.backend})

    ##############################
    ####    CUSTOMIZATIONS    ####
    ##############################

    def _generate_subtypes(self, types: set) -> frozenset:
        # treat backend=None as wildcard
        kwargs = [self.kwargs]
        if self.backend is None:
            kwargs.extend([
                {**kw, **{"backend": b}}
                for kw in kwargs
                for b in self._backends
            ])

        # build result, skipping invalid kwargs
        result = set()
        for t in types:
            for kw in kwargs:
                try:
                    result.add(t.instance(**kw))
                except TypeError:
                    continue

        # return as frozenset
        return frozenset(result)

    ######################
    ####    EXTRAS    ####
    ######################

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


############################
####    ATOMIC TYPES    ####
############################


class ComplexType(ComplexMixin, AtomicType):

    name = "complex"
    aliases = {
        complex: {},
        np.complexfloating: {"backend": "numpy"},
        "complex": {},
        "cfloat": {},
        "complex float": {},
        "complex floating": {},
        "c": {},
    }
    _backends = ("python", "numpy")
    _equiv_float = "FloatType"
    _smaller = ("Complex64Type", "Complex128Type", "Complex160Type")

    def __init__(self, backend: str = None):
        # complex
        if backend is None:
            type_def = complex
            dtype = np.dtype(np.complex128)

        # complex[python]
        elif backend == "python":
            type_def = complex
            dtype = np.dtype(np.object_)

        # complex[numpy]
        elif backend == "numpy":
            type_def = np.complex128
            dtype = np.dtype(np.complex128)

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.min = type_def(-2**53)
        self.max = type_def(2**53)
        self.backend = backend

        super().__init__(
            type_def=type_def,
            dtype=dtype,
            na_value=type_def("nan+nanj"),
            itemsize=16,
            slug=self.slugify(backend=backend)
        )


class Complex64Type(ComplexMixin, AtomicType, supertype=ComplexType):

    name = "complex64"
    aliases = {
        np.complex64: {"backend": "numpy"},
        np.dtype(np.complex64): {"backend": "numpy"},
        "complex64": {},
        "csingle": {},
        "complex single": {},
        "singlecomplex": {},
        "c8": {},
        "F": {},
    }
    _backends = ("numpy",)
    _equiv_float = "Float32Type"
    _smaller = ()

    def __init__(self, backend: str = None):
        # unrecognized
        if backend not in (None, "numpy"):
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend
        self.min = np.complex64(-2**24)
        self.max = np.complex64(2**24)

        super().__init__(
            type_def=np.complex64,
            dtype=np.dtype(np.complex64),
            na_value=np.complex64("nan+nanj"),
            itemsize=8,
            slug=self.slugify(backend=backend)
        )


class Complex128Type(ComplexMixin, AtomicType, supertype=ComplexType):

    name = "complex128"
    aliases = {
        np.complex128: {"backend": "numpy"},
        np.dtype(np.complex128): {"backend": "numpy"},
        "complex128": {},
        "cdouble": {},
        "complex double": {},
        "complex_": {},
        "c16": {},
        "D": {},
    }
    _backends = ("python", "numpy")
    _equiv_float = "Float64Type"
    _smaller = ("Complex64Type",)

    def __init__(self, backend: str = None):
        # complex128
        if backend is None:
            type_def = complex
            dtype = np.dtype(np.complex128)

        # complex128[python]
        elif backend == "python":
            type_def = complex
            dtype = np.dtype(np.object_)

        # complex128[numpy]
        elif backend == "numpy":
            type_def = np.complex128
            dtype = np.dtype(np.complex128)

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend
        self.min = type_def(-2**53)
        self.max = type_def(2**53)

        super().__init__(
            type_def=type_def,
            dtype=dtype,
            na_value=type_def("nan+nanj"),
            itemsize=16,
            slug=self.slugify(backend=backend)
        )


class Complex160Type(
    ComplexMixin,
    AtomicType,
    add_to_registry=has_clongdouble,
    supertype=ComplexType if has_clongdouble else None
):

    name = "complex160"
    aliases = {
        np.clongdouble: {"backend": "numpy"},
        np.dtype(np.clongdouble): {"backend": "numpy"},
        "complex160": {},
        "clongdouble": {},
        "clongfloat": {},
        "complex longdouble": {},
        "complex longfloat": {},
        "complex long double": {},
        "complex long float": {},
        "longcomplex": {},
        "long complex": {},
        "c20": {},
        "G": {},
    }
    _backends = ("numpy")
    _equiv_float = "Float80Type"
    _smaller = ("Complex64Type", "Complex128Type")

    def __init__(self, backend: str = None):
        # unrecognized
        if backend not in (None, "numpy"):
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend
        self.min = np.clongdouble(-2**64)
        self.max = np.clongdouble(2**64)

        super().__init__(
            type_def=np.clongdouble,
            dtype=np.dtype(np.clongdouble),
            na_value=np.clongdouble("nan+nanj"),
            itemsize=np.dtype(np.clongdouble).itemsize,
            slug=self.slugify(backend=backend)
        )


#######################
####    PRIVATE    ####
#######################


cdef dict forward_declare = {
    "ComplexType": ComplexType,
    "Complex64Type": Complex64Type,
    "Complex128Type": Complex128Type,
    "Complex160Type": Complex160Type,
}
