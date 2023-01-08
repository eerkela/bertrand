from types import MappingProxyType

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AdapterType, AtomicType
cimport pdtypes.types.atomic.complex as complex_types

from pdtypes.error import shorten_list
cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast


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


#########################
####    CONSTANTS    ####
#########################


cdef bint has_longdouble = np.dtype(np.longdouble).itemsize > 8


######################
####    MIXINS    ####
######################


class FloatMixin:

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
        """Reduce the itemsize of a float type to fit the observed range."""
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
    def equiv_complex(self) -> AtomicType:
        _class = complex_types.forward_declare[self._equiv_complex]
        return _class.instance(backend=self.backend)


############################
####    ATOMIC TYPES    ####
############################


class FloatType(FloatMixin, AtomicType):

    name = "float"
    aliases = {
        float: {},
        np.floating: {"backend": "numpy"},
        "float": {},
        "floating": {},
        "f": {},
    }
    _backends = ("python", "numpy")
    _equiv_complex = "ComplexType"
    _smaller = ("Float16Type", "Float32Type", "Float64Type", "Float80Type")

    def __init__(self, backend: str = None):
        # float
        if backend is None:
            type_def = float
            dtype = np.dtype(np.float64)

        # float[python]
        elif backend == "python":
            type_def = float
            dtype = np.dtype(np.object_)

        # float[numpy]
        elif backend == "numpy":
            type_def = np.float64
            dtype = np.dtype(np.float64)

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend
        self.min = type_def(-2**53)
        self.max = type_def(2**53)

        super(FloatType, self).__init__(
            type_def=type_def,
            dtype=dtype,
            na_value=np.nan,
            itemsize=8,
            slug=self.slugify(backend=backend)
        )


class Float16Type(FloatMixin, AtomicType):

    name = "float16"
    aliases = {
        np.float16: {"backend": "numpy"},
        np.dtype(np.float16): {"backend": "numpy"},
        "float16": {},
        "half": {},
        "f2": {},
        "e": {},
    }
    _backends = ("numpy",)
    _equiv_complex = "Complex64Type"
    _smaller = ()

    def __init__(self, backend: str = None):
        # unrecognized
        if backend not in (None, "numpy"):
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend
        self.min = np.float16(-2**11)
        self.max = np.float16(2**11)

        super(Float16Type, self).__init__(
            type_def=np.float16,
            dtype=np.dtype(np.float16),
            na_value=np.nan,
            itemsize=2,
            slug=self.slugify(backend=backend)
        )


class Float32Type(FloatMixin, AtomicType):

    name = "float32"
    aliases = {
        np.float32: {"backend": "numpy"},
        np.dtype(np.float32): {"backend": "numpy"},
        "float32": {},
        "single": {},
        "f4": {},
    }
    _backends = ("numpy")
    _equiv_complex = "Complex64Type"
    _smaller = ("Float16Type",)

    def __init__(self, backend: str = None):
        # unrecognized
        if backend not in (None, "numpy"):
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend
        self.min = np.float32(-2**24)
        self.max = np.float32(2**24)

        super(Float32Type, self).__init__(
            type_def=np.float32,
            dtype=np.dtype(np.float32),
            na_value=np.nan,
            itemsize=4,
            slug=self.slugify(backend=backend)
        )


class Float64Type(FloatMixin, AtomicType):

    name = "float64"
    aliases = {
        np.float64: {"backend": "numpy"},
        np.dtype(np.float64): {"backend": "numpy"},
        "float64": {},
        "double": {},
        "float_": {},
        "f8": {},
        "d": {},
    }
    _backends = ("python", "numpy")
    _equiv_complex = "Complex128Type"
    _smaller = ("Float16Type", "Float32Type")

    def __init__(self, backend: str = None):
        # float64
        if backend is None:
            type_def = float
            dtype = np.dtype(np.float64)

        # float64[python]
        elif backend == "python":
            type_def = float
            dtype = np.dtype(np.object_)

        # float64[numpy]
        elif backend == "numpy":
            type_def = np.float64
            dtype = np.dtype(np.float64)

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend
        self.min = type_def(-2**53)
        self.max = type_def(2**53)

        super(Float64Type, self).__init__(
            type_def=type_def,
            dtype=dtype,
            na_value=np.nan,
            itemsize=8,
            slug=self.slugify(backend=backend)
        )


class Float80Type(FloatMixin, AtomicType, add_to_registry=has_longdouble):

    name = "float80"
    aliases = {
        np.longdouble: {"backend": "numpy"},
        np.dtype(np.longdouble): {"backend": "numpy"},
        "float80": {},
        "longdouble": {},
        "longfloat": {},
        "long double": {},
        "long float": {},
        "f10": {},
        "g": {},
    }
    _backends = ("numpy")
    _equiv_complex = "Complex160Type"
    _smaller = ("Float16Type", "Float32Type", "Float64Type")

    def __init__(self, backend: str = None):
        # unrecognized
        if backend not in (None, "numpy"):
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend
        self.min = np.longdouble(-2**64)
        self.max = np.longdouble(2**64)

        super(Float80Type, self).__init__(
            type_def=np.longdouble,
            dtype=np.dtype(np.longdouble),
            na_value=np.nan,
            itemsize=np.dtype(np.longdouble).itemsize,
            slug=self.slugify(backend=backend)
        )


##########################
#####    HIERARCHY    ####
##########################


Float16Type.register_supertype(FloatType)
Float32Type.register_supertype(FloatType)
Float64Type.register_supertype(FloatType)
if has_longdouble:
    Float80Type.register_supertype(FloatType)


#######################
####    PRIVATE    ####
#######################


cdef dict forward_declare = {
    "FloatType": FloatType,
    "Float16Type": Float16Type,
    "Float32Type": Float32Type,
    "Float64Type": Float64Type,
    "Float80Type": Float80Type,
}
