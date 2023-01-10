from types import MappingProxyType

import numpy as np
cimport numpy as np
import pandas as pd

from .base cimport AdapterType, AtomicType

from pdtypes.error import shorten_list
cimport pdtypes.types.cast as cast
import pdtypes.types.cast as cast


######################
####    MIXINS    ####
######################


class IntegerMixin:

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

    def to_boolean(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> pd.Series:
        """Convert integer data to a boolean data type."""
        # check for overflow
        if series.min() < 0 or series.max() > 1:
            if errors == "coerce":
                series = cast.SeriesWrapper(
                    series.abs().clip(0, 1),
                    is_na=series.isna(),
                    hasnans=series.hasnans
                )
            else:
                index = series[(series < 0) | (series > 1)].index.values
                raise OverflowError(
                    f"values exceed {dtype} range at index "
                    f"{shorten_list(index)}"
                )

        # delegate to AtomicType.to_boolean()
        return super().to_boolean(
            series=series,
            dtype=dtype,
            errors=errors,
            **unused
        )

    def to_integer(
        self,
        series: cast.SeriesWrapper,
        dtype: AtomicType,
        errors: str,
        **unused
    ) -> pd.Series:
        """Convert integer data to another integer data type."""
        # check for overflow
        if series.min() < dtype.min or series.max() > dtype.max:
            if errors == "coerce":
                pass
        
        raise NotImplementedError()

    ######################
    ####    EXTRAS    ####
    ######################

    def downcast(self, series: pd.Series) -> AtomicType:
        """Reduce the itemsize of an integer type to fit the observed range."""
        min_val = series.min()
        max_val = series.max()
        for s in self._smaller:
            try:
                instance = forward_declare[s].instance(backend=self.backend)
            except:
                continue
            if min_val >= instance.min and max_val <= instance.max:
                if isinstance(self, AdapterType):
                    return self.replace(atomic_type=instance)
                return instance
        return self


#####################
####    TYPES    ####
#####################


class IntegerType(IntegerMixin, AtomicType):
    """Integer supertype"""

    name = "int"
    aliases = {
        # type
        int: {},
        np.integer: {"backend": "numpy"},

        # string
        "int": {},
        "integer": {},
    }
    _backends = ("python", "numpy", "pandas")
    _smaller = (
        "Int8Type", "Int16Type", "Int32Type", "Int64Type", "UInt64Type"
    )

    def __init__(self, backend: str = None):
        # int
        if backend is None:
            type_def = int
            dtype = np.dtype(np.int64)
            itemsize = None
            self.min = -np.inf
            self.max = np.inf
            self.__dict__["is_nullable"] = False

        # int[python]
        elif backend == "python":
            type_def = int
            dtype = np.dtype(np.object_)
            itemsize = None
            self.min = -np.inf
            self.max = np.inf

        # int[numpy]
        elif backend == "numpy":
            type_def = np.int64
            dtype = np.dtype(np.int64)
            itemsize = 8
            self.min = -2**63
            self.max = 2**63 - 1
            self.__dict__["is_nullable"] = False

        # int[pandas]
        elif backend == "pandas":
            type_def = np.int64
            dtype = pd.Int64Dtype()
            itemsize = 8
            self.min = -2**63
            self.max = 2**63 - 1

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        super().__init__(
            type_def=type_def,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=itemsize,
            slug=self.slugify(backend=backend)
        )


class SignedIntegerType(IntegerType, supertype=IntegerType):
    """Signed integer type."""

    name = "signed"
    aliases = {
        # type
        np.signedinteger: {"backend": "numpy"},

        # string
        "signed": {},
        "signed int": {},
        "signed integer": {},
        "i": {},
    }
    _smaller = ("Int8Type", "Int16Type", "Int32Type", "Int64Type")

    def __init__(self, backend: str = None):
        # Internally, this is an exact copy of IntegerType
        super().__init__(backend=backend)


class Int8Type(IntegerMixin, AtomicType, supertype=SignedIntegerType):
    """8-bit integer subtype"""

    name = "int8"
    aliases = {
        # type
        np.int8: {"backend": "numpy"},

        # dtype
        np.dtype(np.int8): {"backend": "numpy"},
        pd.Int8Dtype(): {"backend": "pandas"},

        # string
        "int8": {},
        "i1": {},
        "Int8": {"backend": "pandas"},
    }
    _backends = ("numpy", "pandas")
    _smaller = ()

    def __init__(self, backend: str = None):
        # min/max representable values
        self.min = -2**7
        self.max = 2**7 - 1

        # unsigned
        if backend is None:
            dtype = np.dtype(np.int8)
            self.__dict__["is_nullable"] = False

        # unsigned[numpy]
        elif backend == "numpy":
            dtype = np.dtype(np.int8)
            self.__dict__["is_nullable"] = False

        # unsigned[pandas]
        elif backend == "pandas":
            dtype = pd.Int8Dtype()

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        super().__init__(
            type_def=np.int8,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=1,
            slug=self.slugify(backend=backend)
        )


class Int16Type(IntegerMixin, AtomicType, supertype=SignedIntegerType):
    """16-bit integer subtype"""

    name = "int16"
    aliases = {
        # type
        np.int16: {"backend": "numpy"},

        # dtype
        np.dtype(np.int16): {"backend": "numpy"},
        pd.Int16Dtype(): {"backend": "pandas"},

        # string
        "int16": {},
        "i2": {},
        "Int16": {"backend": "pandas"},
    }
    _backends = ("numpy", "pandas")
    _smaller = ("Int8Type",)

    def __init__(self, backend: str = None):
        # min/max representable values
        self.min = -2**15
        self.max = 2**15 - 1

        # unsigned
        if backend is None:
            dtype = np.dtype(np.int16)
            self.__dict__["is_nullable"] = False

        # unsigned[numpy]
        elif backend == "numpy":
            dtype = np.dtype(np.int16)
            self.__dict__["is_nullable"] = False

        # unsigned[pandas]
        elif backend == "pandas":
            dtype = pd.Int16Dtype()

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        super().__init__(
            type_def=np.int16,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=2,
            slug=self.slugify(backend=backend)
        )


class Int32Type(IntegerMixin, AtomicType, supertype=SignedIntegerType):
    """32-bit integer subtype"""

    name = "int32"
    aliases = {
        # type
        np.int32: {"backend": "numpy"},

        # dtype
        np.dtype(np.int32): {"backend": "numpy"},
        pd.Int32Dtype(): {"backend": "pandas"},

        # string
        "int32": {},
        "i4": {},
        "Int32": {"backend": "pandas"},
    }
    _backends = ("numpy", "pandas")
    _smaller = ("Int8Type", "Int16Type")

    def __init__(self, backend: str = None):
        # min/max representable values
        self.min = -2**31
        self.max = 2**31 - 1

        # unsigned
        if backend is None:
            dtype = np.dtype(np.int32)
            self.__dict__["is_nullable"] = False

        # unsigned[numpy]
        elif backend == "numpy":
            dtype = np.dtype(np.int32)
            self.__dict__["is_nullable"] = False

        # unsigned[pandas]
        elif backend == "pandas":
            dtype = pd.Int32Dtype()

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        super().__init__(
            type_def=np.int32,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=4,
            slug=self.slugify(backend=backend)
        )


class Int64Type(IntegerMixin, AtomicType, supertype=SignedIntegerType):
    """64-bit integer subtype"""

    name = "int64"
    aliases = {
        # type
        np.int64: {"backend": "numpy"},

        # dtype
        np.dtype(np.int64): {"backend": "numpy"},
        pd.Int64Dtype(): {"backend": "pandas"},

        # string
        "int64": {},
        "i8": {},
        "Int64": {"backend": "pandas"},
    }
    _backends = ("numpy", "pandas")
    _smaller = ("Int8Type", "Int16Type", "Int32Type")

    def __init__(self, backend: str = None):
        # min/max representable values
        self.min = -2**63
        self.max = 2**63 - 1

        # unsigned
        if backend is None:
            dtype = np.dtype(np.int64)
            self.__dict__["is_nullable"] = False

        # unsigned[numpy]
        elif backend == "numpy":
            dtype = np.dtype(np.int64)
            self.__dict__["is_nullable"] = False

        # unsigned[pandas]
        elif backend == "pandas":
            dtype = pd.Int64Dtype()

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        super().__init__(
            type_def=np.int64,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=8,
            slug=self.slugify(backend=backend)
        )


class UnsignedIntegerType(IntegerMixin, AtomicType, supertype=IntegerType):
    """Unsigned integer supertype"""

    name = "unsigned"
    aliases = {
        # type
        np.unsignedinteger: {"backend": "numpy"},

        # string
        "unsigned": {},
        "unsigned int": {},
        "unsigned integer": {},
        "uint": {},
        "u": {},
    }
    _backends = ("numpy", "pandas")
    _smaller = ("UInt8Type", "UInt16Type", "UInt32Type", "UInt64Type")

    def __init__(self, backend: str = None):
        # min/max representable values
        self.min = 0
        self.max = 2**64 - 1

        # unsigned
        if backend is None:
            dtype = np.dtype(np.uint64)
            self.__dict__["is_nullable"] = False

        # unsigned[numpy]
        elif backend == "numpy":
            dtype = np.dtype(np.uint64)
            self.__dict__["is_nullable"] = False

        # unsigned[pandas]
        elif backend == "pandas":
            dtype = pd.UInt64Dtype()

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        super().__init__(
            type_def=np.uint64,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=8,
            slug=self.slugify(backend=backend)
        )


class UInt8Type(IntegerMixin, AtomicType, supertype=UnsignedIntegerType):
    """8-bit unsigned integer subtype"""

    name = "uint8"
    aliases = {
        # type
        np.uint8: {"backend": "numpy"},

        # dtype
        np.dtype(np.uint8): {"backend": "numpy"},
        pd.UInt8Dtype(): {"backend": "pandas"},

        # string
        "uint8": {},
        "u1": {},
        "UInt8": {"backend": "pandas"},
    }
    _backends = ("numpy", "pandas")
    _smaller = ()

    def __init__(self, backend: str = None):
        # min/max representable values
        self.min = 0
        self.max = 2**8 - 1

        # unsigned
        if backend is None:
            dtype = np.dtype(np.uint8)
            self.__dict__["is_nullable"] = False

        # unsigned[numpy]
        elif backend == "numpy":
            dtype = np.dtype(np.uint8)
            self.__dict__["is_nullable"] = False

        # unsigned[pandas]
        elif backend == "pandas":
            dtype = pd.UInt8Dtype()

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        super().__init__(
            type_def=np.uint8,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=1,
            slug=self.slugify(backend=backend)
        )


class UInt16Type(IntegerMixin, AtomicType, supertype=UnsignedIntegerType):
    """16-bit unsigned integer subtype"""

    name = "uint16"
    aliases = {
        # type
        np.uint16: {"backend": "numpy"},

        # dtype
        np.dtype(np.uint16): {"backend": "numpy"},
        pd.UInt16Dtype(): {"backend": "pandas"},

        # string
        "uint16": {},
        "u2": {},
        "UInt16": {"backend": "pandas"},
    }
    _backends = ("numpy", "pandas")
    _smaller = ("UInt8Type",)

    def __init__(self, backend: str = None):
        # min/max representable values
        self.min = 0
        self.max = 2**16 - 1

        # unsigned
        if backend is None:
            dtype = np.dtype(np.uint16)
            self.__dict__["is_nullable"] = False

        # unsigned[numpy]
        elif backend == "numpy":
            dtype = np.dtype(np.uint16)
            self.__dict__["is_nullable"] = False

        # unsigned[pandas]
        elif backend == "pandas":
            dtype = pd.UInt16Dtype()

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        super().__init__(
            type_def=np.uint16,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=2,
            slug=self.slugify(backend=backend)
        )


class UInt32Type(IntegerMixin, AtomicType, supertype=UnsignedIntegerType):
    """32-bit unsigned integer subtype"""

    name = "uint32"
    aliases = {
        # type
        np.uint32: {"backend": "numpy"},

        # dtype
        np.dtype(np.uint32): {"backend": "numpy"},
        pd.UInt32Dtype(): {"backend": "pandas"},

        # string
        "uint32": {},
        "u4": {},
        "UInt32": {"backend": "pandas"},
    }
    _backends = ("numpy", "pandas")
    _smaller = ("UInt8Type", "UInt16Type")

    def __init__(self, backend: str = None):
        # min/max representable values
        self.min = 0
        self.max = 2**32 - 1

        # unsigned
        if backend is None:
            dtype = np.dtype(np.uint32)
            self.__dict__["is_nullable"] = False

        # unsigned[numpy]
        elif backend == "numpy":
            dtype = np.dtype(np.uint32)
            self.__dict__["is_nullable"] = False

        # unsigned[pandas]
        elif backend == "pandas":
            dtype = pd.UInt32Dtype()

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        super().__init__(
            type_def=np.uint32,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=4,
            slug=self.slugify(backend=backend)
        )


class UInt64Type(IntegerMixin, AtomicType, supertype=UnsignedIntegerType):
    """32-bit unsigned integer subtype"""

    name = "uint64"
    aliases = {
        # type
        np.uint64: {"backend": "numpy"},

        # dtype
        np.dtype(np.uint64): {"backend": "numpy"},
        pd.UInt64Dtype(): {"backend": "pandas"},

        # string
        "uint64": {},
        "u8": {},
        "UInt64": {"backend": "pandas"},
    }
    _backends = ("numpy", "pandas")
    _smaller = ("UInt8Type", "UInt16Type", "UInt32Type")

    def __init__(self, backend: str = None):
        # min/max representable values
        self.min = 0
        self.max = 2**64 - 1

        # unsigned
        if backend is None:
            dtype = np.dtype(np.uint64)
            self.__dict__["is_nullable"] = False

        # unsigned[numpy]
        elif backend == "numpy":
            dtype = np.dtype(np.uint64)
            self.__dict__["is_nullable"] = False

        # unsigned[pandas]
        elif backend == "pandas":
            dtype = pd.UInt64Dtype()

        # unrecognized
        else:
            raise TypeError(
                f"{self.name} backend not recognized: {repr(backend)}"
            )

        self.backend = backend

        super().__init__(
            type_def=np.uint64,
            dtype=dtype,
            na_value=pd.NA,
            itemsize=8,
            slug=self.slugify(backend=backend)
        )


#########################################
####    PLATFORM-SPECIFIC ALIASES    ####
#########################################


# the following aliases are platform-specific and may be assigned to different
# AtomicTypes based on hardware configuration.  Luckily, numpy's dtype()
# factory automatically resolves these, so we can just piggyback off it.
cdef dict platform_specific_aliases = {
    # C char
    "char": np.dtype(np.byte),
    "signed char": "char",
    "byte": "char",
    "b": "char",

    # C short
    "short": np.dtype(np.short),
    "short int": "short",
    "short integer": "short",
    "signed short": "short",
    "signed short int": "short",
    "signed short integer": "short",
    "h": "short",

    # C int
    "intc": np.dtype(np.intc),
    "signed intc": "intc",

    # C long
    "long": np.dtype(np.int_),
    "long int": "long",
    "long integer": "long",
    "signed long": "long",
    "signed long int": "long",
    "signed long integer": "long",
    "l": "long",

    # C long long
    "long long": np.dtype(np.longlong),
    "long long int": "long long",
    "long long integer": "long long",
    "signed long long": "long long",
    "signed long long int": "long long",
    "signed long long integer": "long long",
    "longlong": "long long",
    "signed longlong": "long long",
    "q": "long long",

    # C ssize_t
    "ssize_t": np.dtype(np.intp),
    "intp": "ssize_t",
    "int0": "ssize_t",
    "p": "ssize_t",

    # C unsigned char
    "unsigned char": np.dtype(np.ubyte),
    "unsigned byte": "unsigned char",
    "ubyte": "unsigned char",
    "B": "unsigned char",

    # C unsigned short
    "unsigned short": np.dtype(np.ushort),
    "unsigned short int": "unsigned short",
    "unsigned short integer": "unsigned short",
    "ushort": "unsigned short",
    "H": "unsigned short",

    # C unsigned int
    "unsigned intc": np.dtype(np.uintc),
    "uintc": "unsigned intc",
    "I": "unsigned intc",

    # C unsigned long
    "unsigned long": np.dtype(np.uint),
    "unsigned long int": "unsigned long",
    "unsigned long integer": "unsigned long",
    "ulong": "unsigned long",
    "L": "unsigned long",

    # C unsigned long long
    "unsigned long long": np.dtype(np.ulonglong),
    "unsigned long long int": "unsigned long long",
    "unsigned long long integer": "unsigned long long",
    "ulonglong": "unsigned long long",
    "unsigned longlong": "unsigned long long",
    "Q": "unsigned long long",

    # C size_t
    "size_t": np.dtype(np.uintp),
    "uintp": "size_t",
    "uint0": "size_t",
    "P": "size_t",
}
for alias, lookup in platform_specific_aliases.items():
    info = AtomicType.registry.aliases[lookup]
    info.base.register_alias(alias, defaults={})


#######################
####    PRIVATE    ####
#######################


cdef dict forward_declare = {
    "IntegerType": IntegerType,
    "SignedIntegerType": SignedIntegerType,
    "Int8Type": Int8Type,
    "Int16Type": Int16Type,
    "Int32Type": Int32Type,
    "Int64Type": Int64Type,
    "UnsignedIntegerType": UnsignedIntegerType,
    "UInt8Type": UInt8Type,
    "UInt16Type": UInt16Type,
    "UInt32Type": UInt32Type,
    "UInt64Type": UInt64Type
}

